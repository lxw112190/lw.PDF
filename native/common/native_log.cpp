#include "common/native_log.h"

#include "common/diagnostic_console.h"

#include <ShlObj.h>
#include <Windows.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace {
constexpr std::uintmax_t kMaximumLogSize = 2U * 1024U * 1024U;

std::string Clean(std::string_view message) {
  std::string result(message);
  for (auto& character : result) {
    if (character == '\r' || character == '\n' || character == '\t') {
      character = ' ';
    }
  }
  return result;
}

std::filesystem::path ResolveLogPath() {
  PWSTR local_app_data = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE,
                                  nullptr, &local_app_data)) ||
      !local_app_data) {
    return {};
  }
  const auto folder = std::filesystem::path(local_app_data) / L"lw.PDF" / L"logs";
  CoTaskMemFree(local_app_data);
  std::error_code error;
  std::filesystem::create_directories(folder, error);
  return error ? std::filesystem::path{} : folder / L"lw.PDF.log";
}

class NativeLogger final {
 public:
  void Write(std::string_view level, std::string_view message) noexcept {
    try {
      std::lock_guard lock(mutex_);
      SYSTEMTIME now{};
      GetLocalTime(&now);
      char timestamp[32]{};
      _snprintf_s(timestamp, sizeof(timestamp), _TRUNCATE,
                  "%04hu-%02hu-%02hu %02hu:%02hu:%02hu.%03hu",
                  now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                  now.wSecond, now.wMilliseconds);

      const auto line = std::string(timestamp) + " [" + std::string(level) +
          "] " + Clean(message);
      const auto debug_line = line + "\r\n";
      OutputDebugStringA(debug_line.c_str());
      WriteDiagnosticConsole(line);

      if (path_.empty()) path_ = ResolveLogPath();
      if (!path_.empty()) {
        RotateIfNeeded();
        std::ofstream output(path_, std::ios::binary | std::ios::app);
        if (output) output << line << "\r\n";
      }
    } catch (...) {
      // Logging must never affect the application operation being diagnosed.
    }
  }

 private:
  void RotateIfNeeded() {
    std::error_code error;
    if (!std::filesystem::exists(path_, error) || error ||
        std::filesystem::file_size(path_, error) < kMaximumLogSize || error) {
      return;
    }
    const auto previous = path_.parent_path() / L"lw.PDF.previous.log";
    std::filesystem::remove(previous, error);
    error.clear();
    std::filesystem::rename(path_, previous, error);
  }

  std::mutex mutex_;
  std::filesystem::path path_;
};

NativeLogger& Logger() {
  static NativeLogger logger;
  return logger;
}
}  // namespace

std::string FormatHresult(const std::int32_t value) noexcept {
  try {
    char buffer[11]{};
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "0x%08X",
                static_cast<std::uint32_t>(value));
    return buffer;
  } catch (...) {
    return "0x00000000";
  }
}

std::string FormatWin32Error(const std::uint32_t value) noexcept {
  try {
    char buffer[11]{};
    _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "0x%08X", value);
    return buffer;
  } catch (...) {
    return "0x00000000";
  }
}

void NativeLogInfo(const std::string_view message) noexcept {
  Logger().Write("INFO", message);
}

void NativeLogError(const std::string_view message) noexcept {
  Logger().Write("ERROR", message);
}

void NativeLogDebug(const std::string_view message) noexcept {
#ifdef _DEBUG
  Logger().Write("DEBUG", message);
#else
  (void)message;
#endif
}
