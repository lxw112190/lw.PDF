#pragma once

#include <Windows.h>

#include <optional>
#include <string>

std::optional<std::wstring> ChoosePdfFile(HWND owner);
