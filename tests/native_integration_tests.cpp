#include "app/command_line.h"
#include "associations/file_association.h"
#include "common/utf8.h"
#include "runtime/file_grant.h"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
int failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << "\n";
  ++failures;
}
}  // namespace

int main() {
  Check(BuildAssociationOpenCommand(L"C:\\Tools\\lw.PDF.exe") ==
            L"\"C:\\Tools\\lw.PDF.exe\" \"%1\"",
        "association command quotes executable and argument");

  const auto root = std::filesystem::temp_directory_path() / "lw-pdf-native-test";
  std::filesystem::create_directories(root);
  const auto pdf = root / "中文 PDF 文件.pdf";
  std::ofstream(pdf, std::ios::binary) << "%PDF-1.4\n";
  const auto resolved = ResolveLaunchPdfPath(pdf.wstring());
  Check(resolved.has_value(), "accepts an existing PDF with a Unicode name");
  Check(!ResolveLaunchPdfPath((root / "not-a-pdf.txt").wstring()).has_value(),
        "rejects non-PDF arguments");
  PdfFileGrantManager grants;
  const auto grant = grants.Create(pdf);
  Check(grant.url.rfind("https://file.lwpdf/", 0) == 0,
        "creates an opaque native file URL");
  Check(grant.name == WideToUtf8(pdf.filename().wstring()) && grant.size > 0 &&
            grant.url.find(WideToUtf8(pdf.wstring())) == std::string::npos,
        "exposes filename and size without exposing the native path");
  Check(grants.Find(grant.id).has_value() && grants.Revoke(grant.id) &&
            !grants.Find(grant.id).has_value(),
        "revokes one-time native file permissions");
  const auto text = root / "not-a-pdf.txt";
  std::ofstream(text, std::ios::binary) << "not PDF";
  bool rejected = false;
  try { grants.Create(text); } catch (...) { rejected = true; }
  Check(rejected, "does not grant non-PDF files");
  std::filesystem::remove_all(root);
  return failures == 0 ? 0 : 1;
}
