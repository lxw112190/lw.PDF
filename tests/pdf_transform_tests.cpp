#include "common/utf8.h"
#include "pdf/pdf_transformer.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <filesystem>
#include <fstream>
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

QPDFObjectHandle NewPage(QPDF& pdf, int width, int rotate) {
  QPDFObjectHandle page = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
  page.replaceKey("/Type", QPDFObjectHandle::newName("/Page"));
  page.replaceKey("/MediaBox",
      QPDFObjectHandle::parse("[0 0 " + std::to_string(width) + " " +
                              std::to_string(width) + "]"));
  page.replaceKey("/Resources", QPDFObjectHandle::newDictionary());
  page.replaceKey("/Contents",
      pdf.makeIndirectObject(QPDFObjectHandle::newStream(&pdf, "0 0 1 1 re S\n")));
  if (rotate != 0) page.replaceKey("/Rotate", QPDFObjectHandle::newInteger(rotate));
  return page;
}

// Creates a PDF whose pages are distinguishable by their MediaBox widths.
// Returns the page object handles in document order.
std::vector<QPDFObjectHandle> CreatePdf(const std::filesystem::path& path,
                                        const std::vector<int>& widths,
                                        const std::vector<int>& rotates = {}) {
  QPDF pdf;
  pdf.emptyPDF();
  QPDFPageDocumentHelper helper(pdf);
  std::vector<QPDFObjectHandle> pages;
  for (std::size_t index = 0; index < widths.size(); ++index) {
    const int rotate = rotates.size() > index ? rotates[index] : 0;
    const auto page = NewPage(pdf, widths[index], rotate);
    helper.addPage(QPDFPageObjectHelper(page), false);
    pages.push_back(page);
  }
  QPDFWriter writer(pdf);
  writer.setOutputFilename(WideToUtf8(path.wstring()).c_str());
  writer.write();
  return pages;
}

std::vector<int> PageWidths(const std::filesystem::path& path) {
  try {
    QPDF pdf;
    pdf.processFile(WideToUtf8(path.wstring()).c_str());
    std::vector<int> widths;
    for (auto& page : QPDFPageDocumentHelper(pdf).getAllPages()) {
      widths.push_back(page.getMediaBox().getArrayItem(2).getIntValueAsInt());
    }
    return widths;
  } catch (...) {
    return {};
  }
}

int PageRotate(const std::filesystem::path& path, int page_number) {
  try {
    QPDF pdf;
    pdf.processFile(WideToUtf8(path.wstring()).c_str());
    auto pages = QPDFPageDocumentHelper(pdf).getAllPages();
    if (page_number < 1 || static_cast<std::size_t>(page_number) > pages.size()) return 0;
    const auto rotate = pages[page_number - 1].getObjectHandle().getKey("/Rotate");
    return rotate.isNull() ? 0 : rotate.getIntValueAsInt();
  } catch (...) {
    return 0;
  }
}

std::filesystem::path TempDir() {
  const auto root = std::filesystem::temp_directory_path() /
                    ("lw-pdf-transform-test-" + std::to_string(GetCurrentProcessId()));
  std::filesystem::create_directories(root);
  return root;
}

void CheckReverse() {
  const auto root = TempDir();
  const auto input = root / "four-pages.pdf";
  const auto output = root / "reversed.pdf";
  CreatePdf(input, {100, 200, 300, 400});
  const auto result = TransformPdf(input.wstring(), output.wstring(),
                                   PdfTransformRequest{PdfTransformKind::ReversePages});
  Check(result.error == PdfTransformError::None && result.page_count == 4,
        "reverses a four-page PDF");
  Check(PageWidths(output) == std::vector<int>({400, 300, 200, 100}),
        "page order becomes D C B A");
  Check(PageWidths(input) == std::vector<int>({100, 200, 300, 400}),
        "the input PDF stays untouched");
}

