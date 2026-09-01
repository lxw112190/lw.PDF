#include "webview/webview_host.h"

#include "common/utf8.h"
#include "common/native_log.h"
#include "runtime/bounded_file_stream.h"
#include "runtime/file_grant.h"
#include "runtime/http_range.h"

#include <ShlObj.h>
#include <Shlwapi.h>
#include <Shellapi.h>

#include <filesystem>
#include <string>
#include <string_view>

using Microsoft::WRL::Callback;
namespace {
constexpr std::wstring_view kAppOrigin = L"https://app.lwpdf/";
constexpr std::wstring_view kFileOrigin = L"https://file.lwpdf/";

bool StartsWithIgnoreCase(std::wstring_view value, std::wstring_view prefix) {
  return value.size() >= prefix.size() && CompareStringOrdinal(value.data(),
      static_cast<int>(prefix.size()), prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}
bool IsTrustedAppUri(std::wstring_view uri) { return StartsWithIgnoreCase(uri, kAppOrigin); }
bool IsFileUri(std::wstring_view uri) { return StartsWithIgnoreCase(uri, kFileOrigin); }
bool IsPublicWebUri(std::wstring_view uri) {
  return (StartsWithIgnoreCase(uri, L"https://") || StartsWithIgnoreCase(uri, L"http://")) &&
      !IsTrustedAppUri(uri) && !IsFileUri(uri);
}
void OpenPublicWebUri(HWND owner, std::wstring_view uri) {
  if (!IsPublicWebUri(uri)) return;
  const std::wstring value(uri);
  ShellExecuteW(owner, L"open", value.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}
std::wstring UserDataFolder() {
  PWSTR local = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local))) return L"";
  const auto path = std::filesystem::path(local) / L"lw.PDF" / L"WebView2";
  CoTaskMemFree(local); std::filesystem::create_directories(path); return path.wstring();
}
bool IsTrustedWebView(ICoreWebView2* webview) {
  LPWSTR source = nullptr;
  const bool trusted = webview && SUCCEEDED(webview->get_Source(&source)) && source && IsTrustedAppUri(source);
  if (source) CoTaskMemFree(source);
  return trusted;
}
bool PostJsonToTrustedWebView(ICoreWebView2* webview, const std::string& message) {
  if (!IsTrustedWebView(webview)) return false;
  const auto json = Utf8ToWide(message);
  return SUCCEEDED(webview->PostWebMessageAsJson(json.c_str()));
}
std::optional<std::string> GrantIdFromUri(std::wstring_view uri) {
  if (!IsFileUri(uri)) return std::nullopt;
  const auto rest = uri.substr(kFileOrigin.size());
  const auto slash = rest.find(L'/');
  if (slash == std::wstring_view::npos || slash != 32U) return std::nullopt;
  std::string id;
  id.reserve(32U);
  for (const auto character : rest.substr(0, slash)) {
    if (!((character >= L'0' && character <= L'9') || (character >= L'a' && character <= L'f'))) return std::nullopt;
    id.push_back(static_cast<char>(character));
  }
  return id;
}
std::wstring GetRangeHeader(ICoreWebView2WebResourceRequest* request) {
  Microsoft::WRL::ComPtr<ICoreWebView2HttpRequestHeaders> headers;
  LPWSTR value = nullptr;
  if (!request || FAILED(request->get_Headers(&headers)) || !headers || FAILED(headers->GetHeader(L"Range", &value)) || !value) return L"";
  std::wstring result(value); CoTaskMemFree(value); return result;
}
void ReplyFile(ICoreWebView2Environment* environment, ICoreWebView2WebResourceRequestedEventArgs* args,
               const std::shared_ptr<PdfFileGrantManager>& grants) {
  Microsoft::WRL::ComPtr<ICoreWebView2WebResourceRequest> request;
  LPWSTR raw_uri = nullptr;
  if (!environment || !grants || FAILED(args->get_Request(&request)) || !request || FAILED(request->get_Uri(&raw_uri)) || !raw_uri) { if (raw_uri) CoTaskMemFree(raw_uri); return; }
  const auto id = GrantIdFromUri(raw_uri); CoTaskMemFree(raw_uri);
  const auto grant = id ? grants->Find(*id) : std::nullopt;
  Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
  if (!grant) { NativeLogDebug("file.range.grant_not_found"); environment->CreateWebResourceResponse(nullptr, 404, L"Not Found", L"Content-Type: text/plain", &response); args->put_Response(response.Get()); return; }
  const auto range = ParseHttpByteRange(GetRangeHeader(request.Get()), grant->size);
  if (!range) { NativeLogDebug("file.range.invalid id=" + grant->id); environment->CreateWebResourceResponse(nullptr, 416, L"Range Not Satisfiable", (L"Content-Range: bytes */" + std::to_wstring(grant->size)).c_str(), &response); args->put_Response(response.Get()); return; }
  Microsoft::WRL::ComPtr<IStream> stream;
  const auto opened = OpenBoundedFileStream(grant->path, range->first,
                                            range->Length(), &stream);
  if (FAILED(opened)) {
    NativeLogError("file.range.open_failed hresult=" +
                   std::to_string(static_cast<long>(opened)));
    environment->CreateWebResourceResponse(nullptr, 404, L"Not Found", L"Content-Type: text/plain", &response); args->put_Response(response.Get()); return;
  }
  NativeLogDebug("file.range.served id=" + grant->id + " first=" +
                 std::to_string(range->first) + " length=" +
                 std::to_string(range->Length()));
  const bool partial = range->partial;
  const auto headers = BuildPdfRangeResponseHeaders(*range, grant->size);
  const auto created = environment->CreateWebResourceResponse(stream.Get(), partial ? 206 : 200,
      partial ? L"Partial Content" : L"OK", headers.c_str(), &response);
  if (FAILED(created) || !response) {
    NativeLogError("file.range.response_create_failed hresult=" +
                   std::to_string(static_cast<long>(created)));
    return;
  }
  const auto delivered = args->put_Response(response.Get());
  if (FAILED(delivered)) {
    NativeLogError("file.range.response_delivery_failed hresult=" +
                   std::to_string(static_cast<long>(delivered)));
  }
}
const wchar_t kBridgeScript[] = LR"JS((() => {
  if (window.lw) return;
  const pending = new Map(); const listeners = new Map(); let nextId = 0;
  window.lw = Object.freeze({ platform: 'windows', invoke(method, params = {}) {
    const id = `lw-${++nextId}`;
    return new Promise((resolve, reject) => { pending.set(id, {resolve, reject}); chrome.webview.postMessage({type:'request', id, method, params}); });
  }, on(name, callback) { const set = listeners.get(name) || new Set(); set.add(callback); listeners.set(name, set); },
  off(name, callback) { listeners.get(name)?.delete(callback); } });
  chrome.webview.addEventListener('message', event => { const message = event.data;
    if (message?.type === 'response') { const item = pending.get(message.id); if (!item) return; pending.delete(message.id); message.error ? item.reject(Object.assign(new Error(message.error.message || 'Native request failed'), { code: message.error.code })) : item.resolve(message.result); return; }
    if (message?.type === 'event') for (const listener of listeners.get(message.name) || []) listener(message.payload);
  });
})();)JS";
}

