#include "runtime/http_range.h"

#include <algorithm>
#include <limits>

namespace {
bool StartsWithBytes(std::wstring_view value) {
  constexpr std::wstring_view prefix = L"bytes=";
  if (value.size() < prefix.size()) return false;
  for (std::size_t index = 0; index < prefix.size(); ++index) {
    wchar_t character = value[index];
    if (character >= L'A' && character <= L'Z') character += L'a' - L'A';
    if (character != prefix[index]) return false;
  }
  return true;
}

bool ParseUnsigned(std::wstring_view value, std::uintmax_t& output) {
  if (value.empty()) return false;
  output = 0;
  for (const auto character : value) {
    if (character < L'0' || character > L'9') return false;
    const auto digit = static_cast<std::uintmax_t>(character - L'0');
    if (output > (std::numeric_limits<std::uintmax_t>::max() - digit) / 10U) {
      return false;
    }
    output = output * 10U + digit;
  }
  return true;
}
}  // namespace

std::optional<HttpByteRange> ParseHttpByteRange(const std::wstring_view header,
                                                const std::uintmax_t size) {
  if (size == 0) return std::nullopt;
  if (header.empty()) return HttpByteRange{0, size - 1U, false};
  if (!StartsWithBytes(header) || header.find(L',') != std::wstring_view::npos) {
    return std::nullopt;
  }

  constexpr std::size_t prefix_size = 6U;
  const auto dash = header.find(L'-', prefix_size);
  if (dash == std::wstring_view::npos) return std::nullopt;
  const auto left = header.substr(prefix_size, dash - prefix_size);
  const auto right = header.substr(dash + 1U);
  if (left.empty()) {
    std::uintmax_t suffix = 0;
    if (!ParseUnsigned(right, suffix) || suffix == 0) return std::nullopt;
    const auto first = suffix >= size ? 0 : size - suffix;
    return HttpByteRange{first, size - 1U, true};
  }

  std::uintmax_t first = 0;
  if (!ParseUnsigned(left, first) || first >= size) return std::nullopt;
  if (right.empty()) return HttpByteRange{first, size - 1U, true};

  std::uintmax_t last = 0;
  if (!ParseUnsigned(right, last) || first > last) return std::nullopt;
  return HttpByteRange{first, std::min(last, size - 1U), true};
}

std::wstring BuildPdfRangeResponseHeaders(const HttpByteRange& range,
                                          const std::uintmax_t total_size) {
  std::wstring headers =
      L"Content-Type: application/pdf\r\n"
      L"Accept-Ranges: bytes\r\n"
      L"Access-Control-Allow-Origin: https://app.lwpdf\r\n"
      L"Access-Control-Expose-Headers: Accept-Ranges, Content-Length, Content-Range\r\n"
      L"Content-Length: " + std::to_wstring(range.Length());
  if (range.partial) {
    headers += L"\r\nContent-Range: bytes " + std::to_wstring(range.first) +
               L"-" + std::to_wstring(range.last) + L"/" +
               std::to_wstring(total_size);
  }
  return headers;
}
