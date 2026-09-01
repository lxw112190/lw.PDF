#include "bridge/bridge_dispatcher.h"

#include "associations/file_association.h"
#include "common/native_log.h"
#include "common/utf8.h"
#include "dialogs/file_dialog.h"
#include "pdf/pdf_page_editor.h"
#include "pdf/pdf_transformer.h"
#include "runtime/file_grant.h"
#include "runtime/recent_files.h"

#include <Shellapi.h>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <thread>
#include <utility>

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

struct TransformCompletion {
  std::string id;
  std::string kind;
  std::wstring output_path;
  BridgeDispatcher::Reply reply;
  PdfTransformResult result;
  ULONGLONG duration_ms = 0;
};

struct PageEditCompletion {
  std::string id;
  std::wstring output_path;
  std::uint32_t final_page_number = 1;
  BridgeDispatcher::Reply reply;
  PdfPageEditResult result;
  ULONGLONG duration_ms = 0;
};
}

BridgeDispatcher::BridgeDispatcher(
    HWND owner, std::shared_ptr<PdfFileGrantManager> grants,
    std::shared_ptr<RecentFileStore> recent_files)
    : owner_(owner),
      grants_(std::move(grants)),
      recent_files_(std::move(recent_files)) {}

void BridgeDispatcher::SetLaunchPath(const std::optional<std::wstring>& path) {
  launch_path_ = path;
}

std::optional<std::string> BridgeDispatcher::TakeLaunchEvent() {
  if (!launch_path_) return std::nullopt;
  const auto path = *launch_path_;
  launch_path_.reset();
  const auto grant = grants_->Create(path);
  NativeLogDebug("file.launch.grant id=" + grant.id + " name=" + grant.name +
                 " size=" + std::to_string(grant.size));
  return json{{"type", "event"}, {"name", "file.opened"},
              {"payload", PublicGrant(grant)}}.dump();
}

void BridgeDispatcher::DiscardTransformCompletion(LPARAM payload) {
  delete reinterpret_cast<TransformCompletion*>(payload);
}

void BridgeDispatcher::CompleteTransform(LPARAM payload) {
  std::unique_ptr<TransformCompletion> completion(
      reinterpret_cast<TransformCompletion*>(payload));
  transform_busy_ = false;
  if (completion->result.error != PdfTransformError::None) {
    const char* code = "PDF_TRANSFORM_FAILED";
    const char* message = "PDF transform failed";
    switch (completion->result.error) {
      case PdfTransformError::SourceUnavailable:
        code = "PDF_SOURCE_UNAVAILABLE";
        message = "PDF source is no longer available";
        break;
      case PdfTransformError::PasswordRequired:
        code = "PDF_PASSWORD_REQUIRED";
        message = "Password-protected PDFs are not supported yet";
        break;
      case PdfTransformError::PageRangeInvalid:
        code = "PDF_PAGE_RANGE_INVALID";
        message = "Invalid page range";
        break;
      case PdfTransformError::OutputWriteFailed:
        code = "PDF_OUTPUT_WRITE_FAILED";
        message = "Cannot write the output PDF";
        break;
      case PdfTransformError::SameFile:
        message = "无法覆盖原文件，请选择其他保存位置。";
        break;
      default:
        break;
    }
    NativeLogError("pdf.transform.failed kind=" + completion->kind +
                   " error=" + std::to_string(static_cast<int>(completion->result.error)) +
                   " duration_ms=" + std::to_string(completion->duration_ms));
    completion->reply(Error(completion->id, code, message).dump());
    return;
  }
  try {
    const auto new_grant = grants_->Create(completion->output_path);
    NativeLogInfo("pdf.transform.completed kind=" + completion->kind +
                  " pages=" + std::to_string(completion->result.page_count) +
                  " size=" + std::to_string(new_grant.size) +
                  " duration_ms=" + std::to_string(completion->duration_ms));
    completion->reply(Success(completion->id,
                              {{"cancelled", false}, {"file", PublicGrant(new_grant)}}).dump());
  } catch (const std::exception& error) {
    NativeLogError("pdf.transform.grant.failed: " + std::string(error.what()));
    completion->reply(Error(completion->id, "PDF_OUTPUT_WRITE_FAILED",
                            "Cannot access the transformed PDF").dump());
  }
}