WebViewHost::~WebViewHost() {
  if (webview_) { webview_->remove_WebMessageReceived(message_token_); webview_->remove_WebResourceRequested(resource_token_); webview_->remove_NavigationStarting(navigation_token_); webview_->remove_FrameNavigationStarting(frame_navigation_token_); webview_->remove_NewWindowRequested(new_window_token_); webview_->remove_PermissionRequested(permission_token_); webview_->remove_NavigationCompleted(completed_token_); webview_->remove_ProcessFailed(process_failed_token_); }
  if (controller_) controller_->Close();
}

void WebViewHost::Create(HWND window, const std::wstring& content_folder, std::shared_ptr<PdfFileGrantManager> grants, MessageHandler on_message, ReadyHandler on_ready) {
  window_ = window; grants_ = std::move(grants); on_message_ = std::move(on_message); on_ready_ = std::move(on_ready);
  const auto user_data = UserDataFolder();
  const auto creating_environment = CreateCoreWebView2EnvironmentWithOptions(nullptr, user_data.c_str(), nullptr,
    Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this, content_folder](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
      if (FAILED(result) || !environment) { NativeLogError("webview.environment.failed hresult=" + std::to_string(static_cast<long>(result))); MessageBoxW(window_, L"未找到 WebView2 Runtime。请安装 Microsoft Edge WebView2 Evergreen Runtime。", L"lw.PDF", MB_OK | MB_ICONERROR); return result; }
      environment_ = environment;
      return environment->CreateCoreWebView2Controller(window_, Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([this, content_folder](HRESULT status, ICoreWebView2Controller* controller) -> HRESULT {
        if (FAILED(status) || !controller) { NativeLogError("webview.controller.failed hresult=" + std::to_string(static_cast<long>(status))); return status; }
        controller_ = controller; controller_->get_CoreWebView2(&webview_);
        Microsoft::WRL::ComPtr<ICoreWebView2Controller4> controller4;
        // Desktop drops must stay in the native data plane. Disabling WebView
        // drops lets the Win32 host receive WM_DROPFILES and issue a FileGrant
        // URL instead of copying the complete PDF through a DOM File object.
        if (SUCCEEDED(controller_.As(&controller4))) controller4->put_AllowExternalDrop(FALSE);
        Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings; if (SUCCEEDED(webview_->get_Settings(&settings))) { settings->put_AreDevToolsEnabled(FALSE); settings->put_IsStatusBarEnabled(FALSE); settings->put_AreHostObjectsAllowed(FALSE); settings->put_IsWebMessageEnabled(TRUE); }
        Microsoft::WRL::ComPtr<ICoreWebView2_3> webview3; if (SUCCEEDED(webview_.As(&webview3))) webview3->SetVirtualHostNameToFolderMapping(L"app.lwpdf", content_folder.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        webview_->AddScriptToExecuteOnDocumentCreated(kBridgeScript, nullptr);
        webview_->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT { LPWSTR uri = nullptr; if (FAILED(args->get_Uri(&uri)) || !uri) { if (uri) CoTaskMemFree(uri); args->put_Cancel(TRUE); return S_OK; } const std::wstring value(uri); CoTaskMemFree(uri); if (!IsTrustedAppUri(value)) { args->put_Cancel(TRUE); OpenPublicWebUri(window_, value); } else { UINT64 navigation_id = 0; if (SUCCEEDED(args->get_NavigationId(&navigation_id))) app_navigation_id_ = navigation_id; } return S_OK; }).Get(), &navigation_token_);
        webview_->add_FrameNavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT { LPWSTR uri = nullptr; const bool trusted = SUCCEEDED(args->get_Uri(&uri)) && uri && IsTrustedAppUri(uri); if (uri) CoTaskMemFree(uri); if (!trusted) args->put_Cancel(TRUE); return S_OK; }).Get(), &frame_navigation_token_);
        webview_->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT { args->put_Handled(TRUE); LPWSTR uri = nullptr; if (SUCCEEDED(args->get_Uri(&uri)) && uri) OpenPublicWebUri(window_, uri); if (uri) CoTaskMemFree(uri); return S_OK; }).Get(), &new_window_token_);
        webview_->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs* args) -> HRESULT { args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK; }).Get(), &permission_token_);
        webview_->AddWebResourceRequestedFilter(L"https://file.lwpdf/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        webview_->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>([this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT { ReplyFile(environment_.Get(), args, grants_); return S_OK; }).Get(), &resource_token_);
        webview_->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT { LPWSTR source = nullptr; const bool trusted = SUCCEEDED(args->get_Source(&source)) && source && IsTrustedAppUri(source); if (source) CoTaskMemFree(source); if (!trusted) return S_OK; LPWSTR json = nullptr; if (SUCCEEDED(args->get_WebMessageAsJson(&json)) && json) { const auto request = WideToUtf8(json); CoTaskMemFree(json); if (on_message_) { const auto target = webview_; on_message_(request, [target](const std::string& reply) { if (!reply.empty()) PostJsonToTrustedWebView(target.Get(), reply); }); } } return S_OK; }).Get(), &message_token_);
        webview_->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
          UINT64 navigation_id = 0;
          if (!args || FAILED(args->get_NavigationId(&navigation_id)) ||
              navigation_id != app_navigation_id_) return S_OK;
          BOOL success = FALSE;
          COREWEBVIEW2_WEB_ERROR_STATUS web_error = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
          const auto success_result = args->get_IsSuccess(&success);
          const auto status_result = args->get_WebErrorStatus(&web_error);
          if (SUCCEEDED(success_result) && success) {
            failure_message_shown_ = false;
            NativeLogInfo("webview.ready");
            if (on_ready_) on_ready_();
          } else {
            NativeLogError("webview.navigation_failed status=" +
                           std::to_string(static_cast<int>(web_error)) +
                           " success_hresult=" +
                           std::to_string(static_cast<long>(success_result)) +
                           " status_hresult=" +
                           std::to_string(static_cast<long>(status_result)));
            if (!failure_message_shown_) {
              failure_message_shown_ = true;
              MessageBoxW(window_,
                          L"lw.PDF 界面加载失败。\n\n请重新启动应用；如果问题持续，请查看日志。",
                          L"lw.PDF", MB_OK | MB_ICONERROR);
            }
          }
          return S_OK;
        }).Get(), &completed_token_);
        webview_->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([this](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs* args) -> HRESULT {
          COREWEBVIEW2_PROCESS_FAILED_KIND kind = COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
          const auto result = args ? args->get_ProcessFailedKind(&kind) : E_POINTER;
          NativeLogError("webview.process_failed kind=" +
                         std::to_string(static_cast<int>(kind)) +
                         " hresult=" + std::to_string(static_cast<long>(result)));
          if (!failure_message_shown_) {
            failure_message_shown_ = true;
            MessageBoxW(window_,
                        L"WebView2 进程异常，lw.PDF 可能无法继续正常工作。\n\n请重新启动应用；如果问题持续，请查看日志。",
                        L"lw.PDF", MB_OK | MB_ICONERROR);
          }
          return S_OK;
        }).Get(), &process_failed_token_);
        Resize();
        const auto navigation = webview_->Navigate(L"https://app.lwpdf/index.html");
        if (FAILED(navigation)) NativeLogError("webview.navigate.failed hresult=" + std::to_string(static_cast<long>(navigation)));
        else NativeLogDebug("webview.navigation_started");
        return navigation;
      }).Get());
    }).Get());
  if (FAILED(creating_environment)) {
    NativeLogError("webview.environment.start_failed hresult=" +
                   std::to_string(static_cast<long>(creating_environment)));
  }
}

bool WebViewHost::PostJson(const std::string& message) { return PostJsonToTrustedWebView(webview_.Get(), message); }
void WebViewHost::Resize() { if (!controller_ || !window_) return; RECT bounds{}; GetClientRect(window_, &bounds); controller_->put_Bounds(bounds); }
