#pragma once

#include <Windows.h>

#include <functional>
#include <memory>
#include <optional>
#include <string>

class PdfFileGrantManager;

class BridgeDispatcher {
 public:
  using Reply = std::function<void(const std::string&)>;
  BridgeDispatcher(HWND owner, std::shared_ptr<PdfFileGrantManager> grants);
  void Dispatch(const std::string& request, Reply reply);
  void SetLaunchPath(const std::optional<std::wstring>& path);
  std::optional<std::string> TakeLaunchEvent();

 private:
  HWND owner_;
  std::shared_ptr<PdfFileGrantManager> grants_;
  std::optional<std::wstring> launch_path_;
};