void BridgeDispatcher::DiscardPageEditCompletion(LPARAM payload) {
  delete reinterpret_cast<PageEditCompletion*>(payload);
}

void BridgeDispatcher::CompletePageEdit(LPARAM payload) {
  std::unique_ptr<PageEditCompletion> completion(
      reinterpret_cast<PageEditCompletion*>(payload));
  transform_busy_ = false;
  if (completion->result.error != PdfPageEditError::None) {
    const char* code = "PDF_PAGE_EDIT_FAILED";
    const char* message = "页面整理失败，请确认文件未损坏。";
    switch (completion->result.error) {
      case PdfPageEditError::SourceUnavailable:
        code = "PDF_SOURCE_UNAVAILABLE";
        message = "当前 PDF 文件权限已失效，请重新打开文件后再试。";
        break;
      case PdfPageEditError::InvalidPlan:
        code = "PDF_PAGE_PLAN_INVALID";
        message = "页面整理方案无效，请重新操作。";
        break;
      case PdfPageEditError::PasswordRequired:
        code = "PDF_PASSWORD_REQUIRED";
        message = "当前版本暂不支持整理受密码保护的 PDF。";
        break;
      case PdfPageEditError::PermissionDenied:
        code = "PDF_PAGE_EDIT_NOT_ALLOWED";
        message = "当前 PDF 禁止页面整理。";
        break;
      case PdfPageEditError::OutputWriteFailed:
        code = "PDF_OUTPUT_WRITE_FAILED";
        message = "无法写入目标文件，请检查磁盘空间或文件权限。";
        break;
      case PdfPageEditError::SameFile:
        code = "PDF_SAME_FILE";
        message = "无法覆盖原文件，请选择其他保存位置。";
        break;
      default:
        break;
    }
    NativeLogError("pdf.page_edit.failed error=" +
                   std::to_string(static_cast<int>(completion->result.error)) +
                   " duration_ms=" + std::to_string(completion->duration_ms));
    completion->reply(Error(completion->id, code, message).dump());
    return;
  }
  try {
    const auto new_grant = grants_->Create(completion->output_path);
    NativeLogInfo("pdf.page_edit.completed pages=" +
                  std::to_string(completion->result.page_count) +
                  " size=" + std::to_string(new_grant.size) +
                  " duration_ms=" + std::to_string(completion->duration_ms));
    completion->reply(Success(completion->id,
                              {{"cancelled", false},
                               {"file", PublicGrant(new_grant)},
                               {"pageNumber", completion->final_page_number}}).dump());
  } catch (const std::exception& error) {
    NativeLogError("pdf.page_edit.grant.failed: " + std::string(error.what()));
    completion->reply(Error(completion->id, "PDF_OUTPUT_WRITE_FAILED",
                            "Cannot access the organized PDF").dump());
  }
}

