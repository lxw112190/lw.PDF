cmake_minimum_required(VERSION 3.22)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/package.json")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/package.json" LWPDF_PACKAGE_JSON)
string(JSON LWPDF_PACKAGE_VERSION GET "${LWPDF_PACKAGE_JSON}" version)
project(lw_PDF VERSION "${LWPDF_PACKAGE_VERSION}" LANGUAGES CXX)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "lw.PDF Native integration supports Windows x64 only")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# FetchContent dependencies declare cmake_minimum_required below 3.15, which
# would ignore CMAKE_MSVC_RUNTIME_LIBRARY under CMP0091 OLD and mix /MD into
# /MT targets. Keep the static runtime effective for every subproject.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
# qpdf headers and sources are UTF-8; without /utf-8 MSVC parses them with
# the active code page (e.g. GBK on Chinese systems) and breaks the build.
if(MSVC)
  add_compile_options(/utf-8)
endif()
include(CTest)
# Keep FetchContent subprojects from building their own test executables;
# restore the caller's choice after dependencies are configured.
set(LWPDF_BUILD_TESTING ${BUILD_TESTING})
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

include(FetchContent)
include(ExternalProject)
FetchContent_Declare(nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa)
FetchContent_MakeAvailable(nlohmann_json)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/miniz.tar.gz")
  set(LWPDF_MINIZ_URL "${CMAKE_CURRENT_SOURCE_DIR}/.deps/miniz.tar.gz")
else()
  set(LWPDF_MINIZ_URL "https://github.com/richgel999/miniz/archive/refs/tags/3.1.0.tar.gz")
endif()
FetchContent_Declare(miniz URL "${LWPDF_MINIZ_URL}"
  URL_HASH SHA256=09569fc19d060ac9f5999ba9356728c2494ebe6a24ac0eb0a6b6ae3d396cfea6)
FetchContent_MakeAvailable(miniz)

# libqpdf performs the PDF structural transformations (reverse pages,
# rotate pages) without re-encoding page content. qpdf locates zlib and
# libjpeg through find_path/find_library, so pre-seed the cache entries
# with the FetchContent targets; the genex paths resolve at generate time
# once the static libraries exist. .deps/ is a local download cache, not
# tracked by git (same convention as miniz).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/zlib-1.3.1.tar.gz")
  set(LWPDF_ZLIB_URL "${CMAKE_CURRENT_SOURCE_DIR}/.deps/zlib-1.3.1.tar.gz")
else()
  set(LWPDF_ZLIB_URL "https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz")
endif()
FetchContent_Declare(zlib URL "${LWPDF_ZLIB_URL}"
  URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23)
