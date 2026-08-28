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
  static constexpr UINT kTransformCompleteMessage = WM_APP + 17;
  BridgeDispatcher(HWND owner, std::shared_ptr<PdfFileGrantManager> grants,
                   std::shared_ptr<RecentFileStore> recent_files);
  void Dispatch(const std::string& request, Reply reply);
  void SetLaunchPath(const std::optional<std::wstring>& path);
  std::optional<std::string> TakeLaunchEvent();
  void CompleteTransform(LPARAM payload);
  static void DiscardTransformCompletion(LPARAM payload);

 private:
  HWND owner_;
  std::shared_ptr<PdfFileGrantManager> grants_;
  std::shared_ptr<RecentFileStore> recent_files_;
  std::optional<std::wstring> launch_path_;
  bool transform_busy_ = false;
};
