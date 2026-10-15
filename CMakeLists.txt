cmake_minimum_required(VERSION 3.21)

# 发布构建由 CI 根据 tag（v1.2.3）传入版本号
set(SNAPPIN_VERSION "0.1.2" CACHE STRING "Application version")
# 自动更新从这个仓库的 GitHub Releases 获取新版本
set(SNAPPIN_GITHUB_REPO "wangchaozhi/SnapPin" CACHE STRING "owner/repo for update checks")
# 版本号错了自动更新会反复提示，宁可构建失败
if(NOT SNAPPIN_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "SNAPPIN_VERSION must look like 1.2.3, got '${SNAPPIN_VERSION}'")
endif()

project(SnapPin VERSION ${SNAPPIN_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Widgets Network)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/utf-8)
endif()

set(CORE_SOURCES
    src/main.cpp
    src/app/appcontext.h src/app/appcontext.cpp
    src/app/actionregistry.h src/app/actionregistry.cpp
    src/app/imageutil.h src/app/imageutil.cpp
    src/app/appsettings.h src/app/appsettings.cpp
    src/app/settingsdialog.h src/app/settingsdialog.cpp
    src/history/historystore.h src/history/historystore.cpp
    src/history/historywindow.h src/history/historywindow.cpp
    src/platform/platform.h
    src/annotation/shapes.h src/annotation/shapes.cpp
    src/annotation/annotationdoc.h src/annotation/annotationdoc.cpp
    src/annotation/tools.h src/annotation/tools.cpp
    src/capture/capturesession.h src/capture/capturesession.cpp
    src/capture/overlaywindow.h src/capture/overlaywindow.cpp
    src/capture/overlaystates.h src/capture/overlaystates.cpp
    src/capture/editortoolbar.h src/capture/editortoolbar.cpp
    src/capture/scrollstitcher.h src/capture/scrollstitcher.cpp
    src/capture/scrollcapture.h src/capture/scrollcapture.cpp
    src/pin/pinwindow.h src/pin/pinwindow.cpp
    src/pin/pinmanager.h src/pin/pinmanager.cpp
    src/update/updater.h src/update/updater.cpp
)

# 平台适配层：核心代码只依赖 platform/platform.h 中的接口
if(WIN32)
    set(PLATFORM_SOURCES
        src/platform/win/winplatform.h
        src/platform/win/winplatform.cpp
        src/platform/win/wincapturer.cpp
        src/platform/win/dxgicapturer.cpp
        src/platform/win/winwindowfinder.cpp
        src/platform/win/uiaelements.cpp
        src/platform/win/winhotkeymanager.cpp
        src/platform/win/winwheelthrottle.cpp
    )
else()
    set(PLATFORM_SOURCES src/platform/generic/genericplatform.cpp)
endif()

qt_add_executable(SnapPin WIN32 MACOSX_BUNDLE ${CORE_SOURCES} ${PLATFORM_SOURCES})

target_include_directories(SnapPin PRIVATE src)
target_compile_definitions(SnapPin PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    SNAPPIN_GITHUB_REPO="${SNAPPIN_GITHUB_REPO}"
)
target_link_libraries(SnapPin PRIVATE Qt6::Widgets Qt6::Network)

option(SNAPPIN_BUILD_TESTS "Build unit tests" ON)
if(SNAPPIN_BUILD_TESTS)
    enable_testing()
    qt_add_executable(scrollstitcher_test tests/scrollstitcher_test.cpp src/capture/scrollstitcher.cpp)
    target_include_directories(scrollstitcher_test PRIVATE src)
    target_link_libraries(scrollstitcher_test PRIVATE Qt6::Gui)
    add_test(NAME scrollstitcher COMMAND scrollstitcher_test)
endif()

if(WIN32)
    target_compile_definitions(SnapPin PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(SnapPin PRIVATE user32 gdi32 dwmapi d3d11 dxgi ole32 oleaut32)
endif()
