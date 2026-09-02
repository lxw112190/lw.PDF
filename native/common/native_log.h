#pragma once

#include <cstdint>
#include <string>
#include <string_view>

void NativeLogInfo(std::string_view message) noexcept;
void NativeLogError(std::string_view message) noexcept;
void NativeLogDebug(std::string_view message) noexcept;
std::string FormatHresult(std::int32_t value) noexcept;
std::string FormatWin32Error(std::uint32_t value) noexcept;
