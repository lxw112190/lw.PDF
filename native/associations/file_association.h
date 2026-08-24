#pragma once

#include <Windows.h>

#include <string>

struct FileAssociationStatus {
  bool registered = false;
  bool current = false;
  std::wstring executable_path;
  std::wstring registered_executable_path;
};

std::wstring BuildAssociationOpenCommand(const std::wstring& executable_path);
FileAssociationStatus GetPdfFileAssociationStatus();
void RegisterPdfFileAssociations();
void UnregisterPdfFileAssociations();
bool OpenDefaultAppsSettings(HWND owner);
