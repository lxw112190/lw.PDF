#include "common/diagnostic_console.h"

#include <Windows.h>

#include <mutex>

namespace {
std::mutex& ConsoleMutex() {
  static std::mutex mutex;
  return mutex;
}

HANDLE& ConsoleOutput() {
  static HANDLE output = INVALID_HANDLE_VALUE;
  return output;
}

bool& ConsoleEnabled() {
  static bool enabled = false;
  return enabled;
}
}  // namespace

bool EnableDiagnosticConsole() noexcept {
  try {
    std::lock_guard lock(ConsoleMutex());
    if (ConsoleEnabled()) return true;

    if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
      const auto error = GetLastError();
      if (error != ERROR_ACCESS_DENIED && !AllocConsole()) return false;
    }

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    auto output = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (output == INVALID_HANDLE_VALUE) return false;
    ConsoleOutput() = output;
    ConsoleEnabled() = true;
    return true;
  } catch (...) {
    return false;
  }
}

bool IsDiagnosticConsoleEnabled() noexcept {
  std::lock_guard lock(ConsoleMutex());
  return ConsoleEnabled();
}

void WriteDiagnosticConsole(const std::string_view line) noexcept {
  try {
    std::lock_guard lock(ConsoleMutex());
    if (!ConsoleEnabled() || ConsoleOutput() == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(ConsoleOutput(), line.data(), static_cast<DWORD>(line.size()),
              &written, nullptr);
    WriteFile(ConsoleOutput(), "\r\n", 2, &written, nullptr);
  } catch (...) {
    // Diagnostics must never affect normal application operation.
  }
}