set(ZLIB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(zlib)
# zlib.h lives in the source dir while CMake generates zconf.h into the
# build dir; qpdf needs both on its include path.
set(ZLIB_H_PATH "${zlib_SOURCE_DIR};${zlib_BINARY_DIR}"
    CACHE PATH "zlib headers for qpdf" FORCE)
set(ZLIB_LIB_PATH "$<TARGET_FILE:zlibstatic>" CACHE PATH "zlib library for qpdf" FORCE)

# libjpeg-turbo forbids add_subdirectory() integration, so it is built as an
# ExternalProject and installed per config to keep /MT Debug and Release
# artifacts apart (its library is named jpeg.lib in both configs).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/libjpeg-turbo-3.0.4.tar.gz")
  set(LWPDF_JPEG_URL "${CMAKE_CURRENT_SOURCE_DIR}/.deps/libjpeg-turbo-3.0.4.tar.gz")
else()
  set(LWPDF_JPEG_URL "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/3.0.4/libjpeg-turbo-3.0.4.tar.gz")
endif()
set(LWPDF_JPEG_INSTALL "${CMAKE_CURRENT_BINARY_DIR}/jpeg-install")
ExternalProject_Add(lwpdf_libjpeg
  URL "${LWPDF_JPEG_URL}"
  URL_HASH SHA256=99130559e7d62e8d695f2c0eaeef912c5828d5b84a0537dcb24c9678c9d5b76b
  PREFIX "${CMAKE_CURRENT_BINARY_DIR}/_deps/lwpdf-libjpeg"
  CMAKE_ARGS
    -DENABLE_SHARED=OFF
    -DENABLE_STATIC=ON
    -DWITH_TURBOJPEG=OFF
    -DWITH_JAVA=OFF
    -DWITH_SIMD=OFF
    -DWITH_TOOLS=OFF
    -DBUILD_TESTING=OFF
    -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
    -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>
    -DCMAKE_INSTALL_PREFIX=${LWPDF_JPEG_INSTALL}/$<CONFIG>
  BUILD_BYPRODUCTS "${LWPDF_JPEG_INSTALL}/$<CONFIG>/lib/jpeg-static.lib")
set(LIBJPEG_H_PATH "${LWPDF_JPEG_INSTALL}/$<CONFIG>/include"
    CACHE PATH "libjpeg headers for qpdf" FORCE)
set(LIBJPEG_LIB_PATH "${LWPDF_JPEG_INSTALL}/$<CONFIG>/lib/jpeg-static.lib"
    CACHE PATH "libjpeg library for qpdf" FORCE)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/qpdf-12.2.0.tar.gz")
  set(LWPDF_QPDF_URL "${CMAKE_CURRENT_SOURCE_DIR}/.deps/qpdf-12.2.0.tar.gz")
else()
  set(LWPDF_QPDF_URL "https://github.com/qpdf/qpdf/releases/download/v12.2.0/qpdf-12.2.0.tar.gz")
endif()
FetchContent_Declare(qpdf EXCLUDE_FROM_ALL
  URL "${LWPDF_QPDF_URL}"
  URL_HASH SHA256=b3d1575b2218badc3549d6977524bb0f8c468c6528eebc8967bbe3078cf2cace)
# qpdf 12.2.0 is the last release that builds as C++17; only the static
# library is linked.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_DOC OFF CACHE BOOL "" FORCE)
# EXCLUDE_FROM_ALL: qpdf's CLI/example targets assume a top-level build
# (CMAKE_SOURCE_DIR) and do not work as a FetchContent subproject; only
# libqpdf is needed and it is still built as a dependency of lw_pdf.
FetchContent_MakeAvailable(qpdf)
# libqpdf compiles against jpeglib.h, so its build must wait for the
# ExternalProject install step.
add_dependencies(libqpdf lwpdf_libjpeg)

set(WEBVIEW2_VERSION "1.0.4078.44" CACHE STRING "WebView2 SDK version")
FetchContent_Declare(webview2_sdk
  URL "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_VERSION}"
  DOWNLOAD_NAME webview2.zip SOURCE_SUBDIR _no_cmake)
FetchContent_MakeAvailable(webview2_sdk)
set(WEBVIEW2_ROOT "${webview2_sdk_SOURCE_DIR}")
set(WEBVIEW2_INCLUDE "${WEBVIEW2_ROOT}/build/native/include")
set(WEBVIEW2_LOADER "${WEBVIEW2_ROOT}/build/native/x64/WebView2LoaderStatic.lib")
if(NOT EXISTS "${WEBVIEW2_INCLUDE}/WebView2.h" OR NOT EXISTS "${WEBVIEW2_LOADER}")
  message(FATAL_ERROR "Unexpected WebView2 SDK layout")
endif()

find_program(NPM_EXECUTABLE NAMES npm.cmd npm REQUIRED)
set(LWPDF_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(LWPDF_FRONTEND_ZIP "${LWPDF_GENERATED_DIR}/frontend.zip")
file(GLOB_RECURSE LWPDF_FRONTEND_INPUTS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*" "${CMAKE_CURRENT_SOURCE_DIR}/public/*")
list(APPEND LWPDF_FRONTEND_INPUTS
  "${CMAKE_CURRENT_SOURCE_DIR}/index.html" "${CMAKE_CURRENT_SOURCE_DIR}/package.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/package-lock.json" "${CMAKE_CURRENT_SOURCE_DIR}/tsconfig.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/vite.config.ts")
add_custom_command(
  OUTPUT "${LWPDF_FRONTEND_ZIP}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${LWPDF_GENERATED_DIR}"
  COMMAND "${NPM_EXECUTABLE}" --prefix "${CMAKE_CURRENT_SOURCE_DIR}" run build
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${LWPDF_FRONTEND_ZIP}"
  COMMAND "${CMAKE_COMMAND}" -E chdir "${CMAKE_CURRENT_SOURCE_DIR}/dist"
          "${CMAKE_COMMAND}" -E tar cf "${LWPDF_FRONTEND_ZIP}" --format=zip .
  DEPENDS ${LWPDF_FRONTEND_INPUTS}
  COMMENT "Building and embedding the lw.PDF frontend"
  VERBATIM)
add_custom_target(lwpdf_frontend_bundle DEPENDS "${LWPDF_FRONTEND_ZIP}")
file(TO_CMAKE_PATH "${LWPDF_FRONTEND_ZIP}" LWPDF_FRONTEND_ZIP_RC)
configure_file(resources/frontend_bundle.rc.in "${LWPDF_GENERATED_DIR}/frontend_bundle.rc" @ONLY)
configure_file(resources/version.rc.in "${LWPDF_GENERATED_DIR}/version.rc" @ONLY)
set_source_files_properties("${LWPDF_GENERATED_DIR}/frontend_bundle.rc"
  PROPERTIES OBJECT_DEPENDS "${LWPDF_FRONTEND_ZIP}")

add_executable(lw_pdf WIN32
  native/app/main.cpp native/app/main_window.cpp native/app/command_line.cpp
  native/associations/file_association.cpp native/bridge/bridge_dispatcher.cpp
  native/common/diagnostic_console.cpp native/common/native_log.cpp
  native/dialogs/file_dialog.cpp native/pdf/pdf_page_editor.cpp
  native/resources/frontend_bundle.cpp
  native/runtime/atomic_pdf_writer.cpp native/runtime/bounded_file_stream.cpp
  native/runtime/file_grant.cpp
  native/runtime/http_range.cpp native/runtime/recent_files.cpp
  native/webview/webview_host.cpp
  resources/lw-pdf.rc "${LWPDF_GENERATED_DIR}/version.rc"
  "${LWPDF_GENERATED_DIR}/frontend_bundle.rc")
add_dependencies(lw_pdf lwpdf_frontend_bundle)
target_include_directories(lw_pdf PRIVATE native "${WEBVIEW2_INCLUDE}")
target_include_directories(lw_pdf SYSTEM PRIVATE "${miniz_SOURCE_DIR}")
# zlibstatic is linked explicitly so MSBuild knows to build it before lw_pdf
# (qpdf only carries the library path, not a target dependency).
target_link_libraries(lw_pdf PRIVATE nlohmann_json::nlohmann_json miniz libqpdf zlibstatic "${WEBVIEW2_LOADER}"
  ole32 shell32 comdlg32 shlwapi user32 gdi32 bcrypt advapi32)
target_compile_definitions(lw_pdf PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_compile_options(lw_pdf PRIVATE /W4 /permissive- /EHsc)
set_target_properties(lw_pdf PROPERTIES OUTPUT_NAME "lw.PDF")

# Restore the caller's cache value after dependency configuration.
set(BUILD_TESTING ${LWPDF_BUILD_TESTING} CACHE BOOL "Build the testing tree" FORCE)
# Build this project's tests only when the caller enabled BUILD_TESTING.
if(LWPDF_BUILD_TESTING)
  add_executable(lw_pdf_page_editor_tests
    tests/pdf_page_editor_tests.cpp
    native/pdf/pdf_page_editor.cpp)
  target_include_directories(lw_pdf_page_editor_tests PRIVATE native)
  target_compile_definitions(lw_pdf_page_editor_tests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_compile_options(lw_pdf_page_editor_tests PRIVATE /W4 /permissive- /EHsc)
  target_link_libraries(lw_pdf_page_editor_tests PRIVATE libqpdf zlibstatic)
  add_test(NAME lw_pdf_page_editor_tests COMMAND lw_pdf_page_editor_tests)

  add_executable(lw_pdf_native_tests
    tests/native_integration_tests.cpp
    native/app/command_line.cpp
    native/associations/file_association.cpp
    native/runtime/atomic_pdf_writer.cpp
    native/runtime/bounded_file_stream.cpp
    native/runtime/file_grant.cpp
    native/runtime/http_range.cpp
    native/runtime/recent_files.cpp
    native/common/diagnostic_console.cpp
    native/common/native_log.cpp)
  target_include_directories(lw_pdf_native_tests PRIVATE native)
  target_compile_definitions(lw_pdf_native_tests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_compile_options(lw_pdf_native_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
  target_link_libraries(lw_pdf_native_tests PRIVATE nlohmann_json::nlohmann_json
    ole32 shell32 shlwapi advapi32 bcrypt)
  add_test(NAME lw_pdf_native_tests COMMAND lw_pdf_native_tests)
endif()
