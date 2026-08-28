#include "pdf/pdf_transformer.h"

#include "common/utf8.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QUtil.hh>

#include <Windows.h>
#include <filesystem>
#include <optional>
#include <set>
#include <vector>

namespace {

bool SameFilePath(const std::wstring& left, const std::wstring& right) {
  const auto left_utf8 = WideToUtf8(left);
  const auto right_utf8 = WideToUtf8(right);
  // qpdf's file identity check uses volume/file IDs on Windows, so it also
  // catches hard links and aliases that lexical path comparison misses.
  if (QUtil::same_file(left_utf8.c_str(), right_utf8.c_str())) return true;
  std::error_code error;
  const auto left_absolute = std::filesystem::absolute(left, error).lexically_normal();
  const auto right_absolute = std::filesystem::absolute(right, error).lexically_normal();
  return _wcsicmp(left_absolute.c_str(), right_absolute.c_str()) == 0;
}

// Accepts only simple page lists like "1", "1,3,5", "1-5", "1,3,5-8".
// Whitespace around numbers is tolerated; qpdf advanced syntax is not.
std::optional<std::set<std::uint32_t>> ParsePageRange(const std::string& range,
                                                      std::uint32_t page_count) {
  if (range.empty()) return std::nullopt;
  std::set<std::uint32_t> pages;
  std::size_t offset = 0;
  while (true) {
    const auto comma = range.find(',', offset);
    const auto token = range.substr(offset, comma == std::string::npos
                                                ? std::string::npos
                                                : comma - offset);
    const auto begin = token.find_first_not_of(" \t");
    const auto end = token.find_last_not_of(" \t");
    if (begin == std::string::npos) return std::nullopt;
    const auto part = token.substr(begin, end - begin + 1U);
    const auto dash = part.find('-');
    const auto parse_number = [&part, page_count](std::size_t from,
                                      std::size_t to) -> std::optional<std::uint64_t> {
      if (from >= to) return std::nullopt;
      std::uint64_t value = 0;
      for (std::size_t i = from; i < to; ++i) {
        if (part[i] < '0' || part[i] > '9') return std::nullopt;
        value = value * 10U + static_cast<std::uint64_t>(part[i] - '0');
        if (value > page_count) return std::nullopt;
      }
      if (value < 1) return std::nullopt;
      return value;
    };
    std::optional<std::uint64_t> first;
    std::optional<std::uint64_t> last;
    if (dash == std::string::npos) {
      first = parse_number(0, part.size());
      last = first;
    } else {
      first = parse_number(0, dash);
      last = parse_number(dash + 1U, part.size());
      if (first && last && *last < *first) return std::nullopt;
    }
    if (!first || !last) return std::nullopt;
    for (auto page = *first; page <= *last; ++page) {
      pages.insert(static_cast<std::uint32_t>(page));
    }
    if (comma == std::string::npos) break;
    offset = comma + 1U;
  }
  if (pages.empty()) return std::nullopt;
  return pages;
}

int RotationDegrees(PdfRotation rotation) {
  switch (rotation) {
    case PdfRotation::Left90: return -90;
    case PdfRotation::Right90: return 90;
    case PdfRotation::Rotate180: return 180;
  }
  return 0;
}

void ReversePageOrder(QPDF& pdf) {
  QPDFPageDocumentHelper helper(pdf);
  const auto pages = helper.getAllPages();
  // Detach every page, then re-attach them in reverse. Page objects keep
  // their identity, so outline/link destinations keep following content.
  for (const auto& page : pages) helper.removePage(page);
  for (auto it = pages.rbegin(); it != pages.rend(); ++it) {
    helper.addPage(*it, false);
  }
}

// Returns the selected 1-based page numbers, or nullopt for "all pages".
std::optional<std::set<std::uint32_t>> SelectedPages(
    const PdfPageSelection& selection, std::uint32_t page_count) {
  switch (selection.kind) {
    case PdfPageSelectionKind::All:
      return std::nullopt;
    case PdfPageSelectionKind::Single: {
      if (selection.page < 1 || selection.page > page_count) return std::nullopt;
      std::set<std::uint32_t> selected;
      selected.insert(selection.page);
      return selected;
    }
    case PdfPageSelectionKind::Range:
      return ParsePageRange(selection.range, page_count);
  }
  return std::nullopt;
}

void RotatePages(QPDF& pdf, PdfRotation rotation, const PdfPageSelection& selection,
                 std::uint32_t page_count) {
  QPDFPageDocumentHelper helper(pdf);
  auto pages = helper.getAllPages();
  const auto selected = SelectedPages(selection, page_count);
  const auto angle = RotationDegrees(rotation);
  for (std::size_t index = 0; index < pages.size(); ++index) {
    if (selected && !selected->count(static_cast<std::uint32_t>(index + 1U))) continue;
    // Relative rotation stacks with any existing /Rotate entry.
    pages[index].rotatePage(angle, true);
  }
}

}  // namespace

