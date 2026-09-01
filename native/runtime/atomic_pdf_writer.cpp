#include "runtime/atomic_pdf_writer.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>

namespace {
constexpr std::uintmax_t kMaximumPdfBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;

void RemoveTemporary(const std::filesystem::path& temporary) {
  std::error_code error;
  std::filesystem::remove(temporary, error);
}
}  // namespace

PdfAtomicWriteResult WritePdfStreamAtomically(
    IStream* input, const std::filesystem::path& temporary,
    const std::filesystem::path& output) {
  PdfAtomicWriteResult result;
  if (!input) {
    result.error = "request_body_unavailable";
    return result;
  }
  if (temporary.empty() || output.empty()) {
    result.error = "output_path_invalid";
    return result;
  }

  std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    result.error = "output_open_failed";
    RemoveTemporary(temporary);
    return result;
  }

  std::array<char, 64 * 1024> buffer{};
  std::array<char, 5> header{};
  std::size_t header_bytes = 0;
  bool failed = false;
  while (!failed) {
    ULONG read = 0;
    const HRESULT read_result = input->Read(
        buffer.data(), static_cast<ULONG>(buffer.size()), &read);
    if (FAILED(read_result)) {
      result.error = "request_read_failed";
      failed = true;
      break;
    }
    if (read == 0) break;

    const auto remaining_header = header.size() - header_bytes;
    const auto header_copy =
        (std::min<std::size_t>(remaining_header, read));
    if (header_copy != 0) {
      std::copy_n(buffer.data(), header_copy, header.data() + header_bytes);
      header_bytes += header_copy;
    }
    if (result.bytes_written > kMaximumPdfBytes - read) {
      result.error = "pdf_too_large";
      failed = true;
      break;
    }
    file.write(buffer.data(), static_cast<std::streamsize>(read));
    if (!file) {
      result.error = "output_write_failed";
      failed = true;
      break;
    }
    result.bytes_written += read;
  }

  file.flush();
  if (!file && result.error.empty()) result.error = "output_flush_failed";
  file.close();
  if (file.fail() && result.error.empty()) result.error = "output_close_failed";

  const bool valid_header =
      header_bytes == header.size() &&
      std::equal(header.begin(), header.end(), std::array<char, 5>{'%', 'P', 'D', 'F', '-'}.begin());
  if (result.error.empty() && (result.bytes_written == 0 || !valid_header)) {
    result.error = "invalid_pdf_body";
  }
  if (!result.error.empty()) {
    RemoveTemporary(temporary);
    return result;
  }
  if (!MoveFileExW(temporary.c_str(), output.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    result.error = "output_commit_failed";
    RemoveTemporary(temporary);
    return result;
  }
  result.success = true;
  return result;
}
