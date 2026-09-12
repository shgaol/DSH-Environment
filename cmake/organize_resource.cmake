# cmake/organize_resource.cmake
# 用途：把 windeployqt 部署到 bin 根目录下的、与“直接调用的 dll 无关”的
#       运行时文件（resources/*、translations 残留等）
#       整理到 bin/Resource 下面，保证 qml/resources/translations/plugins
#       全部位于 Resource 目录。
# 用法：cmake -DBIN_DIR=<bin绝对路径> -P organize_resource.cmake
# 说明：windeployqt 已用 --plugindir/--translationdir 直接部署
#       到 bin/Resource/...，本脚本只处理 windeployqt 仍会放到 bin 根目录的
#       WebEngine 资源文件（它没有对应的 --*dir 选项）。
#
# 重要：QtWebEngineProcess.exe 不能移进 Resource！
#       它依赖 Qt6WebEngineCore.dll 等，Windows 加载器按 exe 所在目录→系统目录
#       →PATH 查找依赖 DLL；移走会导致进程无法启动、WebEngine 初始化失败、
#       程序退出（这正是“开启服务会让程序退出”的根因）。它必须留在 bin 根目录。

if(NOT DEFINED BIN_DIR)
    message(FATAL_ERROR "缺少 -DBIN_DIR 参数")
endif()

set(RES_DIR "${BIN_DIR}/Resource")

# 1) WebEngine 资源文件（icudtl.dat、snapshot_blob.bin、*.pak 等）→ Resource/resources
file(GLOB _web_files "${BIN_DIR}/icudtl.dat" "${BIN_DIR}/snapshot_blob.bin"
                     "${BIN_DIR}/v8_context_snapshot.bin" "${BIN_DIR}/*.pak")
foreach(_f IN LISTS _web_files)
    if(EXISTS "${_f}")
        get_filename_component(_name "${_f}" NAME)
        file(MAKE_DIRECTORY "${RES_DIR}/resources")
        file(RENAME "${_f}" "${RES_DIR}/resources/${_name}")
        message(STATUS "移动 ${_name} -> Resource/resources/")
    endif()
endforeach()

# 1b) windeployqt 生成的 resources/ 目录（WebEngine 资源，如 *.pak、icudtl.dat 等）
#     若整个目录在 bin 根目录，整体合并进 Resource/resources
if(EXISTS "${BIN_DIR}/resources")
    file(MAKE_DIRECTORY "${RES_DIR}/resources")
    file(COPY "${BIN_DIR}/resources/" DESTINATION "${RES_DIR}/resources/")
    file(REMOVE_RECURSE "${BIN_DIR}/resources")
    message(STATUS "合并 bin/resources -> Resource/resources")
endif()

# 2) windeployqt 若在 bin 根生成了 qml/（Qt 6.11 的 windeployqt 无 --qmlimportdir，
#     QML 导入默认部署到 exe 同目录）→ 移入 Resource/qml
if(EXISTS "${BIN_DIR}/qml")
    file(MAKE_DIRECTORY "${RES_DIR}/qml")
    file(COPY "${BIN_DIR}/qml/" DESTINATION "${RES_DIR}/qml/")
    file(REMOVE_RECURSE "${BIN_DIR}/qml")
    message(STATUS "合并 bin/qml -> Resource/qml")
endif()

# 3) windeployqt 若在 bin 根生成了 translations/ 残留（未走 --translationdir 的部分）
if(EXISTS "${BIN_DIR}/translations")
    file(COPY "${BIN_DIR}/translations/" DESTINATION "${RES_DIR}/translations/")
    file(REMOVE_RECURSE "${BIN_DIR}/translations")
    message(STATUS "合并 bin/translations -> Resource/translations")
endif()

# 4) 生成 qt.conf：Qt 在 QApplication 构造时自动读取 exe 同目录的 qt.conf，
#    把 plugins/qml/resources/translations 等查找路径一次性指到 bin/Resource 下。
#    - Prefix        → .                （相对 exe 目录，作为其余路径的基准）
#    - Libraries     → .                （Qt dll 在 bin 根目录）
#    - Plugins       → Resource/plugins （Qt 插件）
#    - QmlImports    → Resource/qml     （QML 模块）
#    - Translations  → Resource/translations（Qt 翻译 + qtwebengine_locales）
#    - Data          → Resource         （WebEngine 资源 = DataPath/resources）
#    注意：路径相对 exe 目录（bin）；QtWebEngineProcess.exe 留在 bin 根目录。
file(WRITE "${BIN_DIR}/qt.conf"
"[Paths]
Prefix = .
Libraries = .
Plugins = Resource/plugins
QmlImports = Resource/qml
Translations = Resource/translations
Data = Resource
")
message(STATUS "生成 bin/qt.conf（plugins/qml/translations/data → Resource）")
