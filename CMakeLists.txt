cmake_minimum_required(VERSION 3.21)

# 发布构建由 CI 根据 tag（v1.2.3）传入版本号
set(SNAPPIN_VERSION "0.2.41" CACHE STRING "Application version")
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
    src/app/appicon.h src/app/appicon.cpp
    src/app/actionregistry.h src/app/actionregistry.cpp
    src/app/imageutil.h src/app/imageutil.cpp
    src/app/appsettings.h src/app/appsettings.cpp
    src/app/settingsdialog.h src/app/settingsdialog.cpp
    src/history/historystore.h src/history/historystore.cpp
    src/history/historywindow.h src/history/historywindow.cpp
    src/platform/platform.h
    src/platform/hdrtonemap.h src/platform/hdrtonemap.cpp
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
    src/ocr/ocrwindow.h src/ocr/ocrwindow.cpp
    src/ocr/textrecognizer.h src/ocr/textrecognizer.cpp
    src/pin/pinmanager.h src/pin/pinmanager.cpp
    src/translate/translationengine.h
    src/translate/openaiengine.h src/translate/openaiengine.cpp
    src/translate/translator.h src/translate/translator.cpp
    src/translate/overlayrender.h src/translate/overlayrender.cpp
    src/translate/secretstore.h
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
        src/translate/secretstore_win.cpp
    )
elseif(APPLE)
    set(PLATFORM_SOURCES
        src/platform/mac/macplatform.h
        src/platform/mac/macplatform.mm
        src/platform/mac/maccapturer.mm
        src/platform/mac/macwindowfinder.mm
        src/platform/mac/machotkeymanager.mm
        src/platform/mac/macwheelthrottle.mm
        src/translate/mac/secretstore_mac.mm
        src/translate/systemengine.h
        src/translate/mac/systemengine.mm
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(PLATFORM_SOURCES
        src/platform/linux/linuxplatform.h
        src/platform/linux/linuxplatform.cpp
        src/platform/linux/x11windowfinder.cpp
        src/platform/linux/x11hotkeymanager.cpp
        src/platform/linux/portalcapturer.cpp
        src/translate/secretstore_linux.cpp
    )
else()
    set(PLATFORM_SOURCES src/platform/generic/genericplatform.cpp)
endif()

# 应用图标：Windows 嵌进 exe，macOS 放进 .app/Contents/Resources（源文件和生成脚本见 resources/icons、tools/gen_icons.py）
if(WIN32)
    set(APP_ICON_SOURCES resources/icons/snappin.rc)
    # .rc 编译器不跟踪 .ico 的改动，手动声明依赖
    set_source_files_properties(resources/icons/snappin.rc PROPERTIES
        OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/snappin.ico)
elseif(APPLE)
    set(APP_ICON_SOURCES resources/icons/snappin.icns)
    set_source_files_properties(resources/icons/snappin.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

# 文字识别：PP-OCRv5（mobile）模型 + ONNX Runtime（CPU），配置时下载并校验 SHA256，放在构建目录的 _deps 里。
# 用最新稳定版。它的 macOS 包只有 Apple 芯片（arm64），没有 Intel 版，所以 macOS 只构建 Apple 芯片版本
set(SNAPPIN_ORT_VERSION 1.31.0)
if(WIN32)
    set(_ort_pkg onnxruntime-win-x64-${SNAPPIN_ORT_VERSION})
    set(_ort_archive ${_ort_pkg}.zip)
    set(_ort_sha256 3958d8a44984160692c1b4ffd0de3cb48f8c39f9d34bb31e433db06e30b82195)
elseif(APPLE)
    if(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64"
       OR (NOT CMAKE_OSX_ARCHITECTURES AND CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "x86_64"))
        message(FATAL_ERROR "ONNX Runtime ${SNAPPIN_ORT_VERSION} 没有 Intel 版 macOS 库，只能构建 Apple 芯片版本："
                            "-DCMAKE_OSX_ARCHITECTURES=arm64")
    endif()
    set(_ort_pkg onnxruntime-osx-arm64-${SNAPPIN_ORT_VERSION})
    set(_ort_archive ${_ort_pkg}.tgz)
    set(_ort_sha256 1031b36dd3bdaa8d976a6130d9cf2a05e9be320b93ae804f8427b91dc3432afc)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_ort_pkg onnxruntime-linux-aarch64-${SNAPPIN_ORT_VERSION})
    set(_ort_archive ${_ort_pkg}.tgz)
    set(_ort_sha256 c5b8b3cca31f3d643a3b313b8f42e2d0f16a4fcefd02cda343d97bd5306afaab)
else()
    set(_ort_pkg onnxruntime-linux-x64-${SNAPPIN_ORT_VERSION})
    set(_ort_archive ${_ort_pkg}.tgz)
    set(_ort_sha256 cc5c72baf5ae5c8238a6841f0897227be2d02826b9cf98eaf02fdefaeeb03a57)
endif()
set(_deps ${CMAKE_BINARY_DIR}/_deps)

# 文件已存在且校验一致时不重新下载
function(snappin_download url file sha256)
    file(DOWNLOAD ${url} ${file} EXPECTED_HASH SHA256=${sha256} STATUS status)
    list(GET status 0 code)
    if(NOT code EQUAL 0)
        message(FATAL_ERROR "Download failed: ${url}\n${status}")
    endif()
endfunction()

snappin_download(https://github.com/microsoft/onnxruntime/releases/download/v${SNAPPIN_ORT_VERSION}/${_ort_archive}
                 ${_deps}/${_ort_archive} ${_ort_sha256})
set(ORT_ROOT ${_deps}/${_ort_pkg})
if(NOT EXISTS ${ORT_ROOT}/include/onnxruntime_cxx_api.h)
    file(ARCHIVE_EXTRACT INPUT ${_deps}/${_ort_archive} DESTINATION ${_deps})
endif()
add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES INTERFACE_INCLUDE_DIRECTORIES ${ORT_ROOT}/include)
if(WIN32)
    set(ORT_RUNTIME ${ORT_ROOT}/lib/onnxruntime.dll)
    set_target_properties(onnxruntime PROPERTIES IMPORTED_LOCATION ${ORT_RUNTIME} IMPORTED_IMPLIB ${ORT_ROOT}/lib/onnxruntime.lib)
