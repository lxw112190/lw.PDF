#include "app/main_window.h"

#include "bridge/bridge_dispatcher.h"
#include "common/native_log.h"
#include "resources/frontend_bundle.h"
#include "runtime/file_grant.h"
#include "runtime/recent_files.h"
#include "webview/webview_host.h"

#include <Shellapi.h>
#include <ShlObj.h>

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
constexpr wchar_t kClassName[] = L"lw.PDF.MainWindow";
constexpr int kDefaultWidth = 1180;
constexpr int kDefaultHeight = 760;
constexpr int kMinimumWidth = 640;
constexpr int kMinimumHeight = 480;

std::filesystem::path WindowGeometryPath() {
  PWSTR local_app_data = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local_app_data)) || !local_app_data) {
    if (local_app_data) CoTaskMemFree(local_app_data);
    return {};
  }
  const auto path = std::filesystem::path(local_app_data) / L"lw.PDF" / L"window.json";
  CoTaskMemFree(local_app_data);
  return path;
}

struct SavedWindowGeometry { RECT rect{0, 0, kDefaultWidth, kDefaultHeight}; bool maximized = false; };

SavedWindowGeometry LoadWindowGeometry() {
  SavedWindowGeometry geometry;
  try {
    std::ifstream input(WindowGeometryPath(), std::ios::binary);
    const auto root = nlohmann::json::parse(input);
    const int x = root.value("x", 0), y = root.value("y", 0);
    const int width = root.value("width", kDefaultWidth), height = root.value("height", kDefaultHeight);
    if (!root.is_object() || root.value("version", 0) != 1 || width < kMinimumWidth || height < kMinimumHeight || width > 10000 || height > 10000) return geometry;
    geometry.rect = {x, y, x + width, y + height};
    geometry.maximized = root.value("maximized", false);
  } catch (...) {}
  return geometry;
}

SavedWindowGeometry ValidateWindowGeometry(SavedWindowGeometry geometry) {
  if (MonitorFromRect(&geometry.rect, MONITOR_DEFAULTTONULL)) return geometry;
  const auto monitor = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO info{sizeof(info)};
  if (GetMonitorInfoW(monitor, &info)) {
    const int width = std::min(geometry.rect.right - geometry.rect.left, info.rcWork.right - info.rcWork.left - 80);
    const int height = std::min(geometry.rect.bottom - geometry.rect.top, info.rcWork.bottom - info.rcWork.top - 80);
    geometry.rect.left = info.rcWork.left + 40; geometry.rect.top = info.rcWork.top + 40;
    geometry.rect.right = geometry.rect.left + width; geometry.rect.bottom = geometry.rect.top + height;
  }
  return geometry;
}

