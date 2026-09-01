#pragma once

#include <Windows.h>
#include <Unknwn.h>
#include <WebView2.h>
#include <wrl.h>

#include <functional>
#include <memory>
#include <string>

class PdfFileGrantManager;

class WebViewHost {
 public:
  using Reply = std::function<void(const std::string&)>;
  using MessageHandler = std::function<void(const std::string&, Reply)>;
  using ReadyHandler = std::function<void()>;
  ~WebViewHost();
  void Create(HWND window, const std::wstring& content_folder,
              std::shared_ptr<PdfFileGrantManager> grants,
              MessageHandler on_message, ReadyHandler on_ready);
  bool PostJson(const std::string& message);
  void Resize();

 private:
  HWND window_ = nullptr;
  MessageHandler on_message_;
  ReadyHandler on_ready_;
  std::shared_ptr<PdfFileGrantManager> grants_;
  Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller_;
  Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
  Microsoft::WRL::ComPtr<ICoreWebView2Environment> environment_;
  EventRegistrationToken message_token_{};
  EventRegistrationToken resource_token_{};
  EventRegistrationToken navigation_token_{};
  EventRegistrationToken frame_navigation_token_{};
  EventRegistrationToken new_window_token_{};
  EventRegistrationToken permission_token_{};
  EventRegistrationToken completed_token_{};
  EventRegistrationToken process_failed_token_{};
  UINT64 app_navigation_id_ = 0;
  bool failure_message_shown_ = false;
};
