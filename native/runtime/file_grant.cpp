#include "runtime/file_grant.h"

#include "common/utf8.h"

#include <bcrypt.h>
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
