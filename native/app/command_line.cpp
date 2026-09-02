#include "app/command_line.h"

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <system_error>
#include <utility>

namespace {
bool IsPdfExtension(const std::filesystem::path& path) {
  const auto extension = path.extension().wstring();
  return _wcsicmp(extension.c_str(), L".pdf") == 0;
}
}  // namespace

AppLaunchParseResult ParseLaunchOptions(
    const std::vector<std::wstring>& arguments) {
  AppLaunchParseResult result;
  result.success = true;
  if (arguments.empty()) return result;

  for (std::size_t index = 1; index < arguments.size(); ++index) {
    const auto& argument = arguments[index];
    if (argument == L"--console") {
      result.console_requested = true;
      result.options.console = true;
      continue;
    }
    if (argument.rfind(L"--", 0) == 0) {
      result.success = false;
      result.error = L"未知启动参数：" + argument;
      return result;
    }
    if (result.options.pdf_path) {
      result.success = false;
      result.error = L"只能同时打开一个 PDF 文件。";
      return result;
    }
    const auto path = ResolveLaunchPdfPath(argument);
    if (!path) {
      result.success = false;
      result.error = L"无法打开指定文件。请确认文件存在，并且扩展名为 .pdf。";
      return result;
    }
    result.options.pdf_path = path;
  }
  return result;
}

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
