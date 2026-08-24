#include "resources/frontend_bundle.h"

#include "common/utf8.h"

#include <ShlObj.h>
#include <Windows.h>
#include <miniz.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {
constexpr int kFrontendResourceId = 101;

std::filesystem::path CacheRoot() {
  PWSTR local = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local))) throw std::runtime_error("Cannot locate local application data");
  const std::filesystem::path root(local); CoTaskMemFree(local);
  return root / L"lw.PDF" / L"frontend";
}
std::wstring PayloadHash(const std::uint8_t* bytes, std::size_t size) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (std::size_t index = 0; index < size; ++index) { hash ^= bytes[index]; hash *= 1099511628211ULL; }
  std::wostringstream output; output << std::hex << std::setw(16) << std::setfill(L'0') << hash;
  return output.str();
}
bool IsSafeRelativePath(const std::filesystem::path& path) {
  if (path.empty() || path.is_absolute() || path.has_root_name()) return false;
  for (const auto& part : path) if (part == L"..") return false;
  return true;
}
void ExtractZip(const std::uint8_t* bytes, std::size_t size, const std::filesystem::path& destination) {
  mz_zip_archive archive{};
  if (!mz_zip_reader_init_mem(&archive, bytes, size, 0)) throw std::runtime_error("Embedded frontend ZIP is invalid");
  try {
    const auto count = mz_zip_reader_get_num_files(&archive);
    for (mz_uint index = 0; index < count; ++index) {
      mz_zip_archive_file_stat stat{};
      if (!mz_zip_reader_file_stat(&archive, index, &stat)) throw std::runtime_error("Cannot inspect frontend bundle");
      const auto relative = std::filesystem::path(Utf8ToWide(stat.m_filename)).lexically_normal();
      if (!IsSafeRelativePath(relative)) throw std::runtime_error("Unsafe frontend bundle path");
      const auto output = destination / relative;
      if (stat.m_is_directory) { std::filesystem::create_directories(output); continue; }
      std::filesystem::create_directories(output.parent_path());
      size_t extracted_size{}; void* extracted = mz_zip_reader_extract_to_heap(&archive, index, &extracted_size, 0);
      if (!extracted && extracted_size) throw std::runtime_error("Cannot extract frontend bundle");
      std::ofstream file(output, std::ios::binary | std::ios::trunc);
      if (!file) { mz_free(extracted); throw std::runtime_error("Cannot create frontend cache file"); }
      if (extracted_size) file.write(static_cast<const char*>(extracted), static_cast<std::streamsize>(extracted_size));
      mz_free(extracted);
      if (!file) throw std::runtime_error("Cannot write frontend cache file");
    }
  } catch (...) { mz_zip_reader_end(&archive); throw; }
  mz_zip_reader_end(&archive);
}
}

std::filesystem::path ExtractBundledFrontend() {
  const auto module = GetModuleHandleW(nullptr);
  const auto resource = FindResourceW(module, MAKEINTRESOURCEW(kFrontendResourceId), RT_RCDATA);
  const auto loaded = resource ? LoadResource(module, resource) : nullptr;
  const auto size = resource ? static_cast<std::size_t>(SizeofResource(module, resource)) : 0;
  const auto* bytes = loaded ? static_cast<const std::uint8_t*>(LockResource(loaded)) : nullptr;
  if (!bytes || !size) throw std::runtime_error("Embedded frontend resource is missing");
  const auto root = CacheRoot(); std::filesystem::create_directories(root);
  const auto hash = PayloadHash(bytes, size); const auto destination = root / hash;
  if (std::filesystem::exists(destination / L"index.html")) return destination;
  std::error_code error; const auto temporary = root / (hash + L".tmp-" + std::to_wstring(GetCurrentProcessId()));
  std::filesystem::remove_all(temporary, error); std::filesystem::create_directories(temporary);
  try {
    ExtractZip(bytes, size, temporary);
    if (!std::filesystem::exists(temporary / L"index.html")) throw std::runtime_error("Embedded frontend is incomplete");
    std::filesystem::rename(temporary, destination, error);
    if (error && !std::filesystem::exists(destination / L"index.html")) throw std::runtime_error("Cannot publish frontend cache");
    if (error) std::filesystem::remove_all(temporary, error);
  } catch (...) { std::filesystem::remove_all(temporary, error); throw; }
  return destination;
}
