#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class PdfPageEditError {
  None = 0,
  SourceUnavailable,
  InvalidPlan,
  PasswordRequired,
  PermissionDenied,
  OutputWriteFailed,
  SameFile,
  Failed,
};

struct PdfPageEditItem {
  std::uint32_t source_page = 0;
  std::uint16_t rotation = 0;
};

struct PdfPageEditRequest {
  std::vector<PdfPageEditItem> pages;
};

struct PdfPageEditResult {
  PdfPageEditError error = PdfPageEditError::None;
  std::uint32_t page_count = 0;
};

bool ValidatePagePlan(const PdfPageEditRequest& request,
                      std::uint32_t page_count);

PdfPageEditResult EditPdfPages(const std::wstring& input_path,
                               const std::wstring& output_path,
                               const PdfPageEditRequest& request);
