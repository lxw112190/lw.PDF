#include "pdf/pdf_page_editor.h"

#include "common/utf8.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QUtil.hh>

#include <Windows.h>
#include <filesystem>
#include <system_error>
#include <vector>

namespace {

bool SameFilePath(const std::wstring& left, const std::wstring& right) {
  if (QUtil::same_file(WideToUtf8(left).c_str(), WideToUtf8(right).c_str())) return true;
  std::error_code error;
  const auto left_absolute = std::filesystem::absolute(left, error).lexically_normal();
  if (error) return false;
  const auto right_absolute = std::filesystem::absolute(right, error).lexically_normal();
  return !error && _wcsicmp(left_absolute.c_str(), right_absolute.c_str()) == 0;
}

int RotationDegrees(std::uint16_t rotation) {
  switch (rotation) {
    case 0: return 0;
    case 90: return 90;
    case 180: return 180;
    case 270: return -90;
    default: return 0;
  }
}

void RemoveTemporary(const std::filesystem::path& path) {
  std::error_code error;
  std::filesystem::remove(path, error);
}

}  // namespace

bool ValidatePagePlan(const PdfPageEditRequest& request,
                      std::uint32_t page_count) {
  if (page_count == 0 || page_count > 100000U || request.pages.size() != page_count) {
    return false;
  }
  std::vector<bool> seen(page_count + 1U, false);
  for (const auto& item : request.pages) {
    if (item.source_page < 1 || item.source_page > page_count || seen[item.source_page] ||
        (item.rotation != 0 && item.rotation != 90 && item.rotation != 180 && item.rotation != 270)) {
      return false;
    }
    seen[item.source_page] = true;
  }
  return true;
}

PdfPageEditResult EditPdfPages(const std::wstring& input_path,
                               const std::wstring& output_path,
                               const PdfPageEditRequest& request) {
  PdfPageEditResult result;
  if (input_path.empty() || output_path.empty()) {
    result.error = PdfPageEditError::Failed;
    return result;
  }
  if (SameFilePath(input_path, output_path)) {
    result.error = PdfPageEditError::SameFile;
    return result;
  }
  std::error_code path_error;
  if (!std::filesystem::is_regular_file(input_path, path_error) || path_error ||
      std::filesystem::is_directory(output_path, path_error)) {
    result.error = path_error ? PdfPageEditError::SourceUnavailable
                              : PdfPageEditError::OutputWriteFailed;
    return result;
  }

  try {
    QPDF pdf;
    pdf.processFile(WideToUtf8(input_path).c_str(), "");
    (void)pdf.getRoot();
    if (!pdf.allowModifyAssembly()) {
      result.error = PdfPageEditError::PermissionDenied;
      return result;
    }
    QPDFPageDocumentHelper helper(pdf);
    const auto original_pages = helper.getAllPages();
    if (original_pages.size() > 100000U) {
      result.error = PdfPageEditError::InvalidPlan;
      return result;
    }
    result.page_count = static_cast<std::uint32_t>(original_pages.size());
    if (!ValidatePagePlan(request, result.page_count)) {
      result.error = PdfPageEditError::InvalidPlan;
      return result;
    }
    for (const auto& page : original_pages) helper.removePage(page);
    for (const auto& item : request.pages) {
      auto page = original_pages[item.source_page - 1U];
      const auto degrees = RotationDegrees(item.rotation);
      if (degrees != 0) page.rotatePage(degrees, true);
      helper.addPage(page, false);
    }

    const auto output = std::filesystem::path(output_path);
    const auto parent = output.parent_path().empty()
        ? std::filesystem::current_path() : output.parent_path();
    wchar_t temporary_name[MAX_PATH]{};
    if (GetTempFileNameW(parent.c_str(), L"lwp", 0, temporary_name) == 0) {
      result.error = PdfPageEditError::OutputWriteFailed;
      return result;
    }
    const std::filesystem::path temporary(temporary_name);
    DeleteFileW(temporary.c_str());
    try {
      QPDFWriter writer(pdf);
      writer.setOutputFilename(WideToUtf8(temporary.wstring()).c_str());
      writer.write();
      if (!MoveFileExW(temporary.c_str(), output.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        result.error = PdfPageEditError::OutputWriteFailed;
        RemoveTemporary(temporary);
        return result;
      }
    } catch (...) {
      result.error = PdfPageEditError::OutputWriteFailed;
      RemoveTemporary(temporary);
      return result;
    }
    result.error = PdfPageEditError::None;
    return result;
  } catch (const QPDFExc& exception) {
    result.error = exception.getErrorCode() == qpdf_error_code_e::qpdf_e_password
        ? PdfPageEditError::PasswordRequired : PdfPageEditError::Failed;
    return result;
  } catch (...) {
    result.error = PdfPageEditError::Failed;
    return result;
  }
}
