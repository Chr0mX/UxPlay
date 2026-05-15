cmake_minimum_required(VERSION 3.14)

include(FetchContent)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.91.9
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(imgui)

add_executable(uxplay-gui WIN32
  uxplay-gui.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
)

target_include_directories(uxplay-gui PRIVATE
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(uxplay-gui PRIVATE d3d11 dxgi d3dcompiler dwmapi gdi32 user32)
target_compile_definitions(uxplay-gui PRIVATE UNICODE _UNICODE)
