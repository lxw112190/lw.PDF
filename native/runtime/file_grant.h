#pragma once

#include <cstdint>
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

class PdfFileGrantManager {
 public:
  PdfFileGrant Create(const std::filesystem::path& selected);
  std::optional<PdfFileGrant> Find(const std::string& id) const;
  bool Revoke(const std::string& id);

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, PdfFileGrant> grants_;
};
