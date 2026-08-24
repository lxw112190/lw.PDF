#include "dialogs/file_dialog.h"

#include <ShObjIdl.h>

namespace {
std::optional<std::wstring> ResultPath(IFileDialog* dialog) {
  IShellItem* item = nullptr;
  if (FAILED(dialog->GetResult(&item))) return std::nullopt;
  PWSTR raw = nullptr;
  const bool success = SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) && raw;
  std::wstring path = success ? raw : L"";
  if (raw) CoTaskMemFree(raw);
  item->Release();
  return success ? std::optional<std::wstring>(std::move(path)) : std::nullopt;
}
}

std::optional<std::wstring> ChoosePdfFile(HWND owner) {
  IFileOpenDialog* dialog = nullptr;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&dialog)))) return std::nullopt;
  const COMDLG_FILTERSPEC types[] = {
      {L"PDF 文档 (*.pdf)", L"*.pdf"}, {L"所有文件 (*.*)", L"*.*"}};
  DWORD options{};
  dialog->GetOptions(&options);
  dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                     FOS_FILEMUSTEXIST | FOS_NOCHANGEDIR);
  dialog->SetTitle(L"打开 PDF 文件");
  dialog->SetFileTypes(static_cast<UINT>(std::size(types)), types);
  dialog->SetFileTypeIndex(1);
  const HRESULT shown = dialog->Show(owner);
  const auto result = shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)
      ? std::nullopt : (SUCCEEDED(shown) ? ResultPath(dialog) : std::nullopt);
  dialog->Release();
  return result;
}