void BridgeDispatcher::Dispatch(const std::string& request, Reply reply) {
  std::string id;
  std::string method = "unparsed";
  try {
    const auto value = json::parse(request);
    if (!value.is_object() || value.value("type", "") != "request" ||
        !value.contains("id") || !value.at("id").is_string() ||
        !value.contains("method") || !value.at("method").is_string()) {
      reply(Error("", "BRIDGE_INVALID_REQUEST", "Invalid bridge request").dump());
      return;
    }
    id = value.at("id").get<std::string>();
    method = value.at("method").get<std::string>();
    const auto params = value.value("params", json::object());
    if (id.empty() || id.size() > 128 || method.empty() || method.size() > 64 ||
        !params.is_object()) {
      reply(Error(id, "BRIDGE_INVALID_REQUEST", "Invalid bridge request").dump());
      return;
    }
    NativeLogDebug("bridge.request method=" + method + " id=" + id);
    if (method == "dialog.openFile") {
      const auto path = ChoosePdfFile(owner_);
      if (!path) { reply(Success(id, {{"files", json::array()}}).dump()); return; }
      const auto grant = grants_->Create(*path);
      NativeLogDebug("file.dialog.grant id=" + grant.id + " name=" + grant.name +
                     " size=" + std::to_string(grant.size));
      reply(Success(id, {{"files", json::array({PublicGrant(grant)})}}).dump());
      return;
    }
    if (method == "file.revoke") {
      if (!params.contains("id") || !params.at("id").is_string()) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid file permission").dump()); return;
      }
      const auto grant_id = params.at("id").get<std::string>();
      grants_->Revoke(grant_id);
      NativeLogDebug("file.grant.revoked id=" + grant_id);
      reply(Success(id, nullptr).dump()); return;
    }
    if (method == "recent.list") {
      if (!EmptyObject(params)) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid recent file parameters").dump());
        return;
      }
      auto files = json::array();
      for (const auto& file : recent_files_->List()) {
        files.push_back({{"id", file.id}, {"name", file.name},
                         {"lastOpened", file.last_opened}});
      }
      reply(Success(id, {{"files", std::move(files)}}).dump());
      return;
    }
    if (method == "app.documentDirty") {
      if (!params.contains("dirty") || !params.at("dirty").is_boolean()) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid document state").dump());
        return;
      }
      if ((params.contains("annotationDirty") && !params.at("annotationDirty").is_boolean()) ||
          (params.contains("organizerDirty") && !params.at("organizerDirty").is_boolean())) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid document state details").dump());
        return;
      }
      dirty_ = params.at("dirty").get<bool>();
      if (params.contains("annotationDirty") && params.at("annotationDirty").is_boolean()) {
        annotation_dirty_ = params.at("annotationDirty").get<bool>();
      }
      if (params.contains("organizerDirty") && params.at("organizerDirty").is_boolean()) {
        organizer_dirty_ = params.at("organizerDirty").get<bool>();
      }
      if (!dirty_) {
        annotation_dirty_ = false;
        organizer_dirty_ = false;
      }
      NativeLogDebug(std::string("document.dirty=") + (dirty_ ? "true" : "false"));
      reply(Success(id, nullptr).dump());
      return;
    }
    if (method == "recent.confirmOpen") {
      if (!params.contains("grantId") || !params.at("grantId").is_string()) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid file permission").dump());
        return;
      }
      const auto grant_id = params.at("grantId").get<std::string>();
      const auto grant = grants_->Find(grant_id);
      if (!grant) {
        reply(Error(id, "FILE_GRANT_NOT_FOUND", "File permission is no longer available").dump());
        return;
      }
      const auto recent = recent_files_->Confirm(*grant);
      NativeLogDebug("recent.confirmed id=" + recent.id + " name=" + recent.name);
      reply(Success(id, {{"file", {{"id", recent.id}, {"name", recent.name},
          {"lastOpened", recent.last_opened}}}}).dump());
      return;
    }
    if (method == "recent.open") {
      if (!params.contains("id") || !params.at("id").is_string()) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid recent file").dump());
        return;
      }
      const auto recent_id = params.at("id").get<std::string>();
      const auto path = recent_files_->Resolve(recent_id);
      if (!path) {
        reply(Error(id, "RECENT_FILE_UNAVAILABLE", "Recent PDF file is unavailable").dump());
        return;
      }
      const auto grant = grants_->Create(*path);
      NativeLogDebug("recent.open.grant recent_id=" + recent_id +
                     " grant_id=" + grant.id + " name=" + grant.name);
      reply(Success(id, {{"file", PublicGrant(grant)}}).dump());
      return;
    }
    if (method == "recent.clear") {
      if (!EmptyObject(params)) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid recent file parameters").dump());
        return;
      }
      recent_files_->Clear();
      NativeLogInfo("recent.cleared");
      reply(Success(id, nullptr).dump());
      return;
    }
    if (method == "pdf.pageEditSaveAs") {
      if (annotation_dirty_) {
        reply(Error(id, "PDF_UNSAVED_ANNOTATIONS",
                    "Save annotations before organizing this PDF").dump());
        return;
      }
      if (transform_busy_) {
        reply(Error(id, "PDF_PAGE_EDIT_FAILED", "页面整理正在进行中。").dump());
        return;
      }
      if (!params.contains("sourceGrantId") || !params.at("sourceGrantId").is_string() ||
          !params.contains("plan") || !params.at("plan").is_object() ||
          !params.at("plan").contains("pages") ||
          !params.at("plan").at("pages").is_array()) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid page plan").dump());
        return;
      }
      const auto plan_json = params.at("plan").at("pages");
      if (plan_json.empty() || plan_json.size() > 100000U) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid page plan size").dump());
        return;
      }
      if ((params.contains("focusedSourcePage") &&
           !params.at("focusedSourcePage").is_number_unsigned())) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid focused page").dump());
        return;
      }
      PdfPageEditRequest page_request;
      page_request.pages.reserve(plan_json.size());
      for (const auto& item : plan_json) {
        if (!item.is_object() || !item.contains("sourcePage") ||
            !item.at("sourcePage").is_number_unsigned() ||
            item.at("sourcePage").get<std::uint64_t>() < 1 ||
            item.at("sourcePage").get<std::uint64_t>() > UINT32_MAX ||
            !item.contains("rotation") || !item.at("rotation").is_number_unsigned() ||
            item.at("rotation").get<std::uint64_t>() > 270U) {
          reply(Error(id, "PDF_PAGE_PLAN_INVALID", "Invalid page plan item").dump());
          return;
        }
        const auto rotation = item.at("rotation").get<std::uint16_t>();
        if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
          reply(Error(id, "PDF_PAGE_PLAN_INVALID", "Invalid page rotation").dump());
          return;
        }
        page_request.pages.push_back({item.at("sourcePage").get<std::uint32_t>(), rotation});
      }
      std::uint32_t focused_source_page = 1;
      if (params.contains("focusedSourcePage")) {
        if (!params.at("focusedSourcePage").is_number_unsigned() ||
            params.at("focusedSourcePage").get<std::uint64_t>() < 1 ||
            params.at("focusedSourcePage").get<std::uint64_t>() > UINT32_MAX) {
          reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid focused page").dump());
          return;
        }
        focused_source_page = params.at("focusedSourcePage").get<std::uint32_t>();
      }
      const auto grant = grants_->Find(params.at("sourceGrantId").get<std::string>());
      if (!grant) {
        reply(Error(id, "PDF_SOURCE_UNAVAILABLE", "PDF source is no longer available").dump());
        return;
      }
      const auto suggested = grant->path.stem().wstring() + L"_整理.pdf";
      const auto output_path = ChoosePdfSavePath(owner_, suggested);
      if (!output_path) {
        NativeLogInfo("pdf.page_edit.cancelled");
        reply(Success(id, {{"cancelled", true}}).dump());
        return;
      }
      std::error_code source_error;
      const auto normalized_output = std::filesystem::absolute(*output_path, source_error).lexically_normal();
      source_error.clear();
      const auto normalized_source = std::filesystem::absolute(grant->path, source_error).lexically_normal();
      if (!source_error && _wcsicmp(normalized_output.c_str(), normalized_source.c_str()) == 0) {
        reply(Error(id, "PDF_SAME_FILE", "无法覆盖原文件，请选择其他保存位置。").dump());
        return;
      }
      transform_busy_ = true;
      const auto started = GetTickCount64();
      auto completion = std::make_unique<PageEditCompletion>();
      completion->id = id;
      completion->output_path = *output_path;
      completion->reply = std::move(reply);
      const auto worker_owner = owner_;
      const auto input_path = grant->path.wstring();
      std::thread([worker_owner, input_path, page_request, focused_source_page, started,
                   completion = std::move(completion)]() mutable {
        completion->result = EditPdfPages(input_path, completion->output_path, page_request);
        completion->duration_ms = GetTickCount64() - started;
        if (completion->result.error == PdfPageEditError::None) {
          for (std::size_t index = 0; index < page_request.pages.size(); ++index) {
            if (page_request.pages[index].source_page == focused_source_page) {
              completion->final_page_number = static_cast<std::uint32_t>(index + 1U);
              break;
            }
          }
        }
        auto* payload = completion.release();
        if (!PostMessageW(worker_owner, BridgeDispatcher::kPageEditCompleteMessage, 0,
                          reinterpret_cast<LPARAM>(payload))) {
          BridgeDispatcher::DiscardPageEditCompletion(reinterpret_cast<LPARAM>(payload));
        }
      }).detach();
      return;
    }
    if (method == "pdf.transformSaveAs") {
      if (dirty_) {
        reply(Error(id, "PDF_UNSAVED_ANNOTATIONS",
                    "Save annotations before transforming this PDF").dump());
        return;
      }
      if (transform_busy_) {
        reply(Error(id, "PDF_TRANSFORM_FAILED", "PDF transform already in progress").dump());
        return;
      }
      if (!params.contains("sourceGrantId") || !params.at("sourceGrantId").is_string() ||
          !params.contains("operation") || !params.at("operation").is_object()) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid transform request").dump());
        return;
      }
      const auto grant_id = params.at("sourceGrantId").get<std::string>();
      const auto operation = params.at("operation");
      const auto kind = operation.value("kind", "");
      PdfTransformRequest transform_request;
      std::wstring name_suffix;
      if (kind == "reversePages") {
        transform_request.kind = PdfTransformKind::ReversePages;
        name_suffix = L"_倒序";
      } else if (kind == "rotatePages") {
        transform_request.kind = PdfTransformKind::RotatePages;
        name_suffix = L"_旋转";
        const auto direction = operation.value("direction", "");
        if (direction == "left90") transform_request.rotation = PdfRotation::Left90;
        else if (direction == "right90") transform_request.rotation = PdfRotation::Right90;
        else if (direction == "rotate180") transform_request.rotation = PdfRotation::Rotate180;
        else {
          reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid rotation direction").dump());
          return;
        }
        if (!operation.contains("pages") || !operation.at("pages").is_object()) {
          reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid page selection").dump());
          return;
        }
        const auto pages = operation.at("pages");
        const auto pages_kind = pages.value("kind", "");
        if (pages_kind == "all") {
          transform_request.pages.kind = PdfPageSelectionKind::All;
        } else if (pages_kind == "single") {
          if (!pages.contains("page") || !pages.at("page").is_number_unsigned() ||
              pages.at("page").get<std::uint64_t>() < 1 ||
              pages.at("page").get<std::uint64_t>() > UINT32_MAX) {
            reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid page number").dump());
            return;
          }
          transform_request.pages.kind = PdfPageSelectionKind::Single;
          transform_request.pages.page = pages.at("page").get<std::uint32_t>();
        } else if (pages_kind == "range") {
          if (!pages.contains("value") || !pages.at("value").is_string() ||
              pages.at("value").get<std::string>().empty()) {
            reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid page range").dump());
            return;
          }
          transform_request.pages.kind = PdfPageSelectionKind::Range;
          transform_request.pages.range = pages.at("value").get<std::string>();
        } else {
          reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid page selection kind").dump());
          return;
        }
      } else {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid transform kind").dump());
        return;
      }
      const auto grant = grants_->Find(grant_id);
      if (!grant) {
        reply(Error(id, "PDF_SOURCE_UNAVAILABLE", "PDF source is no longer available").dump());
        return;
      }
      transform_busy_ = true;
      const auto started = GetTickCount64();
      NativeLogInfo("pdf.transform.start kind=" + kind);
      const auto suggested = grant->path.stem().wstring() + name_suffix + L".pdf";
      const auto output_path = ChoosePdfSavePath(owner_, suggested);
      if (!output_path) {
        transform_busy_ = false;
        NativeLogInfo("pdf.transform.cancelled kind=" + kind);
        reply(Success(id, {{"cancelled", true}}).dump());
        return;
      }
      auto completion = std::make_unique<TransformCompletion>(TransformCompletion{
          id, kind, *output_path, std::move(reply), {}, 0});
      const auto input_path = grant->path.wstring();
      const auto worker_owner = owner_;
      std::thread([worker_owner, input_path, transform_request, started,
                   completion = std::move(completion)]() mutable {
        completion->result = TransformPdf(input_path, completion->output_path, transform_request);
        completion->duration_ms = GetTickCount64() - started;
        auto* payload = completion.release();
        if (!PostMessageW(worker_owner, BridgeDispatcher::kTransformCompleteMessage, 0,
                          reinterpret_cast<LPARAM>(payload))) {
          BridgeDispatcher::DiscardTransformCompletion(reinterpret_cast<LPARAM>(payload));
        }
      }).detach();
      return;
    }
    if (method == "pdf.annotationSaveGrant") {
      if (!params.contains("sourceGrantId") || !params.at("sourceGrantId").is_string()) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid PDF source").dump());
        return;
      }
      const auto source_id = params.at("sourceGrantId").get<std::string>();
      const auto source = grants_->Find(source_id);
      if (!source) {
        reply(Error(id, "PDF_SOURCE_UNAVAILABLE", "PDF source is no longer available").dump());
        return;
      }
      const auto suggested = source->path.stem().wstring() + L"_批注.pdf";
      const auto output_path = ChoosePdfSavePath(owner_, suggested);
      if (!output_path) {
        NativeLogInfo("pdf.annotation.save_cancelled");
        reply(Success(id, {{"cancelled", true}}).dump());
        return;
      }
      std::error_code path_error;
      const auto normalized_output = std::filesystem::absolute(*output_path, path_error).lexically_normal();
      const auto normalized_source = std::filesystem::absolute(source->path, path_error).lexically_normal();
      if (!path_error && _wcsicmp(normalized_output.c_str(), normalized_source.c_str()) == 0) {
        NativeLogError("pdf.annotation.save_rejected same_file");
        reply(Error(id, "PDF_SAME_FILE", "无法覆盖原文件，请选择其他保存位置。").dump());
        return;
      }
      const auto grant = grants_->CreateSave(*output_path);
      NativeLogInfo("pdf.annotation.save_grant.created id=" + grant.id);
      reply(Success(id, {{"cancelled", false}, {"token", grant.id},
                         {"url", grant.url}}).dump());
      return;
    }
    if (method == "pdf.annotationRevokeSaveGrant") {
      if (!params.contains("token") || !params.at("token").is_string()) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid save permission").dump());
        return;
      }
      grants_->RevokeSave(params.at("token").get<std::string>());
      reply(Success(id, nullptr).dump());
      return;
    }
    if (method == "association.status") {
      if (!EmptyObject(params)) { reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid association parameters").dump()); return; }
      const auto status = GetPdfFileAssociationStatus();
      reply(Success(id, {{"registered", status.registered}, {"current", status.current},
          {"defaultApplication", status.default_application},
          {"executablePath", WideToUtf8(status.executable_path)},
          {"registeredExecutablePath", status.registered_executable_path.empty()
              ? json(nullptr) : json(WideToUtf8(status.registered_executable_path))}}).dump());
      return;
    }
    if (method == "association.register") { RegisterPdfFileAssociations(); NativeLogInfo("association.registered"); reply(Success(id, nullptr).dump()); return; }
    if (method == "association.unregister") { UnregisterPdfFileAssociations(); NativeLogInfo("association.unregistered"); reply(Success(id, nullptr).dump()); return; }
    if (method == "association.openDefaultApps") {
      if (!OpenDefaultAppsSettings(owner_)) { reply(Error(id, "DEFAULT_APPS_OPEN_FAILED", "Cannot open Windows default apps settings").dump()); return; }
      reply(Success(id, nullptr).dump()); return;
    }
    if (method == "diagnostics.error") {
      const auto area = params.value("area", "");
      const auto message = params.value("message", "");
      if (area != "pdf.open" || message.empty() || message.size() > 1000U) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid diagnostic event").dump());
        return;
      }
      NativeLogError("frontend." + area + ".failed: " + message);
      reply(Success(id, nullptr).dump());
      return;
    }
    if (method == "diagnostics.info") {
      const auto area = params.value("area", "");
      const auto message = params.value("message", "");
      if (area != "pdf.open" || message.empty() || message.size() > 200U) {
        reply(Error(id, "BRIDGE_INVALID_PARAMS", "Invalid diagnostic event").dump());
        return;
      }
      NativeLogInfo("frontend." + area + ".ready " + message);
      reply(Success(id, nullptr).dump());
      return;
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
  } catch (const std::exception& error) {
    NativeLogError("bridge." + method + ".failed: " + error.what());
    reply(Error(id, "BRIDGE_OPERATION_FAILED", "Native operation failed").dump());
  } catch (...) {
    NativeLogError("bridge." + method + ".failed: unknown exception");
    reply(Error(id, "BRIDGE_OPERATION_FAILED", "Native operation failed").dump());
  }
}
