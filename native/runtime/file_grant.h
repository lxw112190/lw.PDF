#pragma once

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

struct PdfFileGrant {
  std::string id;
  std::filesystem::path path;
  std::string name;
  std::uintmax_t size = 0;
  std::string url;
};

struct PdfSaveGrant {
  std::string id;
  std::filesystem::path path;
  std::string name;
  std::string url;
  std::chrono::steady_clock::time_point expires_at{};
};

class PdfFileGrantManager {
 public:
  PdfFileGrant Create(const std::filesystem::path& selected);
  std::optional<PdfFileGrant> Find(const std::string& id) const;
  bool Revoke(const std::string& id);
  PdfSaveGrant CreateSave(const std::filesystem::path& selected);
  std::optional<PdfSaveGrant> TakeSave(const std::string& id);
  bool RevokeSave(const std::string& id);

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, PdfFileGrant> grants_;
  std::unordered_map<std::string, PdfSaveGrant> save_grants_;
};
