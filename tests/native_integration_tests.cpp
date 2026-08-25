#include "app/command_line.h"
#include "associations/file_association.h"
#include "common/utf8.h"
#include "runtime/bounded_file_stream.h"
#include "runtime/file_grant.h"
#include "runtime/http_range.h"
#include "runtime/recent_files.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
int failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAILED: " << message << "\n";
  ++failures;
}

void CheckRange(std::wstring_view header, std::uintmax_t size,
                std::uintmax_t first, std::uintmax_t last, bool partial,
                const char* message) {
  const auto range = ParseHttpByteRange(header, size);
  Check(range && range->first == first && range->last == last &&
            range->partial == partial,
        message);
}
}  // namespace

int main() {
  Check(BuildAssociationOpenCommand(L"C:\\Tools\\lw.PDF.exe") ==
            L"\"C:\\Tools\\lw.PDF.exe\" \"%1\"",
        "association command quotes executable and argument");
  const auto association_status = GetPdfFileAssociationStatus();
  Check(!association_status.executable_path.empty(),
        "queries native registration and default-application status");

  const auto root = std::filesystem::temp_directory_path() /
                    ("lw-pdf-native-test-" +
                     std::to_string(GetCurrentProcessId()));
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

  CheckRange(L"", 100, 0, 99, false, "serves a full response without Range");
  CheckRange(L"bytes=0-99", 100, 0, 99, true,
             "keeps an explicit full-file Range as a 206 response");
  CheckRange(L"bytes=10-19", 100, 10, 19, true,
             "parses a closed byte range");
  CheckRange(L"bytes=90-", 100, 90, 99, true,
             "parses an open-ended byte range");
  CheckRange(L"bytes=-10", 100, 90, 99, true,
             "parses a suffix byte range");
  CheckRange(L"bytes=-150", 100, 0, 99, true,
             "clamps an oversized suffix range");
  CheckRange(L"bytes=95-150", 100, 95, 99, true,
             "clamps an oversized range end");
  Check(!ParseHttpByteRange(L"bytes=100-", 100),
        "rejects a range starting after EOF");
  Check(!ParseHttpByteRange(L"bytes=10-5", 100),
        "rejects a reversed range");
  Check(!ParseHttpByteRange(L"bytes=0-1,4-5", 100),
        "rejects unsupported multi-range requests");
  Check(!ParseHttpByteRange(L"items=0-1", 100),
        "rejects an unsupported range unit");
  const auto partial_headers = BuildPdfRangeResponseHeaders(
      HttpByteRange{10, 19, true}, 100);
  Check(partial_headers.find(L"Access-Control-Expose-Headers: Accept-Ranges, Content-Length, Content-Range") !=
            std::wstring::npos,
        "exposes Range response headers to PDF.js across the FileGrant origin");
  Check(partial_headers.find(L"Content-Length: 10") != std::wstring::npos &&
            partial_headers.find(L"Content-Range: bytes 10-19/100") !=
                std::wstring::npos,
        "builds consistent partial response headers");
  const auto full_headers = BuildPdfRangeResponseHeaders(
      HttpByteRange{0, 99, false}, 100);
  Check(full_headers.find(L"Content-Range:") == std::wstring::npos,
        "does not add Content-Range to a full response");

  const auto ranged_pdf = root / "range.pdf";
  {
    std::ofstream output(ranged_pdf, std::ios::binary);
    for (std::uint16_t value = 0; value < 256; ++value) {
      output.put(static_cast<char>(value));
    }
  }
  IStream* bounded_stream = nullptr;
  Check(SUCCEEDED(OpenBoundedFileStream(ranged_pdf, 100, 20,
                                        &bounded_stream)) &&
            bounded_stream,
        "opens a bounded native stream");
  if (bounded_stream) {
    IAgileObject* agile = nullptr;
    Check(SUCCEEDED(bounded_stream->QueryInterface(IID_PPV_ARGS(&agile))) && agile,
          "exposes an agile stream for WebView2 background reads");
    if (agile) agile->Release();
    std::array<unsigned char, 64> bytes{};
    ULONG bytes_read = 0;
    Check(bounded_stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()),
                               &bytes_read) == S_FALSE &&
              bytes_read == 20 && bytes.front() == 100 && bytes[19] == 119,
          "never reads beyond the granted byte range");
    Check(bounded_stream->Read(bytes.data(), 1, &bytes_read) == S_FALSE &&
              bytes_read == 0,
          "reports EOF at the range boundary");
    LARGE_INTEGER seek{};
    seek.QuadPart = 5;
    Check(SUCCEEDED(bounded_stream->Seek(seek, STREAM_SEEK_SET, nullptr)),
          "seeks relative to the granted range");
    Check(bounded_stream->Read(bytes.data(), 4, &bytes_read) == S_OK &&
              bytes_read == 4 && bytes[0] == 105 && bytes[3] == 108,
          "reads from a bounded relative seek");
    STATSTG status{};
    Check(SUCCEEDED(bounded_stream->Stat(&status, STATFLAG_NONAME)) &&
              status.cbSize.QuadPart == 20,
          "reports only the bounded stream length");
    IStream* cloned = nullptr;
    Check(SUCCEEDED(bounded_stream->Clone(&cloned)) && cloned,
          "clones a bounded stream at its current relative position");
    if (cloned) {
      unsigned char clone_byte = 0;
      Check(cloned->Read(&clone_byte, 1, &bytes_read) == S_OK &&
                clone_byte == 109,
            "the cloned stream preserves the relative cursor");
      cloned->Release();
    }
    unsigned char original_byte = 0;
    Check(bounded_stream->Read(&original_byte, 1, &bytes_read) == S_OK &&
              original_byte == 109,
          "a clone does not advance the original stream");
    bounded_stream->Release();
  }

  constexpr std::uintmax_t large_size = 128ULL * 1024ULL * 1024ULL;
  const auto large_pdf = root / "large.pdf";
  {
    std::ofstream output(large_pdf, std::ios::binary);
    output << "%PDF-1.7\n";
    output.seekp(static_cast<std::streamoff>(large_size - 1U));
    output.put('\0');
  }
  const auto large_grant = grants.Create(large_pdf);
  Check(large_grant.size == large_size,
        "grants a large PDF without copying it into memory");
  IStream* large_tail = nullptr;
  Check(SUCCEEDED(OpenBoundedFileStream(large_grant.path, large_size - 4096U,
                                        4096U, &large_tail)) &&
            large_tail,
        "opens a small Range over a large PDF");
  if (large_tail) {
    STATSTG status{};
    Check(SUCCEEDED(large_tail->Stat(&status, STATFLAG_NONAME)) &&
              status.cbSize.QuadPart == 4096,
          "large PDF requests expose only the requested bytes");
    large_tail->Release();
  }
  grants.Revoke(large_grant.id);

  const std::array<std::filesystem::path, 3> switching_files{
      pdf, ranged_pdf, large_pdf};
  std::string current_id;
  for (std::size_t index = 0; index < 100; ++index) {
    const auto next = grants.Create(switching_files[index % switching_files.size()]);
    Check(grants.Find(next.id).has_value(),
          "continuous switching keeps the newest grant alive");
    if (!current_id.empty()) {
      Check(grants.Revoke(current_id) && !grants.Find(current_id).has_value(),
            "continuous switching revokes the previous grant");
    }
    current_id = next.id;
  }
  Check(grants.Revoke(current_id), "revokes the final switched document grant");

  const auto recent_store_path = root / "recent.json";
  RecentFileStore recent_files(recent_store_path, 3);
  const auto first_recent_grant = grants.Create(pdf);
  const auto first_recent = recent_files.Confirm(first_recent_grant);
  Check(first_recent.id.size() == 32U && first_recent.name == grant.name,
        "stores a recent PDF behind an opaque identifier");
  const auto resolved_recent = recent_files.Resolve(first_recent.id);
  Check(resolved_recent && *resolved_recent == first_recent_grant.path,
        "resolves a recent identifier only inside the native process");

  const auto second_recent_grant = grants.Create(ranged_pdf);
  const auto second_recent = recent_files.Confirm(second_recent_grant);
  auto recent_list = recent_files.List();
  Check(recent_list.size() == 2U && recent_list.front().id == second_recent.id,
        "orders recent files by the latest successful open");
  const auto refreshed_first = recent_files.Confirm(first_recent_grant);
  recent_list = recent_files.List();
  Check(refreshed_first.id == first_recent.id &&
            recent_list.front().id == first_recent.id &&
            recent_list.size() == 2U,
        "deduplicates a file and preserves its opaque recent identifier");

  const auto third_pdf = root / "third.pdf";
  const auto fourth_pdf = root / "fourth.pdf";
  std::ofstream(third_pdf, std::ios::binary) << "%PDF-1.4\nthird";
  std::ofstream(fourth_pdf, std::ios::binary) << "%PDF-1.4\nfourth";
  recent_files.Confirm(grants.Create(third_pdf));
  const auto fourth_recent = recent_files.Confirm(grants.Create(fourth_pdf));
  recent_list = recent_files.List();
  Check(recent_list.size() == 3U && recent_list.front().id == fourth_recent.id &&
            !recent_files.Resolve(second_recent.id).has_value(),
        "keeps only the configured number of recent files");

  RecentFileStore reloaded_recent_files(recent_store_path, 3);
  recent_list = reloaded_recent_files.List();
  Check(recent_list.size() == 3U && recent_list.front().id == fourth_recent.id,
        "persists and reloads recent files");
  std::filesystem::remove(fourth_pdf);
  recent_list = reloaded_recent_files.List();
  Check(recent_list.size() == 2U &&
            !reloaded_recent_files.Resolve(fourth_recent.id).has_value(),
        "prunes recent files that no longer exist");
  reloaded_recent_files.Clear();
  Check(reloaded_recent_files.List().empty(), "clears the recent file list");
  RecentFileStore cleared_recent_files(recent_store_path, 3);
  Check(cleared_recent_files.List().empty(),
        "persists the cleared recent file list");

  std::filesystem::remove_all(root);
  return failures == 0 ? 0 : 1;
}
