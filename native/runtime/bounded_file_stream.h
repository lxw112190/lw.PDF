#pragma once

#include <Windows.h>
#include <ObjIdl.h>

#include <cstdint>
#include <filesystem>

HRESULT OpenBoundedFileStream(const std::filesystem::path& path,
                              std::uintmax_t offset,
                              std::uintmax_t length,
                              IStream** stream);