void CheckReverseOutlineFollowsContent() {
  const auto root = TempDir();
  const auto input = root / "outline.pdf";
  const auto output = root / "outline-reversed.pdf";
  QPDF pdf;
  pdf.emptyPDF();
  QPDFPageDocumentHelper helper(pdf);
  std::vector<QPDFObjectHandle> pages;
  for (int width : {100, 200, 300, 400}) {
    const auto page = NewPage(pdf, width, 0);
    helper.addPage(QPDFPageObjectHelper(page), false);
    pages.push_back(page);
  }
  QPDFObjectHandle outline_item = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
  outline_item.replaceKey("/Title", QPDFObjectHandle::newString("second page"));
  QPDFObjectHandle destination = QPDFObjectHandle::newArray();
  destination.appendItem(pages[1]);
  destination.appendItem(QPDFObjectHandle::newName("/Fit"));
  outline_item.replaceKey("/Dest", destination);
  QPDFObjectHandle outlines = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
  outline_item.replaceKey("/Parent", outlines);
  outlines.replaceKey("/First", outline_item);
  outlines.replaceKey("/Last", outline_item);
  pdf.getRoot().replaceKey("/Outlines", outlines);
  QPDFWriter writer(pdf);
  writer.setOutputFilename(WideToUtf8(input.wstring()).c_str());
  writer.write();

  const auto result = TransformPdf(input.wstring(), output.wstring(),
                                   PdfTransformRequest{PdfTransformKind::ReversePages});
  Check(result.error == PdfTransformError::None,
        "reverses a PDF that has an outline");

  QPDF reopened;
  reopened.processFile(WideToUtf8(output.wstring()).c_str());
  const auto dest = reopened.getRoot().getKey("/Outlines").getKey("/First").getKey("/Dest");
  auto dest_page = QPDFPageObjectHelper(dest.getArrayItem(0));
  Check(dest_page.getMediaBox().getArrayItem(2).getIntValueAsInt() == 200,
        "outline destination keeps pointing at the same page content");
}

void CheckRotations() {
  const auto root = TempDir();
  const auto input = root / "rotated.pdf";
  const auto output = root / "rotated-out.pdf";
  CreatePdf(input, {100, 200, 300, 400}, {90, 0, 0, 0});

  const auto single = TransformPdf(input.wstring(), output.wstring(),
      PdfTransformRequest{PdfTransformKind::RotatePages, PdfRotation::Right90,
                          PdfPageSelection{PdfPageSelectionKind::Single, 3, ""}});
  Check(single.error == PdfTransformError::None && single.page_count == 4,
        "rotates a single page");
  Check(PageRotate(output, 1) == 90 && PageRotate(output, 2) == 0 &&
            PageRotate(output, 3) == 90 && PageRotate(output, 4) == 0,
        "only the selected page is rotated");

  const auto stacked = TransformPdf(input.wstring(), output.wstring(),
      PdfTransformRequest{PdfTransformKind::RotatePages, PdfRotation::Right90,
                          PdfPageSelection{PdfPageSelectionKind::Single, 1, ""}});
  Check(stacked.error == PdfTransformError::None, "rotates a page with existing /Rotate");
  Check(PageRotate(output, 1) == 180,
        "relative rotation stacks with an existing /Rotate 90");

  const auto left = TransformPdf(input.wstring(), output.wstring(),
      PdfTransformRequest{PdfTransformKind::RotatePages, PdfRotation::Left90,
                          PdfPageSelection{PdfPageSelectionKind::Single, 2, ""}});
  Check(left.error == PdfTransformError::None && PageRotate(output, 2) == 270,
        "left rotation produces 270 degrees");

  const auto all = TransformPdf(input.wstring(), output.wstring(),
      PdfTransformRequest{PdfTransformKind::RotatePages, PdfRotation::Rotate180,
                          PdfPageSelection{PdfPageSelectionKind::All, 0, ""}});
  Check(all.error == PdfTransformError::None &&
            PageRotate(output, 1) == 270 && PageRotate(output, 2) == 180 &&
            PageRotate(output, 3) == 180 && PageRotate(output, 4) == 180,
        "rotates all pages and stacks the first page to 270");

  const auto range_input = root / "seven-pages.pdf";
  CreatePdf(range_input, {10, 20, 30, 40, 50, 60, 70});
  const auto ranged = TransformPdf(range_input.wstring(), output.wstring(),
      PdfTransformRequest{PdfTransformKind::RotatePages, PdfRotation::Right90,
                          PdfPageSelection{PdfPageSelectionKind::Range, 0, "1,3,5-7"}});
  Check(ranged.error == PdfTransformError::None,
        "rotates an explicit page range");
  Check(PageRotate(output, 1) == 90 && PageRotate(output, 2) == 0 &&
            PageRotate(output, 3) == 90 && PageRotate(output, 4) == 0 &&
            PageRotate(output, 5) == 90 && PageRotate(output, 6) == 90 &&
            PageRotate(output, 7) == 90,
        "range 1,3,5-7 rotates exactly pages 1 3 5 6 7");
}

