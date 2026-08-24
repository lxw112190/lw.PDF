#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

struct HttpByteRange {
  std::uintmax_t first = 0;
  std::uintmax_t last = 0;
  bool partial = false;

  std::uintmax_t Length() const { return last - first + 1U; }
};

std::optional<HttpByteRange> ParseHttpByteRange(std::wstring_view header,
                                                std::uintmax_t size);
std::wstring BuildPdfRangeResponseHeaders(const HttpByteRange& range,
                                          std::uintmax_t total_size);
