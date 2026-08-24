#include "app/command_line.h"

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <system_error>

namespace {
bool IsPdfExtension(const std::filesystem::path& path) {
  const auto extension = path.extension().wstring();
  return _wcsicmp(extension.c_str(), L".pdf") == 0;
}
}  // namespace

std::optional<std::wstring> ResolveLaunchPdfPath(
    const std::wstring& argument) {
  if (argument.empty() || argument.size() > 32767U) return std::nullopt;

  std::error_code error;
  auto path = std::filesystem::path(argument);
  if (!path.is_absolute()) path = std::filesystem::absolute(path, error);
  if (error || !IsPdfExtension(path) ||
      !std::filesystem::is_regular_file(path, error) || error) {
    return std::nullopt;
  }
  return path.lexically_normal().wstring();
}
