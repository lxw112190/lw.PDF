#pragma once

#include <optional>
#include <string>
#include <vector>

struct AppLaunchOptions {
  bool console = false;
  std::optional<std::wstring> pdf_path;
};

struct AppLaunchParseResult {
  bool success = false;
  bool console_requested = false;
  AppLaunchOptions options;
  std::wstring error;
};

// Arguments include argv[0]. Keeping parsing independent of WinMain makes it
// straightforward to test without starting a process or opening a console.
AppLaunchParseResult ParseLaunchOptions(
    const std::vector<std::wstring>& arguments);

std::optional<std::wstring> ResolveLaunchPdfPath(
    const std::wstring& argument);
