#pragma once

#include <optional>
#include <string>

std::optional<std::wstring> ResolveLaunchPdfPath(
    const std::wstring& argument);