void SaveWindowGeometry(HWND window) {
  WINDOWPLACEMENT placement{sizeof(placement)};
  if (!GetWindowPlacement(window, &placement)) return;
  const auto path = WindowGeometryPath();
  if (path.empty()) return;
  try {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return;
    const auto& rect = placement.rcNormalPosition;
    const nlohmann::json root{{"version", 1}, {"x", rect.left}, {"y", rect.top}, {"width", rect.right - rect.left}, {"height", rect.bottom - rect.top}, {"maximized", IsZoomed(window) != FALSE}};
    auto temporary = path; temporary += L".tmp";
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << root.dump(2); output.close();
    if (output) MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
  } catch (...) {}
}
struct State {
  std::unique_ptr<BridgeDispatcher> bridge;
  std::shared_ptr<PdfFileGrantManager> grants;
  std::shared_ptr<RecentFileStore> recent_files;
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
      owned->recent_files =
          std::make_shared<RecentFileStore>(DefaultRecentFilesPath());
      owned->webview = std::make_unique<WebViewHost>();
      owned->bridge = std::make_unique<BridgeDispatcher>(
          window, owned->grants, owned->recent_files);
      if (launch_path) owned->bridge->SetLaunchPath(*launch_path);
      state = owned.release(); SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
      try {
        const auto folder = FrontendContentPath();
        if (!std::filesystem::exists(std::filesystem::path(folder) / L"index.html")) throw std::runtime_error("应用前端资源不完整，请重新下载 lw.PDF.exe。");
        auto* bridge = state->bridge.get(); auto* webview = state->webview.get();
        webview->Create(window, folder, state->grants,
            [bridge](const std::string& request, WebViewHost::Reply reply) { bridge->Dispatch(request, std::move(reply)); },
            [bridge, webview] {
              try {
                if (const auto event = bridge->TakeLaunchEvent(); event &&
                    !webview->PostJson(*event)) {
                  NativeLogError("application.launch_file.delivery_failed");
                }
              } catch (const std::exception& error) {
                NativeLogError(std::string("application.launch_file.failed: ") +
                               error.what());
              }
            });
      } catch (const std::exception& error) {
        NativeLogError(std::string("application.frontend.failed: ") + error.what());
        MessageBoxA(window, error.what(), "lw.PDF", MB_OK | MB_ICONERROR); PostMessageW(window, WM_CLOSE, 0, 0);
      }
      return 0;
    }
    case BridgeDispatcher::kTransformCompleteMessage:
      if (state && state->bridge) state->bridge->CompleteTransform(lparam);
      else BridgeDispatcher::DiscardTransformCompletion(lparam);
      return 0;
    case WM_SIZE: if (state) state->webview->Resize(); return 0;
    case WM_EXITSIZEMOVE: SaveWindowGeometry(window); return 0;
    case WM_DROPFILES: {
      if (!state) return 0;
      const auto drop = reinterpret_cast<HDROP>(wparam);
      const auto length = DragQueryFileW(drop, 0, nullptr, 0);
      std::wstring path(length + 1, L'\0'); DragQueryFileW(drop, 0, path.data(), length + 1); path.resize(length); DragFinish(drop);
      try {
        const auto grant = state->grants->Create(path);
        NativeLogInfo("file.drop.granted size=" + std::to_string(grant.size));
        NativeLogDebug("file.drop.grant id=" + grant.id + " name=" + grant.name);
        if (!state->webview->PostJson(nlohmann::json{{"type", "event"},
              {"name", "file.opened"}, {"payload", {{"id", grant.id},
              {"name", grant.name}, {"size", grant.size},
              {"mime", "application/pdf"}, {"url", grant.url}}}}.dump())) {
          state->grants->Revoke(grant.id);
          throw std::runtime_error("WebView is not ready for dropped file");
        }
      } catch (const std::exception& error) {
        NativeLogError(std::string("file.drop.failed: ") + error.what());
        MessageBoxW(window, L"请拖入可读取的 PDF 文件。", L"lw.PDF",
                    MB_OK | MB_ICONINFORMATION);
      }
      return 0;
    }
    case WM_DESTROY: SaveWindowGeometry(window); delete state; SetWindowLongPtrW(window, GWLP_USERDATA, 0); PostQuitMessage(0); return 0;
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
  if (!RegisterClassExW(&window_class)) {
    NativeLogError("window.register_class.failed error=" +
                   std::to_string(GetLastError()));
    return 1;
  }
  const auto geometry = ValidateWindowGeometry(LoadWindowGeometry());
  const auto window = CreateWindowExW(0, kClassName, L"lw.PDF", WS_OVERLAPPEDWINDOW,
      geometry.rect.left, geometry.rect.top, geometry.rect.right - geometry.rect.left,
      geometry.rect.bottom - geometry.rect.top, nullptr, nullptr, instance,
      const_cast<std::optional<std::wstring>*>(&launch_path));
  if (!window) {
    NativeLogError("window.create.failed error=" +
                   std::to_string(GetLastError()));
    return 1;
  }
  DragAcceptFiles(window, TRUE); ShowWindow(window, geometry.maximized ? SW_SHOWMAXIMIZED : SW_SHOW); UpdateWindow(window);
  MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
  return static_cast<int>(message.wParam);
}
