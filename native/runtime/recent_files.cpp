#include "runtime/recent_files.h"

#include "common/utf8.h"

#include <Windows.h>
#include <ShlObj.h>
#include <bcrypt.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <stdexcept>
#include <system_error>

using nlohmann::json;

namespace {
constexpr std::uintmax_t kMaximumStoreBytes = 1024U * 1024U;

std::string NewId() {
  unsigned char bytes[16]{};
  if (BCryptGenRandom(nullptr, bytes, sizeof(bytes),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    throw std::runtime_error("Cannot create recent file token");
  }
  constexpr char hex[] = "0123456789abcdef";
  std::string id(sizeof(bytes) * 2U, '0');
  for (std::size_t i = 0; i < sizeof(bytes); ++i) {
    id[i * 2U] = hex[bytes[i] >> 4U];
    id[i * 2U + 1U] = hex[bytes[i] & 0x0fU];
  }
  return id;
}

bool ValidId(const std::string& id) {
  return id.size() == 32U && std::all_of(id.begin(), id.end(), [](char value) {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
  });
}

bool SamePath(const std::filesystem::path& left,
              const std::filesystem::path& right) {
  return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) ==
         CSTR_EQUAL;
}

bool AvailablePdf(const std::filesystem::path& path) {
  std::error_code error;
  return _wcsicmp(path.extension().c_str(), L".pdf") == 0 &&
         std::filesystem::is_regular_file(path, error) && !error;
}

std::int64_t NowMilliseconds() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
}

RecentFileStore::RecentFileStore(std::filesystem::path storage_path,
                                 std::size_t maximum_files)
    : storage_path_(std::move(storage_path)),
      maximum_files_(std::max<std::size_t>(1U, maximum_files)) {
  Load();
}

std::vector<RecentFileInfo> RecentFileStore::List() {
  std::lock_guard lock(mutex_);
  if (PruneUnavailableLocked()) {
    try {
      PersistLocked();
    } catch (...) {
      // A read operation remains useful even when the cleanup cannot be saved.
    }
  }
  std::vector<RecentFileInfo> result;
  result.reserve(files_.size());
  for (const auto& file : files_) result.push_back(file.public_info);
  return result;
}

RecentFileInfo RecentFileStore::Confirm(const PdfFileGrant& grant) {
  if (!AvailablePdf(grant.path)) {
    throw std::runtime_error("Recent PDF file is unavailable");
  }
  std::lock_guard lock(mutex_);
  const auto existing = std::find_if(files_.begin(), files_.end(),
      [&grant](const StoredFile& file) { return SamePath(file.path, grant.path); });
  StoredFile record;
  if (existing != files_.end()) {
    record = *existing;
    files_.erase(existing);
  } else {
    record.public_info.id = NewId();
    record.path = grant.path;
  }
  record.public_info.name = grant.name;
  record.public_info.last_opened = NowMilliseconds();
  files_.insert(files_.begin(), std::move(record));
  if (files_.size() > maximum_files_) files_.resize(maximum_files_);
  PersistLocked();
  return files_.front().public_info;
}

std::optional<std::filesystem::path> RecentFileStore::Resolve(
    const std::string& id) {
  if (!ValidId(id)) return std::nullopt;
  std::lock_guard lock(mutex_);
  const auto found = std::find_if(files_.begin(), files_.end(),
      [&id](const StoredFile& file) { return file.public_info.id == id; });
  if (found == files_.end()) return std::nullopt;
  if (AvailablePdf(found->path)) return found->path;
  files_.erase(found);
  try {
    PersistLocked();
  } catch (...) {
  }
  return std::nullopt;
}

void RecentFileStore::Clear() {
  std::lock_guard lock(mutex_);
  files_.clear();
  PersistLocked();
}

void RecentFileStore::Load() {
  if (storage_path_.empty()) return;
  std::error_code error;
  if (!std::filesystem::is_regular_file(storage_path_, error) || error ||
      std::filesystem::file_size(storage_path_, error) > kMaximumStoreBytes ||
      error) {
    return;
  }
  try {
    std::ifstream input(storage_path_, std::ios::binary);
    const auto root = json::parse(input);
    if (!root.is_object() || root.value("version", 0) != 1 ||
        !root.contains("files") || !root.at("files").is_array()) {
      return;
    }
    for (const auto& value : root.at("files")) {
      if (!value.is_object()) continue;
      const auto id = value.value("id", "");
      const auto name = value.value("name", "");
      const auto path_text = value.value("path", "");
      const auto last_opened = value.value("lastOpened", std::int64_t{0});
      if (!ValidId(id) || name.empty() || name.size() > 1024U ||
          path_text.empty() || path_text.size() > 32768U || last_opened <= 0) {
        continue;
      }
      std::error_code path_error;
      const auto path = std::filesystem::absolute(Utf8ToWide(path_text), path_error)
                            .lexically_normal();
      if (path_error || _wcsicmp(path.extension().c_str(), L".pdf") != 0) continue;
      const auto duplicate = std::any_of(files_.begin(), files_.end(),
          [&id, &path](const StoredFile& file) {
            return file.public_info.id == id || SamePath(file.path, path);
          });
      if (duplicate) continue;
      files_.push_back({{id, name, last_opened}, path});
    }
    std::stable_sort(files_.begin(), files_.end(),
        [](const StoredFile& left, const StoredFile& right) {
          return left.public_info.last_opened > right.public_info.last_opened;
        });
    if (files_.size() > maximum_files_) files_.resize(maximum_files_);
  } catch (...) {
    files_.clear();
  }
}

void RecentFileStore::PersistLocked() const {
  if (storage_path_.empty()) return;
  const auto parent = storage_path_.parent_path();
  std::error_code error;
  if (!parent.empty()) std::filesystem::create_directories(parent, error);
  if (error) throw std::runtime_error("Cannot create recent file directory");

  auto array = json::array();
  for (const auto& file : files_) {
    array.push_back({{"id", file.public_info.id},
                     {"name", file.public_info.name},
                     {"path", WideToUtf8(file.path.wstring())},
                     {"lastOpened", file.public_info.last_opened}});
  }
  const json root{{"version", 1}, {"files", std::move(array)}};
  auto temporary = storage_path_;
  temporary += L".tmp";
  std::filesystem::remove(temporary, error);
  error.clear();
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << root.dump(2);
    output.close();
    if (!output) {
      std::filesystem::remove(temporary, error);
      throw std::runtime_error("Cannot save recent files");
    }
  }
  if (!MoveFileExW(temporary.c_str(), storage_path_.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(temporary, error);
    throw std::runtime_error("Cannot replace recent file store");
  }
}

bool RecentFileStore::PruneUnavailableLocked() {
  const auto before = files_.size();
  files_.erase(std::remove_if(files_.begin(), files_.end(),
                   [](const StoredFile& file) { return !AvailablePdf(file.path); }),
               files_.end());
  return files_.size() != before;
}

std::filesystem::path DefaultRecentFilesPath() {
  PWSTR local_app_data = nullptr;
  if (SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr,
                           &local_app_data) != S_OK || !local_app_data) {
    if (local_app_data) CoTaskMemFree(local_app_data);
    return {};
  }
  const std::filesystem::path path =
      std::filesystem::path(local_app_data) / L"lw.PDF" / L"recent.json";
  CoTaskMemFree(local_app_data);
  return path;
}
