cmake_minimum_required(VERSION 3.21)

# 发布构建由 CI 根据 tag（v1.2.3）传入版本号
set(SNAPPIN_VERSION "0.2.14" CACHE STRING "Application version")
# 自动更新从这个仓库的 GitHub Releases 获取新版本
set(SNAPPIN_GITHUB_REPO "wangchaozhi/SnapPin" CACHE STRING "owner/repo for update checks")
# 版本号错了自动更新会反复提示，宁可构建失败
if(NOT SNAPPIN_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "SNAPPIN_VERSION must look like 1.2.3, got '${SNAPPIN_VERSION}'")
endif()

# ScreenCaptureKit 的截图接口需要 macOS 14；必须在 project() 之前设置
set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS version")

project(SnapPin VERSION ${SNAPPIN_VERSION} LANGUAGES CXX)
if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.7 REQUIRED COMPONENTS Widgets Network)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(Qt6 REQUIRED COMPONENTS DBus) # Wayland 截屏走 xdg-desktop-portal
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
endif()
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
    src/capture/toolicons.h src/capture/toolicons.cpp
    src/capture/colorpalette.h src/capture/colorpalette.cpp
    src/capture/scrollstitcher.h src/capture/scrollstitcher.cpp
    src/capture/imageregions.h src/capture/imageregions.cpp
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
elseif(APPLE)
    set(PLATFORM_SOURCES
        src/platform/mac/macplatform.h
        src/platform/mac/macplatform.mm
        src/platform/mac/maccapturer.mm
        src/platform/mac/macwindowfinder.mm
        src/platform/mac/machotkeymanager.mm
        src/platform/mac/macwheelthrottle.mm
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_SOURCES
        src/platform/linux/linuxplatform.h
        src/platform/linux/linuxplatform.cpp
        src/platform/linux/x11windowfinder.cpp
        src/platform/linux/x11hotkeymanager.cpp
        src/platform/linux/portalcapturer.cpp
    )
else()
    set(PLATFORM_SOURCES src/platform/generic/genericplatform.cpp)
endif()

# 应用图标：Windows 嵌进 exe，macOS 放进 .app/Contents/Resources（源文件和生成脚本见 resources/icons、tools/gen_icons.py）
if(WIN32)
    set(APP_ICON_SOURCES resources/icons/snappin.rc)
elseif(APPLE)
    set(APP_ICON_SOURCES resources/icons/snappin.icns)
    set_source_files_properties(resources/icons/snappin.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

qt_add_executable(SnapPin WIN32 MACOSX_BUNDLE ${CORE_SOURCES} ${PLATFORM_SOURCES} ${APP_ICON_SOURCES})
# 托盘和窗口图标
qt_add_resources(SnapPin app_icons FILES
    resources/icons/png/snappin-16.png resources/icons/png/snappin-24.png resources/icons/png/snappin-32.png
    resources/icons/png/snappin-48.png resources/icons/png/snappin-64.png resources/icons/png/snappin-128.png
    resources/icons/png/snappin-256.png
    PREFIX /icons BASE resources/icons/png)

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
    qt_add_executable(imageregions_test tests/imageregions_test.cpp src/capture/imageregions.cpp)
    target_include_directories(imageregions_test PRIVATE src)
    target_link_libraries(imageregions_test PRIVATE Qt6::Gui)
    add_test(NAME imageregions COMMAND imageregions_test)
endif()

if(WIN32)
    target_compile_definitions(SnapPin PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(SnapPin PRIVATE user32 gdi32 dwmapi d3d11 dxgi ole32 oleaut32)
endif()

if(APPLE)
    set_target_properties(SnapPin PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/macos/Info.plist.in
        MACOSX_BUNDLE_GUI_IDENTIFIER com.github.wangchaozhi.SnapPin
        MACOSX_BUNDLE_BUNDLE_NAME SnapPin
        MACOSX_BUNDLE_ICON_FILE snappin.icns
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    )
    target_compile_options(SnapPin PRIVATE $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
    # 系统权限（屏幕录制、辅助功能）跟签名绑定。默认的 ad-hoc 签名每次编译都会变，权限随之失效；
    # 本机开发时指定一个固定的签名身份（与发布版相同的自签名证书 "SnapPin Release"，开发版和安装版共用一条系统授权），授权一次即可长期有效
    set(SNAPPIN_CODESIGN_IDENTITY "" CACHE STRING "Code signing identity for local builds (keeps macOS permissions across rebuilds)")
    if(SNAPPIN_CODESIGN_IDENTITY)
        add_custom_command(TARGET SnapPin POST_BUILD
            COMMAND codesign --force --sign "${SNAPPIN_CODESIGN_IDENTITY}" "$<TARGET_BUNDLE_DIR:SnapPin>"
            VERBATIM)
    endif()
    target_link_libraries(SnapPin PRIVATE
        "-framework AppKit" "-framework ApplicationServices" "-framework Carbon"
        "-framework CoreGraphics" "-framework ScreenCaptureKit"
    )
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(SnapPin PRIVATE Qt6::DBus PkgConfig::XCB)
    install(TARGETS SnapPin RUNTIME DESTINATION bin)
    install(FILES resources/linux/snappin.desktop DESTINATION share/applications)
    install(FILES resources/linux/snappin.svg DESTINATION share/icons/hicolor/scalable/apps)
    foreach(size 256 512)
        install(FILES resources/icons/png/snappin-${size}.png
                DESTINATION share/icons/hicolor/${size}x${size}/apps RENAME snappin.png)
    endforeach()
endif()
