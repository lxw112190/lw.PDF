#include "bridge/bridge_dispatcher.h"

#include "associations/file_association.h"
#include "common/utf8.h"
#include "dialogs/file_dialog.h"
#include "runtime/file_grant.h"

#include <Shellapi.h>
#include <nlohmann/json.hpp>

#include <stdexcept>

using nlohmann::json;
namespace {
constexpr char kRepositoryUrl[] = "https://github.com/lxw112190/lw.PDF";
constexpr char kLatestReleaseUrl[] = "https://github.com/lxw112190/lw.PDF/releases/latest";

json Success(const std::string& id, json result) {
  return {{"type", "response"}, {"id", id}, {"result", std::move(result)}};
}
json Error(const std::string& id, const char* code, const char* message) {
  return {{"type", "response"}, {"id", id},
          {"error", {{"code", code}, {"message", message}}}};
}
json PublicGrant(const PdfFileGrant& grant) {
  return {{"id", grant.id}, {"name", grant.name}, {"size", grant.size},
          {"mime", "application/pdf"}, {"url", grant.url}};
}
bool EmptyObject(const json& value) { return value.is_object() && value.empty(); }
}

BridgeDispatcher::BridgeDispatcher(HWND owner, std::shared_ptr<PdfFileGrantManager> grants)
    : owner_(owner), grants_(std::move(grants)) {}

void BridgeDispatcher::SetLaunchPath(const std::optional<std::wstring>& path) {
  launch_path_ = path;
}

std::optional<std::string> BridgeDispatcher::TakeLaunchEvent() {
  if (!launch_path_) return std::nullopt;
  const auto path = *launch_path_;
  launch_path_.reset();
  const auto grant = grants_->Create(path);
  return json{{"type", "event"}, {"name", "file.opened"},
              {"payload", PublicGrant(grant)}}.dump();
}

void BridgeDispatcher::Dispatch(const std::string& request, Reply reply) {
  std::string id;
  try {
    const auto value = json::parse(request);
    if (!value.is_object() || value.value("type", "") != "request" ||
        !value.contains("id") || !value.at("id").is_string() ||
        !value.contains("method") || !value.at("method").is_string()) {
      reply(Error("", "BRIDGE_INVALID_REQUEST", "Invalid bridge request").dump());
      return;
    }
    id = value.at("id").get<std::string>();
    const auto method = value.at("method").get<std::string>();
    const auto params = value.value("params", json::object());
    if (id.empty() || id.size() > 128 || method.empty() || method.size() > 64 ||
        !params.is_object()) {
      reply(Error(id, "BRIDGE_INVALID_REQUEST", "Invalid bridge request").dump());
      return;
    }
    if (method == "dialog.openFile") {
      const auto path = ChoosePdfFile(owner_);
      if (!path) { reply(Success(id, {{"files", json::array()}}).dump()); return; }
      const auto grant = grants_->Create(*path);
      reply(Success(id, {{"files", json::array({PublicGrant(grant)})}}).dump());
      return;
    }
    if (method == "file.revoke") {
      if (!params.contains("id") || !params.at("id").is_string()) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid file permission").dump()); return;
      }
      grants_->Revoke(params.at("id").get<std::string>());
      reply(Success(id, nullptr).dump()); return;
    }
    if (method == "association.status") {
      if (!EmptyObject(params)) { reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid association parameters").dump()); return; }
      const auto status = GetPdfFileAssociationStatus();
      reply(Success(id, {{"registered", status.registered}, {"current", status.current},
          {"executablePath", WideToUtf8(status.executable_path)},
          {"registeredExecutablePath", status.registered_executable_path.empty()
              ? json(nullptr) : json(WideToUtf8(status.registered_executable_path))}}).dump());
      return;
    }
    if (method == "association.register") { RegisterPdfFileAssociations(); reply(Success(id, nullptr).dump()); return; }
    if (method == "association.unregister") { UnregisterPdfFileAssociations(); reply(Success(id, nullptr).dump()); return; }
    if (method == "association.openDefaultApps") {
      if (!OpenDefaultAppsSettings(owner_)) { reply(Error(id, "DEFAULT_APPS_OPEN_FAILED", "Cannot open Windows default apps settings").dump()); return; }
      reply(Success(id, nullptr).dump()); return;
    }
    if (method == "app.openExternal") {
      const auto url = params.value("url", "");
      if (url != kRepositoryUrl && url != kLatestReleaseUrl) { reply(Error(id, "EXTERNAL_URL_BLOCKED", "External URL is not allowed").dump()); return; }
      if (reinterpret_cast<INT_PTR>(ShellExecuteW(owner_, L"open", Utf8ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32) {
        reply(Error(id, "EXTERNAL_OPEN_FAILED", "Cannot open default browser").dump()); return;
      }
      reply(Success(id, nullptr).dump()); return;
    }
    reply(Error(id, "BRIDGE_UNKNOWN_METHOD", "Unknown desktop method").dump());
  } catch (const std::exception&) {
    reply(Error(id, "BRIDGE_OPERATION_FAILED", "Native operation failed").dump());
  }
}
