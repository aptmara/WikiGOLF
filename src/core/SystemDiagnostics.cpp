/**
 * @file SystemDiagnostics.cpp
 * @brief 不具合報告用の診断情報の収集
*/

#include "SystemDiagnostics.h"
#include "BuildEdition.h"

#include <windows.h>
#include <dxgi.h>
#include <intrin.h>
#include <psapi.h>
#include <wrl/client.h>

#include <cstdint>
#include <format>
#include <map>
#include <mutex>
#include <sstream>

#pragma comment(lib, "dxgi.lib")

#ifndef WIKIGOLF_BUILD_COMMIT
#define WIKIGOLF_BUILD_COMMIT "unknown"
#endif

namespace core {
namespace {

std::mutex &SectionsMutex() {
  static std::mutex mutex;
  return mutex;
}

std::map<std::string, std::string> &Sections() {
  static std::map<std::string, std::string> sections;
  return sections;
}

std::string WideToUtf8(const wchar_t *wide) {
  if (!wide || !*wide) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0,
                                       nullptr, nullptr);
  if (size <= 1) return {};
  std::string utf8(static_cast<std::size_t>(size - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8.data(), size, nullptr, nullptr);
  return utf8;
}

std::string ReadRegistryString(const wchar_t *valueName) {
  wchar_t buffer[256] = {};
  DWORD size = sizeof(buffer);
  if (RegGetValueW(HKEY_LOCAL_MACHINE,
                   L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", valueName,
                   RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) {
    return {};
  }
  return WideToUtf8(buffer);
}

DWORD ReadRegistryDword(const wchar_t *valueName) {
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegGetValueW(HKEY_LOCAL_MACHINE,
                   L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", valueName,
                   RRF_RT_REG_DWORD, nullptr, &value, &size) != ERROR_SUCCESS) {
    return 0;
  }
  return value;
}

// GetVersionEx は互換モードの値を返すため、ntdll の RtlGetVersion で実際の版を読む
std::string DescribeOs() {
  RTL_OSVERSIONINFOW info = {};
  info.dwOSVersionInfoSize = sizeof(info);
  using RtlGetVersionFn = LONG(WINAPI *)(RTL_OSVERSIONINFOW *);
  if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
    if (auto fn = reinterpret_cast<RtlGetVersionFn>(
            GetProcAddress(ntdll, "RtlGetVersion"))) {
      fn(&info);
    }
  }
  std::string text = std::format("Windows {}.{}.{}.{}", info.dwMajorVersion,
                                 info.dwMinorVersion, info.dwBuildNumber,
                                 ReadRegistryDword(L"UBR"));
  const std::string displayVersion = ReadRegistryString(L"DisplayVersion");
  if (!displayVersion.empty()) text += " (" + displayVersion + ")";
  BOOL wow64 = FALSE;
  IsWow64Process(GetCurrentProcess(), &wow64);
  text += wow64 ? " 32bit" : " 64bit";
  return text;
}

std::string DescribeCpu() {
  int regs[4] = {};
  __cpuid(regs, 0x80000000);
  std::string brand;
  if (static_cast<unsigned>(regs[0]) >= 0x80000004u) {
    char buffer[49] = {};
    for (int i = 0; i < 3; ++i) {
      __cpuid(regs, 0x80000002 + i);
      memcpy(buffer + i * 16, regs, sizeof(regs));
    }
    brand = buffer;
    const auto first = brand.find_first_not_of(' ');
    const auto last = brand.find_last_not_of(' ');
    brand = first == std::string::npos ? "" : brand.substr(first, last - first + 1);
  }
  if (brand.empty()) brand = "unknown";
  return std::format("{} / {} logical processors", brand,
                     GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
}

std::string DescribeMemory() {
  MEMORYSTATUSEX status = {};
  status.dwLength = sizeof(status);
  GlobalMemoryStatusEx(&status);
  constexpr double kMiB = 1024.0 * 1024.0;
  return std::format("total {:.0f} MB / available {:.0f} MB",
                     status.ullTotalPhys / kMiB, status.ullAvailPhys / kMiB);
}

std::string DescribeProcess() {
  PROCESS_MEMORY_COUNTERS_EX counters = {};
  counters.cb = sizeof(counters);
  K32GetProcessMemoryInfo(GetCurrentProcess(),
                          reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                          sizeof(counters));
  FILETIME created = {}, exited = {}, kernel = {}, user = {}, now = {};
  GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
  GetSystemTimeAsFileTime(&now);
  auto toTicks = [](const FILETIME &t) {
    return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
  };
  const double uptimeSeconds =
      static_cast<double>(toTicks(now) - toTicks(created)) / 10'000'000.0;
  constexpr double kMiB = 1024.0 * 1024.0;
  return std::format("uptime {:.1f} s / working set {:.0f} MB / private {:.0f} MB",
                     uptimeSeconds, counters.WorkingSetSize / kMiB,
                     counters.PrivateUsage / kMiB);
}

std::string DescribeLanguage() {
  wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
  GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
  wchar_t uiLanguage[LOCALE_NAME_MAX_LENGTH] = {};
  ULONG count = 0;
  ULONG size = LOCALE_NAME_MAX_LENGTH;
  GetUserPreferredUILanguages(MUI_LANGUAGE_NAME, &count, uiLanguage, &size);
  return std::format("UI {} / locale {} / ANSI code page {}",
                     WideToUtf8(uiLanguage), WideToUtf8(locale), GetACP());
}

// GPU は名前・ベンダー/デバイスID・ドライバーの版・メモリ量だけを書く
// （LUID やサブシステムIDなど、機器ごとに異なる識別子は書かない）
void DescribeAdapters(std::ostringstream &out) {
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    out << "GPU: (DXGI unavailable)\n";
    return;
  }
  for (UINT index = 0;; ++index) {
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
    DXGI_ADAPTER_DESC1 desc = {};
    if (FAILED(adapter->GetDesc1(&desc))) continue;
    if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;

    std::string driver = "unknown";
    LARGE_INTEGER umdVersion = {};
    if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),
                                                 &umdVersion))) {
      driver = std::format("{}.{}.{}.{}", HIWORD(umdVersion.HighPart),
                           LOWORD(umdVersion.HighPart),
                           HIWORD(umdVersion.LowPart), LOWORD(umdVersion.LowPart));
    }
    constexpr double kMiB = 1024.0 * 1024.0;
    out << std::format(
        "GPU {}: {} (vendor 0x{:04X} device 0x{:04X}) driver {} / VRAM {:.0f} MB / "
        "shared {:.0f} MB\n",
        index, WideToUtf8(desc.Description), desc.VendorId, desc.DeviceId, driver,
        desc.DedicatedVideoMemory / kMiB, desc.SharedSystemMemory / kMiB);

    for (UINT outputIndex = 0;; ++outputIndex) {
      Microsoft::WRL::ComPtr<IDXGIOutput> output;
      if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;
      DXGI_OUTPUT_DESC outputDesc = {};
      if (FAILED(output->GetDesc(&outputDesc))) continue;
      DEVMODEW mode = {};
      mode.dmSize = sizeof(mode);
      const bool hasMode = EnumDisplaySettingsW(outputDesc.DeviceName,
                                                ENUM_CURRENT_SETTINGS, &mode);
      const RECT &rect = outputDesc.DesktopCoordinates;
      out << std::format("  display {}: {}x{}{}\n", outputIndex,
                         rect.right - rect.left, rect.bottom - rect.top,
                         hasMode ? std::format(" @ {} Hz", mode.dmDisplayFrequency)
                                 : std::string());
    }
  }
}

} // namespace

std::string CollectSystemDiagnostics() {
  std::ostringstream out;
  const char *edition = kTrialBuild ? "Trial" : "Full";
#ifdef _DEBUG
  const char *configuration = "Debug";
#else
  const char *configuration = "Release";
#endif
  out << std::format("Game: WikiGOLF {} {} / build {} ({} {})\n", edition,
                     configuration, WIKIGOLF_BUILD_COMMIT, __DATE__, __TIME__);
  out << "OS: " << DescribeOs() << '\n';
  out << "Language: " << DescribeLanguage() << '\n';
  out << "CPU: " << DescribeCpu() << '\n';
  out << "Memory: " << DescribeMemory() << '\n';
  out << "Process: " << DescribeProcess() << '\n';
  DescribeAdapters(out);

  std::lock_guard<std::mutex> lock(SectionsMutex());
  for (const auto &[name, text] : Sections()) {
    out << name << ": " << text << '\n';
  }
  return out.str();
}

void SetDiagnosticsSection(const std::string &name, const std::string &text) {
  std::lock_guard<std::mutex> lock(SectionsMutex());
  Sections()[name] = text;
}

} // namespace core