elseif(APPLE)
    set(ORT_RUNTIME ${ORT_ROOT}/lib/libonnxruntime.${SNAPPIN_ORT_VERSION}.dylib)
    set_target_properties(onnxruntime PROPERTIES IMPORTED_LOCATION ${ORT_RUNTIME})
else()
    set(ORT_RUNTIME ${ORT_ROOT}/lib/libonnxruntime.so.1) # 与库的 SONAME 同名
    set_target_properties(onnxruntime PROPERTIES IMPORTED_LOCATION ${ORT_RUNTIME} IMPORTED_SONAME libonnxruntime.so.1)
endif()

# 模型来自 RapidOCR 转换好的 ONNX 版本（与 PaddleOCR 官方 PP-OCRv5 mobile 权重相同），字典在识别模型的元数据里
set(_ocr_url https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.10.0/onnx/PP-OCRv5)
snappin_download(${_ocr_url}/det/ch_PP-OCRv5_det_mobile.onnx ${_deps}/ocr/det.onnx
                 4d97c44a20d30a81aad087d6a396b08f786c4635742afc391f6621f5c6ae78ae)
snappin_download(${_ocr_url}/rec/ch_PP-OCRv5_rec_mobile.onnx ${_deps}/ocr/rec.onnx
                 5825fc7ebf84ae7a412be049820b4d86d77620f204a041697b0494669b1742c5)

# OCR 引擎（程序和测试共用）；翻译用到的文字处理也放在这里
add_library(snappin_ocr STATIC src/ocr/ocrtext.h src/ocr/ocrtext.cpp src/ocr/ppocr.h src/ocr/ppocr.cpp
                               src/translate/translatetext.h src/translate/translatetext.cpp)
