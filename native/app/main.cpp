#include "app/command_line.h"
#include "app/main_window.h"
#include "common/native_log.h"

#include <Ole2.h>
#include <Shellapi.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  NativeLogInfo("application.start");
  std::optional<std::wstring> launch_path;
  int count = 0;
  auto* args = CommandLineToArgvW(GetCommandLineW(), &count);
  if (args && count > 1) {
    launch_path = ResolveLaunchPdfPath(args[1]);
    if (!launch_path) {
      NativeLogError("application.launch_file.invalid");
      MessageBoxW(nullptr, L"无法打开指定文件。\n\n请确认文件存在，并且扩展名为 .pdf。", L"lw.PDF", MB_OK | MB_ICONERROR);
      LocalFree(args); return 1;
    }
  }
  if (args) LocalFree(args);
  const auto initialized = OleInitialize(nullptr);
  if (FAILED(initialized)) NativeLogError("application.ole_initialize.failed");
  const int result = RunMainWindow(instance, launch_path);
  if (SUCCEEDED(initialized)) OleUninitialize();
  NativeLogInfo("application.stop result=" + std::to_string(result));
  return result;
}
