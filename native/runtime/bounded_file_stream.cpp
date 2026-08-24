#include "runtime/bounded_file_stream.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <limits>
#include <mutex>
#include <new>
#include <utility>

namespace {
class BoundedFileStream final : public IStream, public IAgileObject {
 public:
  BoundedFileStream(std::filesystem::path path, const HANDLE file,
                    const ULONGLONG base, const ULONGLONG length)
      : path_(std::move(path)), file_(file), base_(base), length_(length) {}

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
    if (!object) return E_POINTER;
    *object = nullptr;
    if (iid == __uuidof(IUnknown) || iid == __uuidof(ISequentialStream) ||
        iid == __uuidof(IStream)) {
      *object = static_cast<IStream*>(this);
    } else if (iid == __uuidof(IAgileObject)) {
      *object = static_cast<IAgileObject*>(this);
    } else {
      return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
  }

  ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
  ULONG STDMETHODCALLTYPE Release() override {
    const auto remaining = --references_;
    if (!remaining) delete this;
    return remaining;
  }

  HRESULT STDMETHODCALLTYPE Read(void* buffer, const ULONG requested,
                                 ULONG* bytes_read) override {
    if (bytes_read) *bytes_read = 0;
    if (requested && !buffer) return STG_E_INVALIDPOINTER;
    if (!requested) return S_OK;
    std::lock_guard lock(mutex_);
    const auto remaining = length_ - position_;
    if (!remaining) return S_FALSE;
    const auto bounded = static_cast<DWORD>(
        std::min<ULONGLONG>(remaining, requested));
    LARGE_INTEGER absolute{};
    absolute.QuadPart = static_cast<LONGLONG>(base_ + position_);
    if (!SetFilePointerEx(file_, absolute, nullptr, FILE_BEGIN)) {
      return HRESULT_FROM_WIN32(GetLastError());
    }
    DWORD actual = 0;
    if (!ReadFile(file_, buffer, bounded, &actual, nullptr)) {
      return HRESULT_FROM_WIN32(GetLastError());
    }
    position_ += actual;
    if (bytes_read) *bytes_read = actual;
    return actual == requested ? S_OK : S_FALSE;
  }

  HRESULT STDMETHODCALLTYPE Write(const void*, ULONG, ULONG*) override {
    return STG_E_ACCESSDENIED;
  }