target_include_directories(snappin_ocr PUBLIC src)
target_link_libraries(snappin_ocr PUBLIC Qt6::Gui onnxruntime)
# 模型是程序外的文件（不编进可执行文件，否则 macOS 通用二进制的两个切片各存一份）：
# macOS 放进 .app/Contents/Resources/ocr，Windows 和本机构建放在程序旁边的 ocr/，Linux 安装到 share/snappin/ocr
set(OCR_MODELS ${_deps}/ocr/det.onnx ${_deps}/ocr/rec.onnx)
function(snappin_copy_ocr_models target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${target}>/ocr
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${OCR_MODELS} $<TARGET_FILE_DIR:${target}>/ocr
        VERBATIM)
endfunction()

# 把 ONNX Runtime 放到程序旁边：Windows 与 exe 同目录（优先于系统自带的旧版 onnxruntime.dll），macOS 放进 .app 的 Frameworks
function(snappin_deploy_ort target)
    if(WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_RUNTIME} $<TARGET_FILE_DIR:${target}>
            VERBATIM)
    endif()
endfunction()

if(APPLE)
    set(APP_OCR_MODELS ${OCR_MODELS})
    set_source_files_properties(${OCR_MODELS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/ocr)
endif()
qt_add_executable(SnapPin WIN32 MACOSX_BUNDLE ${CORE_SOURCES} ${PLATFORM_SOURCES} ${APP_ICON_SOURCES} ${APP_OCR_MODELS})
if(NOT APPLE)
    snappin_copy_ocr_models(SnapPin)
endif()
snappin_deploy_ort(SnapPin)

target_include_directories(SnapPin PRIVATE src)
target_compile_definitions(SnapPin PRIVATE
    APP_VERSION="${PROJECT_VERSION}"
    SNAPPIN_GITHUB_REPO="${SNAPPIN_GITHUB_REPO}"
)
target_link_libraries(SnapPin PRIVATE Qt6::Widgets Qt6::Network snappin_ocr)

# macOS 系统翻译：Translation 框架只有 Swift 接口。按每个架构用 swiftc 编成静态库再 lipo 合并
# （CMake 的 Swift 支持在 Ninja + 多架构下不可用）。Translation 框架弱链接，macOS 14、15 上照样能启动
if(APPLE)
    set(_swift_archs ${CMAKE_OSX_ARCHITECTURES})
    if(NOT _swift_archs)
        set(_swift_archs ${CMAKE_HOST_SYSTEM_PROCESSOR})
    endif()
    set(_swift_src ${CMAKE_CURRENT_SOURCE_DIR}/src/translate/mac/systemtranslate.swift)
    set(_swift_dir ${CMAKE_BINARY_DIR}/swift)
    set(_swift_libs)
    foreach(arch ${_swift_archs})
        set(_lib ${_swift_dir}/${arch}/libsystemtranslate.a)
        add_custom_command(OUTPUT ${_lib}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_swift_dir}/${arch}
            COMMAND xcrun swiftc -parse-as-library -emit-library -static -O -swift-version 5
                    -module-name SnapPinSystemTranslate -module-cache-path ${_swift_dir}/module-cache
                    -target ${arch}-apple-macos${CMAKE_OSX_DEPLOYMENT_TARGET}
                    -Xfrontend -disable-autolink-framework -Xfrontend Translation
                    ${_swift_src} -o ${_lib}
            DEPENDS ${_swift_src} VERBATIM)
        list(APPEND _swift_libs ${_lib})
    endforeach()
    set(SWIFT_TRANSLATE_LIB ${_swift_dir}/libsystemtranslate.a)
    add_custom_command(OUTPUT ${SWIFT_TRANSLATE_LIB}
        COMMAND lipo -create ${_swift_libs} -output ${SWIFT_TRANSLATE_LIB}
        DEPENDS ${_swift_libs} VERBATIM)
    add_custom_target(snappin_systemtranslate DEPENDS ${SWIFT_TRANSLATE_LIB})
    add_dependencies(SnapPin snappin_systemtranslate)
    # 链接 Swift 运行库：系统自带的在 SDK 的 usr/lib/swift，兼容库在工具链里
    execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE _sdk OUTPUT_STRIP_TRAILING_WHITESPACE)
    execute_process(COMMAND xcrun --find swiftc OUTPUT_VARIABLE _swiftc OUTPUT_STRIP_TRAILING_WHITESPACE)
    get_filename_component(_toolchain_bin ${_swiftc} DIRECTORY)
    target_link_directories(SnapPin PRIVATE ${_sdk}/usr/lib/swift ${_toolchain_bin}/../lib/swift/macosx)
    target_link_libraries(SnapPin PRIVATE ${SWIFT_TRANSLATE_LIB} "-weak_framework Translation"
                          "-framework NaturalLanguage" "-framework SwiftUI" "-framework Security")
