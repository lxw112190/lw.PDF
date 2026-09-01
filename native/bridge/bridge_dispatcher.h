#pragma once

#include <Windows.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

class PdfFileGrantManager;
class RecentFileStore;

class BridgeDispatcher {
 public:
  using Reply = std::function<void(const std::string&)>;
  static constexpr UINT kPageEditCompleteMessage = WM_APP + 19;
  BridgeDispatcher(HWND owner, std::shared_ptr<PdfFileGrantManager> grants,
                   std::shared_ptr<RecentFileStore> recent_files);
  void Dispatch(const std::string& request, Reply reply);
  void SetLaunchPath(const std::optional<std::wstring>& path);
  std::optional<std::string> TakeLaunchEvent();
  void CompletePageEdit(LPARAM payload);
  static void DiscardPageEditCompletion(LPARAM payload);
  bool IsDirty() const { return dirty_; }

 private:
  HWND owner_;
  std::shared_ptr<PdfFileGrantManager> grants_;
  std::shared_ptr<RecentFileStore> recent_files_;
  std::optional<std::wstring> launch_path_;
  bool page_edit_busy_ = false;
  bool dirty_ = false;
  bool annotation_dirty_ = false;
  bool organizer_dirty_ = false;
};
