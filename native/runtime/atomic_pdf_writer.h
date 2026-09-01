#pragma once

#include <Windows.h>
#include <objidl.h>

#include <cstdint>
#include <filesystem>
#include <string>

struct PdfAtomicWriteResult {
  bool success = false;
  std::uintmax_t bytes_written = 0;
  std::string error;
};

// Copies a PDF request stream to a temporary file and commits it with one
// replace operation. The destination is never touched until every write,
// flush, close, and basic PDF validation step has succeeded.
PdfAtomicWriteResult WritePdfStreamAtomically(
    IStream* input, const std::filesystem::path& temporary,
    const std::filesystem::path& output);
