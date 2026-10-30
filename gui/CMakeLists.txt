cmake_minimum_required(VERSION 3.14)

enable_language(RC)

include(FetchContent)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.9
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

# Launcher version = the receiver's VERSION in uxplay.cpp (single source of truth).
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/../uxplay.cpp UXPLAY_VERSION_LINE REGEX "^#define VERSION \"")
string(REGEX MATCH "([0-9]+)\\.([0-9]+)\\.?([0-9]*)" UXPLAY_VERSION "${UXPLAY_VERSION_LINE}")
set(UXPLAY_VER_MAJOR "${CMAKE_MATCH_1}")
set(UXPLAY_VER_MINOR "${CMAKE_MATCH_2}")
set(UXPLAY_VER_PATCH "${CMAKE_MATCH_3}")
if(UXPLAY_VER_PATCH STREQUAL "")
  set(UXPLAY_VER_PATCH 0)
endif()
# Also used for uxplay.exe's version info (top-level CMakeLists.txt).
set(UXPLAY_VER_MAJOR "${UXPLAY_VER_MAJOR}" PARENT_SCOPE)
set(UXPLAY_VER_MINOR "${UXPLAY_VER_MINOR}" PARENT_SCOPE)
set(UXPLAY_VER_PATCH "${UXPLAY_VER_PATCH}" PARENT_SCOPE)

add_executable(uxplay-gui WIN32
  uxplay-gui.cpp
  launcher_ui.cpp
  session.cpp
  prefs.cpp
  uxplay-gui.rc
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(uxplay-gui PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(uxplay-gui PRIVATE d3d11 dxgi d3dcompiler dwmapi gdi32 user32 shell32 ole32 advapi32)
target_compile_definitions(uxplay-gui PRIVATE
  UNICODE _UNICODE
  $<$<COMPILE_LANGUAGE:CXX>:UXPLAY_LAUNCHER_VERSION="${UXPLAY_VERSION}">
  UXPLAY_VER_MAJOR=${UXPLAY_VER_MAJOR}
  UXPLAY_VER_MINOR=${UXPLAY_VER_MINOR}
  UXPLAY_VER_PATCH=${UXPLAY_VER_PATCH}
)

# Platform-free unit tests for the session model and preferences (no GUI needed).
option(UXPLAY_GUI_TESTS "Build launcher unit tests" OFF)
if(UXPLAY_GUI_TESTS)
  add_executable(uxplay-gui-tests tests/launcher_tests.cpp session.cpp prefs.cpp)
  target_include_directories(uxplay-gui-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()