endif()

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
    qt_add_executable(hdrtonemap_test tests/hdrtonemap_test.cpp src/platform/hdrtonemap.cpp)
    target_include_directories(hdrtonemap_test PRIVATE src)
    target_link_libraries(hdrtonemap_test PRIVATE Qt6::Gui)
    add_test(NAME hdrtonemap COMMAND hdrtonemap_test)
    qt_add_executable(ocrtext_test tests/ocrtext_test.cpp src/ocr/ocrtext.cpp)
    target_include_directories(ocrtext_test PRIVATE src)
    target_link_libraries(ocrtext_test PRIVATE Qt6::Gui)
    add_test(NAME ocrtext COMMAND ocrtext_test)
    qt_add_executable(translatetext_test tests/translatetext_test.cpp src/translate/translatetext.cpp src/ocr/ocrtext.cpp)
    target_include_directories(translatetext_test PRIVATE src)
    target_link_libraries(translatetext_test PRIVATE Qt6::Gui)
    add_test(NAME translatetext COMMAND translatetext_test)
    qt_add_executable(ppocr_test tests/ppocr_test.cpp)
    target_link_libraries(ppocr_test PRIVATE snappin_ocr)
    snappin_copy_ocr_models(ppocr_test)
    snappin_deploy_ort(ppocr_test)
    add_test(NAME ppocr COMMAND ppocr_test)
endif()

if(WIN32)
    target_compile_definitions(SnapPin PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(SnapPin PRIVATE user32 gdi32 dwmapi d3d11 dxgi ole32 oleaut32 advapi32)
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
    # ONNX Runtime 放进 .app/Contents/Frameworks（必须在下面的签名之前）
    set_target_properties(SnapPin PROPERTIES BUILD_RPATH "@executable_path/../Frameworks"
                                             INSTALL_RPATH "@executable_path/../Frameworks")
    add_custom_command(TARGET SnapPin POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:SnapPin>/Frameworks"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ORT_RUNTIME} "$<TARGET_BUNDLE_CONTENT_DIR:SnapPin>/Frameworks/"
        VERBATIM)
    # 系统权限（屏幕录制、辅助功能）跟签名绑定。默认的 ad-hoc 签名每次编译都会变，权限随之失效；
    # 本机开发时指定一个固定的签名身份（与发布版相同的自签名证书 "SnapPin Release"，开发版和安装版共用一条系统授权），授权一次即可长期有效
    set(SNAPPIN_CODESIGN_IDENTITY "" CACHE STRING "Code signing identity for local builds (keeps macOS permissions across rebuilds)")
    if(SNAPPIN_CODESIGN_IDENTITY)
        add_custom_command(TARGET SnapPin POST_BUILD
            COMMAND codesign --force --deep --sign "${SNAPPIN_CODESIGN_IDENTITY}" "$<TARGET_BUNDLE_DIR:SnapPin>"
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
    # AppImage：linuxdeploy 按 RPATH 找到 lib/ 下的 ONNX Runtime 并打包进去
    set_target_properties(SnapPin PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
    file(REAL_PATH ${ORT_RUNTIME} _ort_real) # libonnxruntime.so.1 是符号链接，安装它指向的文件
    install(FILES ${_ort_real} DESTINATION lib RENAME libonnxruntime.so.1)
    install(FILES ${OCR_MODELS} DESTINATION share/snappin/ocr)
    install(FILES resources/linux/snappin.desktop DESTINATION share/applications)
    install(FILES resources/linux/snappin.svg DESTINATION share/icons/hicolor/scalable/apps)
    foreach(size 256 512)
        install(FILES resources/icons/png/snappin-${size}.png
                DESTINATION share/icons/hicolor/${size}x${size}/apps RENAME snappin.png)
    endforeach()
endif()