void CheckInvalidRanges() {
  const auto root = TempDir();
  const auto input = root / "invalid-range.pdf";
  const auto output = root / "invalid-range-out.pdf";
  CreatePdf(input, {100, 200, 300, 400});
  for (const std::string range : {"0", "-1", "abc", "1--", "999999", "5-3", "1,,2"}) {
    const auto result = TransformPdf(input.wstring(), output.wstring(),
        PdfTransformRequest{PdfTransformKind::RotatePages, PdfRotation::Right90,
                            PdfPageSelection{PdfPageSelectionKind::Range, 0, range}});
    Check(result.error == PdfTransformError::PageRangeInvalid,
          ("rejects invalid range '" + range + "'").c_str());
    Check(!std::filesystem::exists(output),
          ("does not produce output for invalid range '" + range + "'").c_str());
  }
  const auto single_out_of_bounds = TransformPdf(input.wstring(), output.wstring(),
      PdfTransformRequest{PdfTransformKind::RotatePages, PdfRotation::Right90,
                          PdfPageSelection{PdfPageSelectionKind::Single, 5, ""}});
  Check(single_out_of_bounds.error == PdfTransformError::PageRangeInvalid,
        "rejects a single page beyond the page count");
}

void CheckErrorCases() {
  const auto root = TempDir();
  const auto input = root / "broken.pdf";
  {
    std::ofstream broken(input, std::ios::binary);
    broken << "this is not a PDF at all";
  }
  const auto output = root / "broken-out.pdf";
  const auto broken_result = TransformPdf(input.wstring(), output.wstring(),
                                          PdfTransformRequest{PdfTransformKind::ReversePages});
  Check(broken_result.error == PdfTransformError::Failed &&
            !std::filesystem::exists(output),
        "a damaged PDF fails safely without output");

  const auto missing = TransformPdf((root / "missing.pdf").wstring(),
                                    output.wstring(),
                                    PdfTransformRequest{PdfTransformKind::ReversePages});
  Check(missing.error == PdfTransformError::SourceUnavailable,
        "a missing source reports source unavailable");

  const auto same = TransformPdf(input.wstring(), input.wstring(),
                                 PdfTransformRequest{PdfTransformKind::ReversePages});
  Check(same.error == PdfTransformError::SameFile,
        "rejects transforming a file onto itself");

  const auto good = root / "good.pdf";
  CreatePdf(good, {100, 200});
  const auto directory_output = TransformPdf(good.wstring(), root.wstring(),
                                             PdfTransformRequest{PdfTransformKind::ReversePages});
  Check(directory_output.error == PdfTransformError::OutputWriteFailed,
        "reports a write failure when output is unusable");
}

void CheckPasswordProtected() {
  const auto root = TempDir();
  const auto input = root / "encrypted.pdf";
  const auto output = root / "encrypted-out.pdf";
  {
    QPDF pdf;
    pdf.emptyPDF();
    QPDFPageDocumentHelper helper(pdf);
    helper.addPage(QPDFPageObjectHelper(NewPage(pdf, 100, 0)), false);
    QPDFWriter writer(pdf);
    writer.setOutputFilename(WideToUtf8(input.wstring()).c_str());
    writer.setR6EncryptionParameters("user", "owner", true, true, true, true,
                                     true, true, qpdf_r3p_full, true);
    writer.write();
  }
  const auto result = TransformPdf(input.wstring(), output.wstring(),
                                   PdfTransformRequest{PdfTransformKind::ReversePages});
  Check(result.error == PdfTransformError::PasswordRequired,
        "a password-protected PDF reports password required");
  Check(!std::filesystem::exists(output),
        "does not produce output for a password-protected PDF");
}

void CheckLargePdfSmoke() {
  const auto root = TempDir();
  const auto input = root / "large.pdf";
  const auto output = root / "large-out.pdf";
  {
    QPDF pdf;
    pdf.emptyPDF();
    QPDFPageDocumentHelper helper(pdf);
    const std::string big_stream(32U * 1024U * 1024U, '0');
    QPDFObjectHandle page = NewPage(pdf, 100, 0);
    page.replaceKey("/Contents",
        pdf.makeIndirectObject(QPDFObjectHandle::newStream(&pdf, big_stream)));
    helper.addPage(QPDFPageObjectHelper(page), false);
    QPDFWriter writer(pdf);
    writer.setOutputFilename(WideToUtf8(input.wstring()).c_str());
    writer.write();
  }
  const auto result = TransformPdf(input.wstring(), output.wstring(),
                                   PdfTransformRequest{PdfTransformKind::ReversePages});
  Check(result.error == PdfTransformError::None && result.page_count == 1 &&
            std::filesystem::file_size(output) > 1024U,
        "transforms a PDF with a 32 MB content stream");
}

}  // namespace

int main() {
  CheckReverse();
  CheckReverseOutlineFollowsContent();
  CheckRotations();
  CheckInvalidRanges();
  CheckErrorCases();
  CheckPasswordProtected();
  CheckLargePdfSmoke();

  const auto root = std::filesystem::temp_directory_path() /
                    ("lw-pdf-transform-test-" + std::to_string(GetCurrentProcessId()));
  std::filesystem::remove_all(root);
  return failures == 0 ? 0 : 1;
}
