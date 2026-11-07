<#
.SYNOPSIS
  計測用ビルドを自動で動かし、プレイ中のフレーム時間と GPU 内訳を出す。

.DESCRIPTION
  デバッグツールと計測を有効にしたビルド（-DWIKIGOLF_DEBUG_TOOLS=ON -DWIKIGOLF_PROFILING=ON）で使う。
  ゲームの設定（画質・解像度など）は、そのビルドの save\settings.ini のまま。

  - -Course を付けると、タイトルのコース選択でそのページを入れて「確認」「スタート！」まで自動で進む。
  - -Course なしなら、標準のランダムなコースへ入る。
  - -AutoShot 秒 を付けると、構えられる状態でその秒数待ってから自動で打つ（パワー 7 割、インパクトはパーフェクト付近）。
  - 起動から -Seconds 秒で自動終了し、最後の -Frames フレームを集計する。
  - -Preset LOW などを付けると、計測の間だけ settings.ini の画質プリセットと内部解像度を書き換え、終わったら戻す。

.EXAMPLE
  pwsh -File tools/bench/Run-Benchmark.ps1 -Course 日本 -Seconds 90 -AutoShot 8
#>
param(
  [string]$Course = '',
  [string]$Goal = '',
  [int]$Seconds = 75,
  [int]$Frames = 600,
  [double]$AutoShot = 0,
  [string]$Label = '',
  [switch]$GrassFull,
  [ValidateSet('', 'LOW', 'MEDIUM', 'HIGH', 'EXHIGH', 'ULTRA')]
  [string]$Preset = '',
  [string]$BuildDir = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot '..\..')
if (-not $BuildDir) { $BuildDir = Join-Path $repoRoot 'build-perf\Release' }
$exe = Join-Path $BuildDir 'WikiGolf.exe'
if (-not (Test-Path $exe)) { throw "WikiGolf.exe が見つかりません: $exe" }

$envNames = 'WIKIGOLF_DEBUG_START', 'WIKIGOLF_DEBUG_EXIT_AFTER', 'WIKIGOLF_DEBUG_COURSE_START',
            'WIKIGOLF_DEBUG_COURSE_GOAL', 'WIKIGOLF_DEBUG_AUTO_SHOT', 'WIKIGOLF_DEBUG_GRASS_FULL'
foreach ($n in $envNames) { Remove-Item "Env:$n" -ErrorAction SilentlyContinue }

if ($Course) {
  $env:WIKIGOLF_DEBUG_COURSE_START = $Course
  if ($Goal) { $env:WIKIGOLF_DEBUG_COURSE_GOAL = $Goal }
} else {
  $env:WIKIGOLF_DEBUG_START = 'golf'
}
$env:WIKIGOLF_DEBUG_EXIT_AFTER = "$Seconds"
if ($AutoShot -gt 0) { $env:WIKIGOLF_DEBUG_AUTO_SHOT = "$AutoShot" }
if ($GrassFull) { $env:WIKIGOLF_DEBUG_GRASS_FULL = '1' }

$settingsPath = Join-Path $BuildDir 'save\settings.ini'
$originalSettings = $null
if ($Preset -and (Test-Path $settingsPath)) {
  # GraphicsPreset.h の各プリセットの内部解像度・MSAA・FXAA と同じ値
  $presetValues = @{
    LOW    = @{ Scale = '0.6'; Msaa = '1'; Fxaa = '1' }
    MEDIUM = @{ Scale = '0.8'; Msaa = '1'; Fxaa = '1' }
    HIGH   = @{ Scale = '1';   Msaa = '1'; Fxaa = '0' }
    EXHIGH = @{ Scale = '1';   Msaa = '4'; Fxaa = '0' }
    ULTRA  = @{ Scale = '1';   Msaa = '8'; Fxaa = '0' }
  }
  $originalSettings = Get-Content $settingsPath -Raw
  $v = $presetValues[$Preset]
  $s = $originalSettings -replace '(?m)^GraphicsPreset=.*$', "GraphicsPreset=$Preset" `
                         -replace '(?m)^RenderScale=.*$', "RenderScale=$($v.Scale)" `
                         -replace '(?m)^MSAA=.*$', "MSAA=$($v.Msaa)" `
                         -replace '(?m)^FXAA=.*$', "FXAA=$($v.Fxaa)"
  Set-Content $settingsPath $s -NoNewline
}

try {
  $p = Start-Process -FilePath $exe -WorkingDirectory $BuildDir -PassThru
  if (-not $p.WaitForExit(($Seconds + 60) * 1000)) {
    Stop-Process -Id $p.Id
    Write-Warning '時間内に終了しなかったので止めました。'
  }
} finally {
  foreach ($n in $envNames) { Remove-Item "Env:$n" -ErrorAction SilentlyContinue }
  if ($null -ne $originalSettings) { Set-Content $settingsPath $originalSettings -NoNewline }
}

$session = Get-ChildItem (Join-Path $BuildDir 'profiling') -Directory | Sort-Object LastWriteTime | Select-Object -Last 1
$scene = 'WikiGolfScene'
$rows = Import-Csv (Join-Path $session.FullName 'performance_frames.csv') |
  Where-Object { $_.scene -eq $scene -and [double]$_.gpu_frame_ms -gt 0 } |
  Select-Object -Last $Frames
if (-not $rows) { Write-Warning "$scene のフレームがありません（コースに入れていない可能性）。"; return }

$rowSet = @{}
$rows | ForEach-Object { $rowSet[$_.frame] = $true }
$gpu = @($rows | ForEach-Object { [double]$_.gpu_frame_ms } | Sort-Object)
$avg = { param($xs) ($xs | Measure-Object -Average).Average }
'[{0}] {1} frames={2}  GPU avg={3:N2} p50={4:N2} p95={5:N2} ms  CPU avg={6:N2} ms  FPS avg={7:N1}' -f `
  $Label, $scene, $rows.Count, (& $avg $gpu), $gpu[[int]($gpu.Count / 2)], $gpu[[int]($gpu.Count * 0.95) - 1],
  (& $avg ($rows | ForEach-Object { [double]$_.cpu_frame_ms })),
  (& $avg ($rows | ForEach-Object { [double]$_.fps }))

Import-Csv (Join-Path $session.FullName 'performance_scopes.csv') |
  Where-Object { $_.domain -eq 'GPU' -and $rowSet.ContainsKey($_.frame) } |
  Group-Object name | ForEach-Object {
    [pscustomobject]@{
      Scope = $_.Name
      AvgMs = [math]::Round((($_.Group | ForEach-Object { [double]$_.inclusive_ms }) | Measure-Object -Sum).Sum / $rows.Count, 2)
    }
  } | Sort-Object AvgMs -Descending | Format-Table -AutoSize | Out-String

Import-Csv (Join-Path $session.FullName 'performance_counters.csv') |
  Where-Object { $rowSet.ContainsKey($_.frame) -and $_.name -match 'GrassBudget|DrawCalls$|Text\.' } |
  Group-Object name | ForEach-Object {
    '{0} avg={1:N2}' -f $_.Name, (($_.Group | ForEach-Object { [double]$_.value }) | Measure-Object -Average).Average
  }

$log = Join-Path $BuildDir 'game_startup.log'
if (Test-Path $log) {
  $shots = @(Select-String -Path $log -Pattern 'Debug auto shot' -SimpleMatch).Count
  if ($AutoShot -gt 0) { "自動ショット: ログ上 $shots 回" }
}
"計測データ: $($session.FullName)"
