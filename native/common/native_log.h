#pragma once

#include <string_view>

void NativeLogInfo(std::string_view message) noexcept;
void NativeLogError(std::string_view message) noexcept;
void NativeLogDebug(std::string_view message) noexcept;

