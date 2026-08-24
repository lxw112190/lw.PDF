#include "app/main_window.h"

#include "bridge/bridge_dispatcher.h"
#include "resources/frontend_bundle.h"
#include "runtime/file_grant.h"
#include "webview/webview_host.h"

#include <Shellapi.h>

#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
constexpr wchar_t kClassName[] = L"lw.PDF.MainWindow";
struct State {
  std::unique_ptr<BridgeDispatcher> bridge;
  std::shared_ptr<PdfFileGrantManager> grants;
  std::unique_ptr<WebViewHost> webview;
};

std::wstring FrontendContentPath() {
  wchar_t development_path[32768]{};
  const auto length = GetEnvironmentVariableW(L"LWPDF_FRONTEND_DIR", development_path,
      static_cast<DWORD>(std::size(development_path)));
  if (length > 0 && length < std::size(development_path)) return development_path;
  return ExtractBundledFrontend().wstring();
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  auto* state = reinterpret_cast<State*>(GetWindowLongPtrW(window, GWLP_USERDATA));
  switch (message) {
    case WM_CREATE: {
      const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
      const auto* launch_path = create ? static_cast<const std::optional<std::wstring>*>(create->lpCreateParams) : nullptr;
      auto owned = std::make_unique<State>();
      owned->grants = std::make_shared<PdfFileGrantManager>();
      owned->webview = std::make_unique<WebViewHost>();
      owned->bridge = std::make_unique<BridgeDispatcher>(window, owned->grants);
      if (launch_path) owned->bridge->SetLaunchPath(*launch_path);
      state = owned.release(); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
      try {
        const auto folder = FrontendContentPath();
        if (!std::filesystem::exists(std::filesystem::path(folder) / L"index.html")) throw std::runtime_error("应用前端资源不完整，请重新下载 lw.PDF.exe。");
        auto* bridge = state->bridge.get(); auto* webview = state->webview.get();
        webview->Create(window, folder, state->grants,
            [bridge](const std::string& request, WebViewHost::Reply reply) { bridge->Dispatch(request, std::move(reply)); },
            [bridge, webview] { if (const auto event = bridge->TakeLaunchEvent()) webview->PostJson(*event); });
      } catch (const std::exception& error) {
        MessageBoxA(window, error.what(), "lw.PDF", MB_OK | MB_ICONERROR); PostMessageW(window, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    case WM_SIZE: if (state) state->webview->Resize(); return 0;
    case WM_DROPFILES: {
      if (!state) return 0;
      const auto drop = reinterpret_cast<HDROP>(wparam);
      const auto length = DragQueryFileW(drop, 0, nullptr, 0);
      std::wstring path(length + 1, L'\0'); DragQueryFileW(drop, 0, path.data(), length + 1); path.resize(length); DragFinish(drop);
      try { const auto grant = state->grants->Create(path); state->webview->PostJson(nlohmann::json{{"type", "event"}, {"name", "file.opened"}, {"payload", {{"id", grant.id}, {"name", grant.name}, {"size", grant.size}, {"mime", "application/pdf"}, {"url", grant.url}}}}.dump()); }
      catch (...) { MessageBoxW(window, L"请拖入可读取的 PDF 文件。", L"lw.PDF", MB_OK | MB_ICONINFORMATION); }
      return 0;
    }
    case WM_DESTROY: delete state; SetWindowLongPtrW(window, GWLP_USERDATA, 0); PostQuitMessage(0); return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}
}

int RunMainWindow(HINSTANCE instance, const std::optional<std::wstring>& launch_path) {
  WNDCLASSEXW window_class{sizeof(window_class)};
  window_class.lpfnWndProc = WindowProc; window_class.hInstance = instance;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  window_class.lpszClassName = kClassName;
  window_class.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
      GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR | LR_SHARED));
  window_class.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
      GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CXSMICON), LR_DEFAULTCOLOR | LR_SHARED));
  if (!RegisterClassExW(&window_class)) return 1;
  const auto window = CreateWindowExW(0, kClassName, L"lw.PDF", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 1180, 760, nullptr, nullptr, instance,
      const_cast<std::optional<std::wstring>*>(&launch_path));
  if (!window) return 1;
  DragAcceptFiles(window, TRUE); ShowWindow(window, SW_SHOW); UpdateWindow(window);
  MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
  return static_cast<int>(message.wParam);
}
