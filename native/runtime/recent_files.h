#pragma once

#include "runtime/file_grant.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct RecentFileInfo {
  std::string id;
  std::string name;
  std::int64_t last_opened = 0;
};

class RecentFileStore {
 public:
  explicit RecentFileStore(std::filesystem::path storage_path,
                           std::size_t maximum_files = 10);

  std::vector<RecentFileInfo> List();
  RecentFileInfo Confirm(const PdfFileGrant& grant);
  std::optional<std::filesystem::path> Resolve(const std::string& id);
  void Clear();

 private:
  struct StoredFile {
    RecentFileInfo public_info;
    std::filesystem::path path;
  };

  void Load();
  void PersistLocked() const;
  bool PruneUnavailableLocked();

  std::filesystem::path storage_path_;
  std::size_t maximum_files_;
  mutable std::mutex mutex_;
  std::vector<StoredFile> files_;
};

std::filesystem::path DefaultRecentFilesPath();
