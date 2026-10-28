cmake_minimum_required(VERSION 3.24)
if(NOT DEFINED PHOTOSCREEN_VERSION)
  set(PHOTOSCREEN_VERSION "0.0.0")
endif()
project(PhotoScreen VERSION ${PHOTOSCREEN_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(APPLE)
  enable_language(OBJCXX)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "15.2" CACHE STRING "Minimum macOS version")
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Test)

add_library(photoscreen_core STATIC
  src/core/annotation.cpp
  src/core/annotation.h
  src/core/geometry.cpp
  src/core/geometry.h
  src/core/interaction.cpp
  src/core/interaction.h
  src/core/screen.cpp
  src/core/screen.h
)
target_include_directories(photoscreen_core PUBLIC src)
target_link_libraries(photoscreen_core PUBLIC Qt6::Core Qt6::Gui)

set(APP_SOURCES
  src/main.cpp
  src/app.cpp src/app.h
  src/captureoverlay.cpp src/captureoverlay.h
  src/platform/platform.h
)
if(APPLE)
  list(APPEND APP_SOURCES src/platform/macos.mm)
  add_library(photoscreen_macimage STATIC src/platform/macimage.mm src/platform/macimage.h)
  target_include_directories(photoscreen_macimage PUBLIC src)
  target_link_libraries(photoscreen_macimage PUBLIC Qt6::Gui "-framework CoreGraphics")
elseif(WIN32)
  list(APPEND APP_SOURCES src/platform/windows.cpp)
else()
  message(FATAL_ERROR "PhotoScreen supports macOS and Windows")
endif()

qt_add_executable(PhotoScreen WIN32 MACOSX_BUNDLE ${APP_SOURCES})
target_link_libraries(PhotoScreen PRIVATE photoscreen_core Qt6::Widgets)
target_compile_definitions(PhotoScreen PRIVATE PHOTOSCREEN_VERSION="${PROJECT_VERSION}")
if(APPLE)
  target_link_libraries(PhotoScreen PRIVATE photoscreen_macimage)
  target_link_libraries(PhotoScreen PRIVATE "-framework AppKit" "-framework Carbon" "-framework CoreGraphics" "-framework ScreenCaptureKit" "-framework ServiceManagement")
  set_source_files_properties(src/platform/macos.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
  set_target_properties(PhotoScreen PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.mike666wq.photoscreen"
    MACOSX_BUNDLE_BUNDLE_NAME "PhotoScreen"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
elseif(WIN32)
  target_link_libraries(PhotoScreen PRIVATE d3d11 dxgi dwmapi user32 gdi32 shell32 ole32)
  target_compile_definitions(PhotoScreen PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
  set_target_properties(PhotoScreen PROPERTIES QT_WINDOWS_APP_PROJECT_IDENTIFIER "PhotoScreen")
endif()

include(CTest)
if(BUILD_TESTING)
  # QTest 的日志落盘到构建目录：Windows CI 上 ctest 不会回显 QTest 的标准输出，
  # 只靠 ctest 的汇总无法定位失败用例，必须把逐条 PASS/FAIL 写进产物。
  file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/reports)
  add_executable(photoscreen_tests tests/core_tests.cpp)
  target_link_libraries(photoscreen_tests PRIVATE photoscreen_core Qt6::Test)
  add_test(NAME photoscreen_core COMMAND photoscreen_tests -o ${CMAKE_BINARY_DIR}/reports/photoscreen_core.txt,txt)
  set_tests_properties(photoscreen_core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_executable(photoscreen_overlay_tests tests/overlay_tests.cpp tests/fake_platform.cpp src/captureoverlay.cpp src/captureoverlay.h)
  target_link_libraries(photoscreen_overlay_tests PRIVATE photoscreen_core Qt6::Test Qt6::Widgets)
  add_test(NAME photoscreen_overlay COMMAND photoscreen_overlay_tests -o ${CMAKE_BINARY_DIR}/reports/photoscreen_overlay.txt,txt)
  set_tests_properties(photoscreen_overlay PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTOSCREEN_REPORT_DIR=${CMAKE_BINARY_DIR}/reports")
  add_executable(photoscreen_visual_report tests/visual_report.cpp)
  target_link_libraries(photoscreen_visual_report PRIVATE photoscreen_core)
  add_test(NAME photoscreen_visual_report COMMAND photoscreen_visual_report)
  set_tests_properties(photoscreen_visual_report PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;PHOTOSCREEN_REPORT_DIR=${CMAKE_BINARY_DIR}/reports")
  if(APPLE)
    add_executable(photoscreen_mac_image_tests tests/mac_image_tests.mm)
    target_link_libraries(photoscreen_mac_image_tests PRIVATE photoscreen_macimage Qt6::Test)
    add_test(NAME photoscreen_mac_image COMMAND photoscreen_mac_image_tests -o ${CMAKE_BINARY_DIR}/reports/photoscreen_mac_image.txt,txt)
    set_tests_properties(photoscreen_mac_image PROPERTIES ENVIRONMENT "PHOTOSCREEN_REPORT_DIR=${CMAKE_BINARY_DIR}/reports")
  endif()
endif()

install(TARGETS PhotoScreen BUNDLE DESTINATION . RUNTIME DESTINATION .)
