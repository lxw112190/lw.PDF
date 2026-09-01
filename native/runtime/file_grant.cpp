#include "runtime/file_grant.h"

#include "common/utf8.h"

#include <bcrypt.h>
#include <chrono>
#include <iterator>
#include <stdexcept>

namespace {
std::string NewId() {
  unsigned char bytes[16]{};
  if (BCryptGenRandom(nullptr, bytes, sizeof(bytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    throw std::runtime_error("Cannot create file permission token");
  }
  constexpr char hex[] = "0123456789abcdef";
  std::string id(sizeof(bytes) * 2U, '0');
  for (std::size_t i = 0; i < sizeof(bytes); ++i) {
    id[i * 2U] = hex[bytes[i] >> 4U];
    id[i * 2U + 1U] = hex[bytes[i] & 0x0fU];
  }
  return id;
}
}

PdfFileGrant PdfFileGrantManager::Create(const std::filesystem::path& selected) {
  std::error_code error;
  const auto path = std::filesystem::absolute(selected, error).lexically_normal();
  if (error || _wcsicmp(path.extension().c_str(), L".pdf") != 0 ||
      !std::filesystem::is_regular_file(path, error) || error) {
    throw std::runtime_error("PDF file is unavailable");
  }
  PdfFileGrant grant;
  grant.id = NewId();
  grant.path = path;
  grant.name = WideToUtf8(path.filename().wstring());
  grant.size = std::filesystem::file_size(path, error);
  if (error) throw std::runtime_error("Cannot read PDF file size");
  grant.url = "https://file.lwpdf/" + grant.id + "/document.pdf";
  std::lock_guard lock(mutex_);
  grants_[grant.id] = grant;
  return grant;
}

std::optional<PdfFileGrant> PdfFileGrantManager::Find(const std::string& id) const {
  std::lock_guard lock(mutex_);
  const auto it = grants_.find(id);
  return it == grants_.end() ? std::nullopt : std::optional<PdfFileGrant>(it->second);
}

bool PdfFileGrantManager::Revoke(const std::string& id) {
  std::lock_guard lock(mutex_);
  return grants_.erase(id) != 0;
}

PdfSaveGrant PdfFileGrantManager::CreateSave(const std::filesystem::path& selected) {
  std::error_code error;
  const auto path = std::filesystem::absolute(selected, error).lexically_normal();
  if (error || _wcsicmp(path.extension().c_str(), L".pdf") != 0) {
    throw std::runtime_error("PDF output path is invalid");
  }
  PdfSaveGrant grant;
  grant.id = NewId();
  grant.path = path;
  grant.name = WideToUtf8(path.filename().wstring());
  grant.url = "https://save.lwpdf/" + grant.id + "/document.pdf";
  grant.expires_at = std::chrono::steady_clock::now() + std::chrono::minutes(30);
  std::lock_guard lock(mutex_);
  const auto now = std::chrono::steady_clock::now();
  for (auto it = save_grants_.begin(); it != save_grants_.end();) {
    it = it->second.expires_at < now ? save_grants_.erase(it) : std::next(it);
  }
  save_grants_[grant.id] = grant;
  return grant;
}

std::optional<PdfSaveGrant> PdfFileGrantManager::TakeSave(const std::string& id) {
  std::lock_guard lock(mutex_);
  const auto now = std::chrono::steady_clock::now();
  for (auto it = save_grants_.begin(); it != save_grants_.end();) {
    it = it->second.expires_at < now ? save_grants_.erase(it) : std::next(it);
  }
  const auto it = save_grants_.find(id);
  if (it == save_grants_.end()) return std::nullopt;
  const auto grant = it->second;
  save_grants_.erase(it);
  return grant;
}

bool PdfFileGrantManager::RevokeSave(const std::string& id) {
  std::lock_guard lock(mutex_);
  return save_grants_.erase(id) != 0;
}
