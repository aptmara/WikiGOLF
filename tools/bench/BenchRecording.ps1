<#
.SYNOPSIS
  ベンチマーク中のゲーム画面を ffmpeg で録画する（Run-Benchmark.ps1 の -Record から使う）。

.DESCRIPTION
  ハマりどころと対処（詳しくは docs/gpu-profiling-and-bench-recording.md の 3 章）:
  - 離席中は画面がスリープしていて、デスクトップが合成されない。
    gdigrab は真っ黒、ddagrab はフレームが来ずに止まる、gfxcapture は 1 コマで終わる。
    → マウスを 1 画素動かして画面を起こし、SetThreadExecutionState で録画中は消えないようにする。
  - スクリプトから起動したゲームは前面に出ず、他のウィンドウの裏に隠れる。
    → 録画の間だけ最前面（TOPMOST）にする。ゲームが終われば消える。
  - 最前面のゲームは DWM の合成を通らず直接表示されることがあり、gdigrab（GDI）では黒くなる。
    → Desktop Duplication の ddagrab で、モニターから描画範囲だけを切り出す。
  - 起動直後はウィンドウの作り直し・大きさの変更があるので、少し待ってから位置を取る。
#>

Add-Type -AssemblyName System.Windows.Forms
if (-not ('BenchRecordingWin32' -as [type])) {
  Add-Type @'
using System; using System.Runtime.InteropServices;
public static class BenchRecordingWin32 {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, UIntPtr e);
  [DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f);
}
'@
}

function Find-BenchFfmpeg {
  $cmd = Get-Command ffmpeg -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  # winget で入れた ffmpeg は PATH に載らないことがある
  $winget = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
  $found = Get-ChildItem $winget -Directory -Filter '*FFmpeg*' -ErrorAction SilentlyContinue |
    ForEach-Object { Get-ChildItem $_.FullName -Recurse -Filter ffmpeg.exe -ErrorAction SilentlyContinue } |
    Select-Object -First 1
  if ($found) { return $found.FullName }
  throw 'ffmpeg が見つかりません（winget install Gyan.FFmpeg.Essentials で入れられます）'
}

<#
.SYNOPSIS
  ゲームのウィンドウが出るのを待ち、終了の少し前まで録画を始める。戻り値を Stop-BenchRecording に渡す。
#>
function Start-BenchRecording {
  param(
    [Parameter(Mandatory)] [System.Diagnostics.Process]$Game,
    [Parameter(Mandatory)] [int]$Seconds,      # ゲームが自動終了するまでの秒数
    [Parameter(Mandatory)] [string]$Path,
    [int]$OutputIndex = 0,                     # ddagrab のモニター番号（主モニターはふつう 0）
    [int]$Width = 1280
  )
  $ffmpeg = Find-BenchFfmpeg
  [BenchRecordingWin32]::SetProcessDPIAware() | Out-Null

  $deadline = (Get-Date).AddSeconds(40)
  do {
    Start-Sleep -Milliseconds 300
    $Game.Refresh()
  } while ($Game.MainWindowHandle -eq 0 -and -not $Game.HasExited -and (Get-Date) -lt $deadline)
  if ($Game.MainWindowHandle -eq 0) { throw 'ゲームのウィンドウが見つからないため録画できません' }
  Start-Sleep -Seconds 6
  $Game.Refresh()
  $hwnd = $Game.MainWindowHandle

  # 画面を起こし、録画中は消えないようにする（CONTINUOUS | SYSTEM_REQUIRED | DISPLAY_REQUIRED）
  [BenchRecordingWin32]::SetThreadExecutionState([Convert]::ToUInt32('80000003', 16)) | Out-Null
  [BenchRecordingWin32]::mouse_event(1, 1, 0, 0, [UIntPtr]::Zero)
  [BenchRecordingWin32]::mouse_event(1, -1, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Seconds 2

  # 最前面へ（HWND_TOPMOST, NOSIZE | NOMOVE | SHOWWINDOW）
  [BenchRecordingWin32]::SetWindowPos($hwnd, [IntPtr](-1), 0, 0, 0, 0, 0x43) | Out-Null
  $rect = New-Object BenchRecordingWin32+RECT
  [BenchRecordingWin32]::GetClientRect($hwnd, [ref]$rect) | Out-Null
  $origin = New-Object BenchRecordingWin32+POINT
  [BenchRecordingWin32]::ClientToScreen($hwnd, [ref]$origin) | Out-Null
  $monitor = [System.Windows.Forms.Screen]::FromHandle($hwnd).Bounds
  $x = $origin.X - $monitor.X
  $y = $origin.Y - $monitor.Y
  $w = $rect.R - $rect.L
  $h = $rect.B - $rect.T

  # 終了の数秒前で止める（終了後の黒い画面を入れない）
  $elapsed = ((Get-Date) - $Game.StartTime).TotalSeconds
  $recordSeconds = [int][Math]::Max(5, $Seconds - $elapsed - 3)

  New-Item -ItemType Directory -Force (Split-Path $Path) | Out-Null
  $filter = "ddagrab=output_idx=${OutputIndex}:framerate=30:draw_mouse=0:offset_x=${x}:offset_y=${y}:video_size=${w}x${h}," +
            "hwdownload,format=bgra,scale=${Width}:-2,format=yuv420p"
  $arguments = "-hide_banner -loglevel warning -y -filter_complex `"$filter`" -t $recordSeconds " +
               "-fps_mode cfr -r 30 -c:v libx264 -preset veryfast -crf 30 -maxrate 2500k -bufsize 5000k " +
               "-movflags +faststart `"$Path`""
  $log = "$Path.log"
  $process = Start-Process $ffmpeg -ArgumentList $arguments -PassThru -NoNewWindow -RedirectStandardError $log
  [pscustomobject]@{ Process = $process; Path = $Path; Log = $log }
}

<# @brief 録画の終了を待ち、画面の消灯設定を戻す。録画ファイルの情報を返す。 #>
function Stop-BenchRecording {
  param([Parameter(Mandatory)] $Recording)
  if (-not $Recording.Process.WaitForExit(30000)) {
    Stop-Process -Id $Recording.Process.Id
    Write-Warning '録画が終わらなかったので止めました。'
  }
  [BenchRecordingWin32]::SetThreadExecutionState([Convert]::ToUInt32('80000000', 16)) | Out-Null
  if (-not (Test-Path $Recording.Path)) {
    Write-Warning "録画ファイルができませんでした。ログ: $($Recording.Log)"
    return
  }
  $sizeMb = (Get-Item $Recording.Path).Length / 1MB
  if ($sizeMb -lt 0.5) {
    Write-Warning "録画が極端に小さい（${sizeMb}MB）。画面が消えていたか、別のモニターを録った可能性。ログ: $($Recording.Log)"
  } else {
    Remove-Item $Recording.Log -ErrorAction SilentlyContinue
  }
  '録画: {0} ({1:N1} MB)' -f $Recording.Path, $sizeMb
}
