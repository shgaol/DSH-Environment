cmake_minimum_required(VERSION 3.16)

project(DSH-Environment VERSION 1.0.0 LANGUAGES CXX)

# 需求6：Qt6 源码目录（参考）：Z:/QTSource/5.15.2/Src
# 需求9：调用的 Qt 编译目录：Z:/QTSource/6.11.2/msvc2022_64
if(NOT CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "Z:/QTSource/6.11.2/msvc2022_64")
endif()

# 需求10：生成的 exe 输出到工作目录下的 bin 目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 开启 Qt 的自动处理（MOC / RCC / UIC）
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 同时兼容 Qt6 与 Qt5
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Network WebSockets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Network WebSockets)

# 网页显示后端探测（按优先级）：
#  1) Qt WebEngine（Qt5/Qt6 通用，最优先；唯一考虑的方向，不用 QML）
#  2) Qt WebView（仅 Qt6，Windows 上基于 Edge WebView2；非 QML）
find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS WebEngineWidgets)
if (NOT Qt${QT_VERSION_MAJOR}WebEngineWidgets_FOUND AND QT_VERSION_MAJOR GREATER_EQUAL 6)
    find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS WebView)
endif()
# 源码为 UTF-8：MSVC 需显式指定 /utf-8，否则中文注释/字符串会编译警告或乱码
if(MSVC)
    add_compile_options(/utf-8)
endif()
add_executable(DSH-Environment WIN32
    main.cpp
    MainWindow.h
    MainWindow.cpp
    EnvSettingDlg.h
    EnvSettingDlg.cpp
    Application.h
    Application.cpp
    DSHSrcManager.h
    DSHSrcManager.cpp
    MdiArea.h
    MdiArea.cpp
    TableView.h
    TableView.cpp
    DHSSrcDlg.h
    DHSSrcDlg.cpp
    DHSScrCtrlDlg.h
    DHSScrCtrlDlg.cpp
    ProfileDlg.h
    ProfileDlg.cpp
    DSWebViewWindow.h
    DSWebViewWindow.cpp
    DSWebEnginePage.h
    DSWebEnginePage.cpp
    DSWebEngineView.h
    DSWebEngineView.cpp
    WebApplet/DSWebAppletStore.h
    WebApplet/DSWebAppletStore.cpp
    WebApplet/DSWebAppletDlg.h
    WebApplet/DSWebAppletDlg.cpp
    WebApplet/DSWebAppletIconFetcher.h
    WebApplet/DSWebAppletIconFetcher.cpp
    WebApplet/DSWebAppletPage.h
    WebApplet/DSWebAppletPage.cpp
    DSHChatWindow/DSHCommander.h
    DSHChatWindow/DSHCommander.cpp
    DSHChatWindow/DSHChatWindow.h
    DSHChatWindow/DSHChatWindow.cpp
    DSCmdView.h
    DSCmdView.cpp
    DSHChatWindow/DSDropTextEdit.h
    DSHChatWindow/DSDropTextEdit.cpp
    DSConPty.h
    DSConPty.cpp
    NavBar/UINavBarItem.h
    NavBar/UINavBarItem.cpp
    NavBar/UINavBar.h
    NavBar/UINavBar.cpp
    resources/resources.qrc    # 应用图标资源（:/app.ico）
    resources/app.rc           # 嵌入 exe 的程序图标（资源管理器显示）
)

target_link_libraries(DSH-Environment PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::WebSockets
)

# MinGW：显式静态链接 GCC 运行时（libgcc / libstdc++ / libwinpthread），
# 避免运行时报 XXX.dll 找不到（Qt 静态库场景下必需）
if(MINGW)
    target_link_options(DSH-Environment PRIVATE -static -static-libgcc -static-libstdc++)
endif()

# 网页显示后端：优先 WebEngine，其次 Qt6 WebView，都没有则占位
if (Qt${QT_VERSION_MAJOR}WebEngineWidgets_FOUND)
    target_compile_definitions(DSH-Environment PRIVATE DSH_HAVE_WEBENGINE)
    target_link_libraries(DSH-Environment PRIVATE Qt${QT_VERSION_MAJOR}::WebEngineWidgets)
elseif (Qt${QT_VERSION_MAJOR}WebView_FOUND AND QT_VERSION_MAJOR GREATER_EQUAL 6)
    target_compile_definitions(DSH-Environment PRIVATE DSH_HAVE_WEBVIEW)
    target_link_libraries(DSH-Environment PRIVATE Qt${QT_VERSION_MAJOR}::WebView)
endif()

# ============================================================
# 需求10/11：部署 Qt 运行库（仅 Qt6，动态库场景）
#   布局：
#     bin/DSH-Environment.exe        生成的 exe（见上方 RUNTIME_OUTPUT_DIRECTORY）
#     bin/Qt6*.dll 等                 直接调用的 Qt dll（与 exe 同目录，由系统加载）
#     bin/QtWebEngineProcess.exe     WebEngine 辅助进程（必须与 Qt6WebEngineCore.dll
#                                    同目录，否则加载失败导致程序退出，不能移入 Resource）
#     bin/Resource/qml/              QML 模块（与直接调用的 dll 无关 → Resource 下）
#     bin/Resource/resources/        WebEngine 数据（icudtl.dat、*.pak 等）
#     bin/Resource/translations/     Qt 翻译 / WebEngine 语言包
#     bin/Resource/plugins/          Qt 插件
#   说明：只拷贝程序正常运行需要的文件（windeployqt 按依赖分析拷贝）；
#         qml/resources/translations/plugins 全部位于 Resource，
#         启动时由 setupResourceDirByWinApi() 用环境变量指向这些目录。
# ============================================================
if(WIN32 AND QT_VERSION_MAJOR EQUAL 6)
    # 从 Qt6::qmake 的导入位置推导 Qt 的 bin 目录，找到 windeployqt
    get_target_property(_qmake_executable Qt6::qmake IMPORTED_LOCATION)
    if(_qmake_executable)
        get_filename_component(_qt_bin_dir "${_qmake_executable}" DIRECTORY)
        set(_windeployqt "${_qt_bin_dir}/windeployqt.exe")

        add_custom_command(TARGET DSH-Environment POST_BUILD
            COMMAND "${_windeployqt}"
                    --no-system-d3d-compiler   # Win10 系统自带 d3dcompiler_47
                    --no-opengl-sw             # 不需要软件 OpenGL 渲染器
                    --plugindir "$<TARGET_FILE_DIR:DSH-Environment>/Resource/plugins"
                    --translationdir "$<TARGET_FILE_DIR:DSH-Environment>/Resource/translations"
                    "$<TARGET_FILE:DSH-Environment>"
            # 把 windeployqt 仍放在 bin 根目录的 WebEngine 运行时文件整理进 Resource
            COMMAND ${CMAKE_COMMAND}
                    -DBIN_DIR="$<TARGET_FILE_DIR:DSH-Environment>"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/organize_resource.cmake"
            COMMENT "windeployqt: Qt dll → bin，resources/translations/plugins → bin/Resource"
        )
    endif()
endif()
