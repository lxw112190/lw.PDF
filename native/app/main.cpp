#include "app/command_line.h"
#include "app/main_window.h"
#include "common/diagnostic_console.h"
#include "common/native_log.h"

#include <Ole2.h>
#include <Shellapi.h>

#include <string>
#include <vector>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  int count = 0;
  auto* args = CommandLineToArgvW(GetCommandLineW(), &count);
  std::vector<std::wstring> arguments;
  if (args) {
    arguments.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) arguments.emplace_back(args[index]);
  }
  AppLaunchParseResult launch;
  if (args) {
    launch = ParseLaunchOptions(arguments);
  } else {
    launch.success = false;
    launch.error = L"无法解析启动参数。";
  }
  const bool console_enabled = launch.options.console && EnableDiagnosticConsole();
  NativeLogInfo("application.start");
  if (launch.options.console && !console_enabled) {
    NativeLogError("diagnostic.console.enable_failed");
    MessageBoxW(nullptr, L"无法打开诊断控制台。\n\n请直接查看日志文件或使用 OutputDebugString 工具。",
                L"lw.PDF", MB_OK | MB_ICONWARNING);
  }
  if (args) LocalFree(args);
  if (!launch.success) {
    NativeLogError("application.command_line.invalid");
    MessageBoxW(nullptr, launch.error.c_str(), L"lw.PDF", MB_OK | MB_ICONERROR);
    NativeLogInfo("application.stop result=1");
    return 1;
  }
  const auto initialized = OleInitialize(nullptr);
  if (SUCCEEDED(initialized)) NativeLogInfo("startup.ole.ok");
  else NativeLogError("startup.ole.failed hresult=" + FormatHresult(initialized));
  const int result = RunMainWindow(instance, launch.options.pdf_path);
  if (SUCCEEDED(initialized)) OleUninitialize();
  NativeLogInfo("application.stop result=" + std::to_string(result));
  return result;
}
