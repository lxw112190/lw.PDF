#pragma once

#include <Windows.h>

#include <optional>
#include <string>

std::optional<std::wstring> ChoosePdfFile(HWND owner);

// Shows a Save As dialog for a new PDF. Returns nullopt when cancelled.
// The path never leaves the native process.
std::optional<std::wstring> ChoosePdfSavePath(HWND owner,
                                              const std::wstring& suggested_name);