  HRESULT STDMETHODCALLTYPE Seek(const LARGE_INTEGER move, const DWORD origin,
                                 ULARGE_INTEGER* new_position) override {
    std::lock_guard lock(mutex_);
    ULONGLONG start = 0;
    if (origin == STREAM_SEEK_CUR) start = position_;
    else if (origin == STREAM_SEEK_END) start = length_;
    else if (origin != STREAM_SEEK_SET) return STG_E_INVALIDFUNCTION;
    ULONGLONG target = start;
    if (move.QuadPart < 0) {
      const auto magnitude = static_cast<ULONGLONG>(-(move.QuadPart + 1)) + 1U;
      if (magnitude > start) return STG_E_INVALIDFUNCTION;
      target = start - magnitude;
    } else {
      const auto amount = static_cast<ULONGLONG>(move.QuadPart);
      if (amount > length_ - start) return STG_E_INVALIDFUNCTION;
      target = start + amount;
    }
    position_ = target;
    if (new_position) new_position->QuadPart = position_;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE SetSize(ULARGE_INTEGER) override {
    return STG_E_ACCESSDENIED;
  }

  HRESULT STDMETHODCALLTYPE CopyTo(IStream* destination, ULARGE_INTEGER amount,
                                   ULARGE_INTEGER* bytes_read,
                                   ULARGE_INTEGER* bytes_written) override {
    if (!destination) return STG_E_INVALIDPOINTER;
    if (bytes_read) bytes_read->QuadPart = 0;
    if (bytes_written) bytes_written->QuadPart = 0;
    ULONGLONG total_read = 0;
    ULONGLONG total_written = 0;
    std::array<unsigned char, 64U * 1024U> buffer{};
    while (total_read < amount.QuadPart) {
      const auto requested = static_cast<ULONG>(std::min<ULONGLONG>(
          buffer.size(), amount.QuadPart - total_read));
      ULONG actual = 0;
      const auto read_result = Read(buffer.data(), requested, &actual);
      if (FAILED(read_result)) return read_result;
      if (!actual) break;
      total_read += actual;
      ULONG written = 0;
      const auto write_result = destination->Write(buffer.data(), actual, &written);
      total_written += written;
      if (FAILED(write_result)) return write_result;
      if (written != actual) return STG_E_CANTSAVE;
      if (read_result == S_FALSE) break;
    }
    if (bytes_read) bytes_read->QuadPart = total_read;
    if (bytes_written) bytes_written->QuadPart = total_written;
    return total_read == amount.QuadPart ? S_OK : S_FALSE;
  }

  HRESULT STDMETHODCALLTYPE Commit(DWORD) override { return S_OK; }
  HRESULT STDMETHODCALLTYPE Revert() override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE LockRegion(ULARGE_INTEGER, ULARGE_INTEGER,
                                       DWORD) override {
    return STG_E_INVALIDFUNCTION;
  }
  HRESULT STDMETHODCALLTYPE UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER,
                                         DWORD) override {
    return STG_E_INVALIDFUNCTION;
  }
  HRESULT STDMETHODCALLTYPE Stat(STATSTG* status, DWORD) override {
    if (!status) return STG_E_INVALIDPOINTER;
    *status = {};
    status->type = STGTY_STREAM;
    status->cbSize.QuadPart = length_;
    status->grfMode = STGM_READ;
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE Clone(IStream** stream) override {
    if (!stream) return E_POINTER;
    ULONGLONG position = 0;
    {
      std::lock_guard lock(mutex_);
      position = position_;
    }
    const auto opened = OpenBoundedFileStream(path_, base_, length_, stream);
    if (FAILED(opened)) return opened;
    LARGE_INTEGER target{};
    target.QuadPart = static_cast<LONGLONG>(position);
    const auto sought = (*stream)->Seek(target, STREAM_SEEK_SET, nullptr);
    if (FAILED(sought)) {
      (*stream)->Release();
      *stream = nullptr;
    }
    return sought;
  }

 private:
  ~BoundedFileStream() {
    if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
  }

  std::atomic<ULONG> references_{1};
  std::filesystem::path path_;
  HANDLE file_ = INVALID_HANDLE_VALUE;
  const ULONGLONG base_ = 0;
  const ULONGLONG length_ = 0;
  ULONGLONG position_ = 0;
  std::mutex mutex_;
};
}  // namespace

HRESULT OpenBoundedFileStream(const std::filesystem::path& path,
                              const std::uintmax_t offset,
                              const std::uintmax_t length,
                              IStream** stream) {
  if (!stream) return E_POINTER;
  *stream = nullptr;
  if (!length ||
      offset > static_cast<std::uintmax_t>(std::numeric_limits<LONGLONG>::max()) ||
      length > static_cast<std::uintmax_t>(std::numeric_limits<LONGLONG>::max()) -
                   offset) {
    return E_INVALIDARG;
  }
  const auto file = CreateFileW(path.c_str(), GENERIC_READ,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
  LARGE_INTEGER file_size{};
  if (!GetFileSizeEx(file, &file_size)) {
    const auto error = GetLastError();
    CloseHandle(file);
    return HRESULT_FROM_WIN32(error);
  }
  if (file_size.QuadPart < 0 ||
      offset + length > static_cast<std::uintmax_t>(file_size.QuadPart)) {
    CloseHandle(file);
    return STG_E_INVALIDFUNCTION;
  }
  auto* bounded = new (std::nothrow) BoundedFileStream(
      path, file, static_cast<ULONGLONG>(offset),
      static_cast<ULONGLONG>(length));
  if (!bounded) {
    CloseHandle(file);
    return E_OUTOFMEMORY;
  }
  *stream = bounded;
  return S_OK;
}