PdfTransformResult TransformPdf(const std::wstring& input_path,
                                const std::wstring& output_path,
                                const PdfTransformRequest& request) {
  PdfTransformResult result;
  if (input_path.empty() || output_path.empty()) {
    result.error = PdfTransformError::InvalidParams;
    return result;
  }
  if (request.kind != PdfTransformKind::ReversePages &&
      request.kind != PdfTransformKind::RotatePages) {
    result.error = PdfTransformError::InvalidParams;
    return result;
  }
  if (request.kind == PdfTransformKind::RotatePages &&
      (request.rotation < PdfRotation::Left90 || request.rotation > PdfRotation::Rotate180)) {
    result.error = PdfTransformError::InvalidParams;
    return result;
  }
  if (request.pages.kind != PdfPageSelectionKind::All &&
      request.pages.kind != PdfPageSelectionKind::Single &&
      request.pages.kind != PdfPageSelectionKind::Range) {
    result.error = PdfTransformError::InvalidParams;
    return result;
  }
  if (SameFilePath(input_path, output_path)) {
    result.error = PdfTransformError::SameFile;
    return result;
  }
  std::error_code error;
  if (!std::filesystem::is_regular_file(input_path, error) || error) {
    result.error = PdfTransformError::SourceUnavailable;
    return result;
  }
  if (std::filesystem::is_directory(output_path)) {
    result.error = PdfTransformError::OutputWriteFailed;
    return result;
  }

  try {
    QPDF pdf;
    pdf.processFile(WideToUtf8(input_path).c_str(), "");
    // Touch the root to force the decryption check for encrypted files.
    (void)pdf.getRoot();
    QPDFPageDocumentHelper helper(pdf);
    const auto page_count = static_cast<std::uint32_t>(helper.getAllPages().size());

    if (request.kind == PdfTransformKind::ReversePages) {
      ReversePageOrder(pdf);
    } else {
      if (request.pages.kind == PdfPageSelectionKind::Range &&
          !ParsePageRange(request.pages.range, page_count)) {
        result.error = PdfTransformError::PageRangeInvalid;
        return result;
      }
      if (request.pages.kind == PdfPageSelectionKind::Single &&
          (request.pages.page < 1 || request.pages.page > page_count)) {
        result.error = PdfTransformError::PageRangeInvalid;
        return result;
      }
      RotatePages(pdf, request.rotation, request.pages, page_count);
    }

    const auto output = std::filesystem::path(output_path);
    const auto parent = output.parent_path().empty()
        ? std::filesystem::current_path() : output.parent_path();
    wchar_t temporary_name[MAX_PATH]{};
    if (GetTempFileNameW(parent.c_str(), L"lwp", 0, temporary_name) == 0) {
      result.error = PdfTransformError::OutputWriteFailed;
      return result;
    }
    const std::filesystem::path temporary_path(temporary_name);
    DeleteFileW(temporary_path.c_str());
    bool committed = false;
    const auto cleanup = [&] {
      std::error_code ignored;
      std::filesystem::remove(temporary_path, ignored);
    };
    try {
      {
        QPDFWriter writer(pdf);
        writer.setOutputFilename(WideToUtf8(temporary_path.wstring()).c_str());
        writer.write();
      }
      if (!MoveFileExW(temporary_path.c_str(), output.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        cleanup();
        result.error = PdfTransformError::OutputWriteFailed;
        return result;
      }
      committed = true;
    } catch (...) {
      cleanup();
      result.error = PdfTransformError::OutputWriteFailed;
      return result;
    }
    result.page_count = page_count;
    result.error = PdfTransformError::None;
    return result;
  } catch (const QPDFExc& exception) {
    if (exception.getErrorCode() == qpdf_error_code_e::qpdf_e_password) {
      result.error = PdfTransformError::PasswordRequired;
    } else {
      result.error = PdfTransformError::Failed;
    }
    return result;
  } catch (...) {
    result.error = PdfTransformError::Failed;
    return result;
  }
}
