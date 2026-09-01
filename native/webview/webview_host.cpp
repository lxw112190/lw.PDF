#include "webview/webview_host.h"

#include "common/utf8.h"
#include "common/native_log.h"
#include "runtime/bounded_file_stream.h"
#include "runtime/atomic_pdf_writer.h"
#include "runtime/file_grant.h"
#include "runtime/http_range.h"

#include <ShlObj.h>
#include <Shlwapi.h>
#include <Shellapi.h>

#include <filesystem>
#include <objbase.h>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>

using Microsoft::WRL::Callback;
namespace {
constexpr std::wstring_view kAppOrigin = L"https://app.lwpdf/";
constexpr std::wstring_view kFileOrigin = L"https://file.lwpdf/";
constexpr std::wstring_view kSaveOrigin = L"https://save.lwpdf/";

bool StartsWithIgnoreCase(std::wstring_view value, std::wstring_view prefix) {
  return value.size() >= prefix.size() && CompareStringOrdinal(value.data(),
      static_cast<int>(prefix.size()), prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}
bool IsTrustedAppUri(std::wstring_view uri) { return StartsWithIgnoreCase(uri, kAppOrigin); }
bool IsFileUri(std::wstring_view uri) { return StartsWithIgnoreCase(uri, kFileOrigin); }
bool IsSaveUri(std::wstring_view uri) { return StartsWithIgnoreCase(uri, kSaveOrigin); }
bool IsPublicWebUri(std::wstring_view uri) {
  return (StartsWithIgnoreCase(uri, L"https://") || StartsWithIgnoreCase(uri, L"http://")) &&
      !IsTrustedAppUri(uri) && !IsFileUri(uri) && !IsSaveUri(uri);
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
std::optional<std::string> SaveIdFromUri(std::wstring_view uri) {
  if (!IsSaveUri(uri)) return std::nullopt;
  const auto rest = uri.substr(kSaveOrigin.size());
  const auto slash = rest.find(L'/');
  if (slash != 32U || rest.substr(slash) != L"/document.pdf") return std::nullopt;
  std::string id;
  id.reserve(32U);
  for (const auto character : rest.substr(0, slash)) {
    if (!((character >= L'0' && character <= L'9') || (character >= L'a' && character <= L'f'))) return std::nullopt;
    id.push_back(static_cast<char>(character));
  }
  return id;
}
std::wstring RequestMethod(ICoreWebView2WebResourceRequest* request) {
  LPWSTR value = nullptr;
  if (!request || FAILED(request->get_Method(&value)) || !value) return L"";
  std::wstring method(value); CoTaskMemFree(value); return method;
}
std::wstring RequestHeader(ICoreWebView2WebResourceRequest* request, const wchar_t* name) {
  Microsoft::WRL::ComPtr<ICoreWebView2HttpRequestHeaders> headers;
  LPWSTR value = nullptr;
  if (!request || FAILED(request->get_Headers(&headers)) || !headers ||
      FAILED(headers->GetHeader(name, &value)) || !value) return L"";
  std::wstring result(value); CoTaskMemFree(value); return result;
}
Microsoft::WRL::ComPtr<IStream> JsonStream(const std::string& body) {
  Microsoft::WRL::ComPtr<IStream> stream;
  auto* raw = SHCreateMemStream(reinterpret_cast<const BYTE*>(body.data()),
                                static_cast<UINT>(body.size()));
  if (raw) stream.Attach(raw);
  return stream;
}

struct SaveCompletion {
  HWND owner = nullptr;
  Microsoft::WRL::ComPtr<ICoreWebView2WebResourceRequestedEventArgs> args;
  Microsoft::WRL::ComPtr<ICoreWebView2Deferral> deferral;
  std::shared_ptr<PdfFileGrantManager> grants;
  std::filesystem::path output;
  std::filesystem::path temporary;
  std::string token;
  std::string error;
  bool success = false;
};

void ReplySave(HWND owner, ICoreWebView2Environment* environment,
               ICoreWebView2WebResourceRequestedEventArgs* args,
               const std::shared_ptr<PdfFileGrantManager>& grants) {
  Microsoft::WRL::ComPtr<ICoreWebView2WebResourceRequest> request;
  LPWSTR raw_uri = nullptr;
  if (!environment || !args || !grants || FAILED(args->get_Request(&request)) || !request ||
      FAILED(request->get_Uri(&raw_uri)) || !raw_uri) {
    if (raw_uri) CoTaskMemFree(raw_uri);
    return;
  }
  const auto id = SaveIdFromUri(raw_uri);
  CoTaskMemFree(raw_uri);
  if (!id) return;
  const auto method = RequestMethod(request.Get());
  const auto origin = RequestHeader(request.Get(), L"Origin");
  if (origin != L"https://app.lwpdf") {
    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
    environment->CreateWebResourceResponse(JsonStream(R"({"error":"origin_not_allowed"})").Get(),
        403, L"Forbidden", L"Content-Type: application/json\r\nAccess-Control-Allow-Origin: https://app.lwpdf",
        &response);
    args->put_Response(response.Get());
    return;
  }
  const std::wstring cors = L"Access-Control-Allow-Origin: https://app.lwpdf\r\n"
                            L"Access-Control-Allow-Methods: PUT, OPTIONS\r\n"
                            L"Access-Control-Allow-Headers: Content-Type\r\n"
                            L"Vary: Origin";
  if (method == L"OPTIONS") {
    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
    environment->CreateWebResourceResponse(nullptr, 204, L"No Content", cors.c_str(), &response);
    args->put_Response(response.Get());
    return;
  }
  if (method != L"PUT" || RequestHeader(request.Get(), L"Content-Type") != L"application/pdf") {
    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
    environment->CreateWebResourceResponse(JsonStream(R"({"error":"request_not_allowed"})").Get(),
        405, L"Method Not Allowed", (L"Content-Type: application/json\r\n" + cors).c_str(), &response);
    args->put_Response(response.Get());
    return;
  }
  Microsoft::WRL::ComPtr<IStream> content;
  if (FAILED(request->get_Content(&content)) || !content) {
    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
    environment->CreateWebResourceResponse(JsonStream(R"({"error":"request_body_missing"})").Get(),
        400, L"Bad Request", (L"Content-Type: application/json\r\n" + cors).c_str(), &response);
    args->put_Response(response.Get());
    return;
  }
  Microsoft::WRL::ComPtr<IStream> marshaled;
  if (FAILED(CoMarshalInterThreadInterfaceInStream(IID_IStream, content.Get(), &marshaled))) {
    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
    environment->CreateWebResourceResponse(JsonStream(R"({"error":"request_body_unavailable"})").Get(),
        500, L"Internal Server Error", (L"Content-Type: application/json\r\n" + cors).c_str(), &response);
    args->put_Response(response.Get());
    return;
  }
  const auto grant = grants->TakeSave(*id);
  if (!grant) {
    Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
    environment->CreateWebResourceResponse(JsonStream(R"({"error":"save_grant_invalid"})").Get(),
        404, L"Not Found", (L"Content-Type: application/json\r\n" + cors).c_str(), &response);
    args->put_Response(response.Get());
    return;
  }
  Microsoft::WRL::ComPtr<ICoreWebView2Deferral> deferral;
  if (FAILED(args->GetDeferral(&deferral)) || !deferral) return;
  auto completion = std::make_unique<SaveCompletion>();
  completion->owner = owner;
  completion->args = args; completion->deferral = deferral;
  completion->grants = grants; completion->output = grant->path;
  completion->temporary = grant->path; completion->temporary += L".lwpdf-" + std::wstring(grant->id.begin(), grant->id.end()) + L".tmp";
  completion->token = grant->id;
  auto* payload = completion.release();
  std::thread([payload, marshaled = marshaled.Detach()]() mutable {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    Microsoft::WRL::ComPtr<IStream> stream;
    const auto stream_result = CoGetInterfaceAndReleaseStream(marshaled, IID_PPV_ARGS(&stream));
    const auto write_result = FAILED(stream_result) || !stream
        ? PdfAtomicWriteResult{false, 0, "request_body_unavailable"}
        : WritePdfStreamAtomically(stream.Get(), payload->temporary, payload->output);
    payload->error = write_result.error;
    payload->success = write_result.success;
    CoUninitialize();
    if (!PostMessageW(payload->owner, WebViewHost::kSaveCompleteMessage, 0, reinterpret_cast<LPARAM>(payload))) {
      WebViewHost::DiscardSave(reinterpret_cast<LPARAM>(payload));
    }
  }).detach();
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
  if (webview_) { webview_->remove_WebMessageReceived(message_token_); webview_->remove_WebResourceRequested(resource_token_); webview_->remove_NavigationStarting(navigation_token_); webview_->remove_FrameNavigationStarting(frame_navigation_token_); webview_->remove_NewWindowRequested(new_window_token_); webview_->remove_PermissionRequested(permission_token_); webview_->remove_NavigationCompleted(completed_token_); webview_->remove_ProcessFailed(process_failed_token_); webview_->remove_DocumentTitleChanged(title_token_); }
  if (controller_) controller_->Close();
}

void WebViewHost::DiscardSave(LPARAM payload) {
  auto* completion = reinterpret_cast<SaveCompletion*>(payload);
  if (!completion) return;
  std::error_code error;
  if (!completion->temporary.empty()) std::filesystem::remove(completion->temporary, error);
  delete completion;
}

void WebViewHost::CompleteSave(LPARAM payload) {
  std::unique_ptr<SaveCompletion> completion(reinterpret_cast<SaveCompletion*>(payload));
  if (!completion) return;
  Microsoft::WRL::ComPtr<ICoreWebView2WebResourceResponse> response;
  const std::wstring headers = L"Content-Type: application/json\r\n"
                               L"Access-Control-Allow-Origin: https://app.lwpdf\r\n"
                               L"Access-Control-Allow-Methods: PUT, OPTIONS\r\n"
                               L"Access-Control-Allow-Headers: Content-Type\r\n"
                               L"Vary: Origin";
  if (completion->success) {
    try {
      const auto grant = completion->grants->Create(completion->output);
      const nlohmann::json body = {{"file", {{"id", grant.id}, {"name", grant.name},
          {"size", grant.size}, {"mime", "application/pdf"}, {"url", grant.url}}}};
      const auto stream = JsonStream(body.dump());
      environment_->CreateWebResourceResponse(stream.Get(), 201, L"Created", headers.c_str(), &response);
      NativeLogInfo("pdf.annotation.saved id=" + grant.id +
                    " size=" + std::to_string(grant.size));
    } catch (const std::exception& error) {
      completion->success = false;
      completion->error = error.what();
    }
  }
  if (!completion->success) {
    const nlohmann::json body = {{"error", completion->error.empty() ? "output_commit_failed" : completion->error}};
    const auto stream = JsonStream(body.dump());
    environment_->CreateWebResourceResponse(stream.Get(), 500, L"Internal Server Error", headers.c_str(), &response);
    NativeLogError("pdf.annotation.save_failed reason=" + completion->error);
  }
  if (response) completion->args->put_Response(response.Get());
  if (completion->deferral) completion->deferral->Complete();
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
        webview_->AddWebResourceRequestedFilter(L"https://save.lwpdf/*", COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL);
        webview_->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>([this](ICoreWebView2*, ICoreWebView2WebResourceRequestedEventArgs* args) -> HRESULT {
          LPWSTR uri = nullptr;
          Microsoft::WRL::ComPtr<ICoreWebView2WebResourceRequest> request;
          if (args && SUCCEEDED(args->get_Request(&request)) && request && SUCCEEDED(request->get_Uri(&uri)) && uri) {
            const std::wstring value(uri); CoTaskMemFree(uri);
            if (IsSaveUri(value)) ReplySave(window_, environment_.Get(), args, grants_);
            else if (IsFileUri(value)) ReplyFile(environment_.Get(), args, grants_);
          } else if (uri) CoTaskMemFree(uri);
          return S_OK;
        }).Get(), &resource_token_);
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
        webview_->add_DocumentTitleChanged(Callback<ICoreWebView2DocumentTitleChangedEventHandler>([this](ICoreWebView2*, IUnknown*) -> HRESULT {
          LPWSTR title = nullptr;
          if (webview_ && SUCCEEDED(webview_->get_DocumentTitle(&title)) && title) {
            SetWindowTextW(window_, *title ? title : L"lw.PDF");
            CoTaskMemFree(title);
          }
          return S_OK;
        }).Get(), &title_token_);
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
