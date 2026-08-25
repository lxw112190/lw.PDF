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

include(FetchContent)
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
  native/common/native_log.cpp
  native/dialogs/file_dialog.cpp native/resources/frontend_bundle.cpp
  native/runtime/bounded_file_stream.cpp native/runtime/file_grant.cpp
  native/runtime/http_range.cpp native/runtime/recent_files.cpp
  native/webview/webview_host.cpp
  resources/lw-pdf.rc "${LWPDF_GENERATED_DIR}/version.rc"
  "${LWPDF_GENERATED_DIR}/frontend_bundle.rc")
add_dependencies(lw_pdf lwpdf_frontend_bundle)
target_include_directories(lw_pdf PRIVATE native "${WEBVIEW2_INCLUDE}")
target_include_directories(lw_pdf SYSTEM PRIVATE "${miniz_SOURCE_DIR}")
target_link_libraries(lw_pdf PRIVATE nlohmann_json::nlohmann_json miniz "${WEBVIEW2_LOADER}"
  ole32 shell32 comdlg32 shlwapi user32 gdi32 bcrypt advapi32)
target_compile_definitions(lw_pdf PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_compile_options(lw_pdf PRIVATE /W4 /permissive- /utf-8 /EHsc)
set_target_properties(lw_pdf PROPERTIES OUTPUT_NAME "lw.PDF")

include(CTest)
if(BUILD_TESTING)
  add_executable(lw_pdf_native_tests
    tests/native_integration_tests.cpp
    native/app/command_line.cpp
    native/associations/file_association.cpp
    native/runtime/bounded_file_stream.cpp
    native/runtime/file_grant.cpp
    native/runtime/http_range.cpp
    native/runtime/recent_files.cpp)
  target_include_directories(lw_pdf_native_tests PRIVATE native)
  target_compile_definitions(lw_pdf_native_tests PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
  target_compile_options(lw_pdf_native_tests PRIVATE /W4 /permissive- /utf-8 /EHsc)
  target_link_libraries(lw_pdf_native_tests PRIVATE nlohmann_json::nlohmann_json
    ole32 shell32 shlwapi advapi32 bcrypt)
  add_test(NAME lw_pdf_native_tests COMMAND lw_pdf_native_tests)
endif()
