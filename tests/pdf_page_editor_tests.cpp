#include "common/utf8.h"
#include "pdf/pdf_page_editor.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <Windows.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;
void Check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << "\n";
  ++failures;
}

QPDFObjectHandle NewPage(QPDF& pdf, int width) {
  auto page = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
  page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
  page.replaceKey("/MediaBox", QPDFObjectHandle::parse(
      "[0 0 " + std::to_string(width) + " " + std::to_string(width) + "]"));
  page.replaceKey("/Resources", QPDFObjectHandle::newDictionary());
  page.replaceKey("/Contents", pdf.makeIndirectObject(
      QPDFObjectHandle::newStream(&pdf, "0 0 1 1 re S\n")));
  return page;
}

std::filesystem::path TempRoot() {
  auto root = std::filesystem::temp_directory_path() /
      ("lw-pdf-page-editor-test-" + std::to_string(GetCurrentProcessId()));
  std::filesystem::create_directories(root);
  return root;
}

void CreatePdf(const std::filesystem::path& path) {
  QPDF pdf;
  pdf.emptyPDF();
  QPDFPageDocumentHelper helper(pdf);
  for (int width : {100, 200, 300}) helper.addPage(QPDFPageObjectHelper(NewPage(pdf, width)), false);
  QPDFWriter writer(pdf);
  writer.setOutputFilename(WideToUtf8(path.wstring()).c_str());
  writer.write();
}

void CreateAssemblyRestrictedPdf(const std::filesystem::path& path) {
  QPDF pdf;
  pdf.emptyPDF();
  QPDFPageDocumentHelper helper(pdf);
  helper.addPage(QPDFPageObjectHelper(NewPage(pdf, 100)), false);
  QPDFWriter writer(pdf);
  writer.setOutputFilename(WideToUtf8(path.wstring()).c_str());
  writer.setR6EncryptionParameters("", "owner", true, true, false, true, true,
                                   true, qpdf_r3p_full, true);
  writer.write();
}

std::vector<int> Widths(const std::filesystem::path& path) {
  QPDF pdf;
  pdf.processFile(WideToUtf8(path.wstring()).c_str());
  std::vector<int> result;
  for (auto page : QPDFPageDocumentHelper(pdf).getAllPages()) {
    result.push_back(page.getMediaBox().getArrayItem(2).getIntValueAsInt());
  }
  return result;
}

int Rotation(const std::filesystem::path& path, std::size_t index) {
  QPDF pdf;
  pdf.processFile(WideToUtf8(path.wstring()).c_str());
  auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
  const auto rotate = pages[index].getObjectHandle().getKey("/Rotate");
  return rotate.isNull() ? 0 : rotate.getIntValueAsInt();
}
}  // namespace

int main() {
  const auto root = TempRoot();
  const auto input = root / "input.pdf";
  const auto output = root / "organized.pdf";
  CreatePdf(input);

  Check(ValidatePagePlan(PdfPageEditRequest{{{3, 90}, {1, 0}, {2, 270}}}, 3),
        "accepts a complete permutation and valid rotations");
  Check(!ValidatePagePlan(PdfPageEditRequest{{{1, 0}, {1, 0}, {3, 0}}}, 3),
        "rejects duplicate source pages");
  Check(!ValidatePagePlan(PdfPageEditRequest{{{1, 45}, {2, 0}, {3, 0}}}, 3),
        "rejects unsupported rotations");
  Check(!ValidatePagePlan(PdfPageEditRequest{{{1, 0}, {2, 0}}}, 3),
        "rejects a plan with the wrong page count");

  const auto result = EditPdfPages(input.wstring(), output.wstring(),
                                   PdfPageEditRequest{{{3, 90}, {1, 0}, {2, 270}}});
  Check(result.error == PdfPageEditError::None && result.page_count == 3,
        "writes a valid reordered page plan");
  Check(Widths(output) == std::vector<int>({300, 100, 200}),
        "preserves the requested page order");
  Check(Rotation(output, 0) == 90 && Rotation(output, 1) == 0 &&
            Rotation(output, 2) == 270,
        "preserves the requested per-page rotations");
  Check(Widths(input) == std::vector<int>({100, 200, 300}),
        "does not modify the source PDF");

  const auto invalid_output = root / "invalid.pdf";
  const auto invalid = EditPdfPages(input.wstring(), invalid_output.wstring(),
                                    PdfPageEditRequest{{{1, 0}, {1, 0}, {3, 0}}});
  Check(invalid.error == PdfPageEditError::InvalidPlan &&
            !std::filesystem::exists(invalid_output),
        "rejects invalid plans before creating output");
  const auto restricted = root / "restricted.pdf";
  const auto restricted_output = root / "restricted-out.pdf";
  CreateAssemblyRestrictedPdf(restricted);
  const auto denied = EditPdfPages(
      restricted.wstring(), restricted_output.wstring(),
      PdfPageEditRequest{{{1, 0}}});
  Check(denied.error == PdfPageEditError::PermissionDenied &&
            !std::filesystem::exists(restricted_output),
        "rejects a PDF whose permissions forbid page assembly");
  std::error_code ignored;
  std::filesystem::remove_all(root, ignored);
  return failures == 0 ? 0 : 1;
}
