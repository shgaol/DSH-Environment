#include "Application.h"

#include "NavBar/UINavBar.h"
#include "NavBar/UINavBarItem.h" // navBar()->setExpanded(...) 需要完整类型
#include "EnvSettingDlg.h"
#include "MainWindow.h"

#include <QCoreApplication>
#include <QDebug>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTranslator>
#include <cstdio>
#include <string>

// Qt WebView 仅 Qt6 支持（Qt5 的 QWebView 是 QQuickItem，无法嵌入 QWidget）
#if defined(DSH_HAVE_WEBVIEW) && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtWebView/QtWebView>
#endif

// Win32 API（放在 Qt 头文件之后，避免宏冲突）
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// 用 Windows API 获取程序所在目录（GetModuleFileNameW）
QString getExeDirByWinApi()
{
    std::wstring buf(MAX_PATH, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, &buf[0], DWORD(buf.size()));
    // 路径过长时自动扩容重试
    while (len == buf.size() && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
        buf.resize(buf.size() * 2);
        len = GetModuleFileNameW(nullptr, &buf[0], DWORD(buf.size()));
    }
    buf.resize(len);

    QString path = QString::fromWCharArray(buf.c_str());
    // 去掉文件名部分，只留目录
    const int idx = path.lastIndexOf(QLatin1Char('\\'));
    if (idx >= 0) {
        path = path.left(idx);
    }
    return path;
}

// 基于 Windows API 的目录设置函数（需求9），必须在 QApplication 创建之前调用：
//   1) 获取程序所在目录；
//   2) 若 程序目录/Resource 存在，则把所有与直接调用的 dll 无关的目录
//      （工作目录、plugins、qml、resources、translations 等）都设置到
//      程序目录/Resource 下面；
//   3) 若 Resource 不存在，则不设置（返回 false）。
// 说明：直接调用的 dll（Qt6Core.dll 等）仍从 exe 所在目录（bin）加载，
//       该目录不在此处改动。
// 注意：plugins/qml/resources/translations 的查找路径不需要在此设置环境变量，
//       部署时 bin 根目录的 qt.conf（[Paths] 段）会在 QApplication 构造时
//       被 Qt 自动读取并指向 bin/Resource 下的对应目录；此处只负责
//       Windows API 部分：程序目录获取、Resource 存在性判断、工作目录切换。
bool setupResourceDirByWinApi()
{
    // 1. 获取程序所在目录（GetModuleFileNameW）
    const QString appDir = getExeDirByWinApi();
    if (appDir.isEmpty())
        return false;

    // 2. 拼接 程序目录/Resource
    const std::wstring resDir = (appDir + QStringLiteral("/Resource")).toStdWString();

    // 3. Resource 不存在 → 不设置
    const DWORD attr = GetFileAttributesW(resDir.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
        return false;

    // 4. 工作目录 → 程序目录/Resource（所有相对路径文件操作都落在 Resource 下）
    SetCurrentDirectoryW(resDir.c_str());

    return true;
}

// 自定义消息处理：屏蔽 Qt 内部噪音，其余照常输出
//  - "WindowDoesNotAcceptFocus"：QWebView 不接收焦点导致的 requestActivate 提示
//  - "must be a top level window"：QWindow::setTransientParent 对标签页内窗口的内部提示
void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    Q_UNUSED(type);
    Q_UNUSED(ctx);
    if (msg.contains(QStringLiteral("WindowDoesNotAcceptFocus"))
        || msg.contains(QStringLiteral("must be a top level window"))) {
        return;
    }
    fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
}

int main(int argc, char *argv[])
{
    // 程序翻译器：在 QApplication 创建之前 load 翻译文件（load 不依赖 app 实例），
    // 创建之后 installTranslator（该函数需要 app 实例）
    QTranslator appTranslator;

    qInstallMessageHandler(messageHandler);

    // Qt WebEngine 的网页视图内部是 QQuickWidget（离屏渲染）。应用里同时存在多个网页视图
    // （每个网页小程序窗口一个）时，必须在创建 QApplication 之前打开“共享 OpenGL 上下文”，
    // 否则个别视图在隐藏/显示后渲染表面可能失效：切回标签时网页变成空白，且刷新也刷不出来
    // （页面内容还在，只是没有新的合成画面）。放在这里是因为该属性只能在 QApplication
    // 创建之前设置，之后再设无效。
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

#if defined(DSH_HAVE_WEBVIEW) && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Qt WebView 初始化：必须在创建 QApplication 之前调用，否则 QWebView 无法工作
    QtWebView::initialize();
#endif

    // 需求9：在 QApplication 创建之前，若 程序目录/Resource 存在，
    // 则把所有与直接调用的 dll 无关的目录（工作目录、qml、resources、
    // translations、plugins 等）都设置到 程序目录/Resource 下面；否则不设置。
    setupResourceDirByWinApi();

    // ---- 在 QApplication 创建之前，按各目录对应的 Qt API 分别设置 ----
    // Qt6 中每个路径类型的设置函数不同，且调用时机不同：
    //   1) plugins       → QCoreApplication::addLibraryPath()（静态，app 创建前可调用）
    //   2) translations  → QTranslator::load() 在 app 创建前即可加载文件；
    //                      安装到应用需 app 实例，故 installTranslator 放在 app 创建后
    //   3) qml           → QQmlEngine::addImportPath()（实例方法，需引擎实例，
    //                      本程序不使用 QML，无需设置；若以后引入 QML 再调用）
    //   4) Data/WebEngine → Qt6 无公开 C++ API 设置资源路径（仅环境变量或 qt.conf，
    //                      已按要求不采用），开发环境从 Qt 安装目录可找到
    const QString appDir = getExeDirByWinApi();
    if (!appDir.isEmpty()) {
        // 1) Qt 插件目录 → 程序目录/Resource/plugins
        QCoreApplication::addLibraryPath(appDir + QStringLiteral("/Resource/plugins"));

        // 2) 翻译文件 → 程序目录/Resource/translations
        //    （load 不依赖 app 实例，QApplication 创建之前即可调用）
        appTranslator.load(appDir + QStringLiteral("/Resource/translations/qt_zh_CN.qm"));
    }

    CApplication app(argc, argv);

    // 安装翻译：QCoreApplication::installTranslator 需要 app 实例，故在创建后调用
    QCoreApplication::installTranslator(&appTranslator);

    // ---- 单实例：程序只能同时打开一个 ----
    // 用命名管道检测：能连上说明已有实例在运行（QtNetwork，已链接）
    const QString singletonKey = QStringLiteral("DSH-Environment-SingleInstance");
    QLocalSocket probe;
    probe.connectToServer(singletonKey);
    if (probe.waitForConnected(300)) {
        // 已有实例：通知它显示主窗口，本实例直接退出
        probe.write("show");
        probe.flush();
        probe.waitForBytesWritten(300);
        probe.disconnectFromServer();
        return 0;
    }

    // 程序图标：从 exe 内嵌资源加载（:/app.ico，与资源管理器中 exe 的图标一致）
    app.setWindowIcon(QIcon(QStringLiteral(":/app.ico")));

    // 程序退出时：自动关闭所有已登记的服务
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, []() {
        CApplication::instance()->killAllServices();
    });

    // 启动时读取环境设置（JSON 文件存在则读取，不存在则跳过）
    app.loadEnvSettings();

    // 全局深色 ToolTip 样式（配合左侧导航栏的深色主题）
    app.setStyleSheet(QStringLiteral(
        "QToolTip {"
        "  background-color: #1F2229;"
        "  color: #E6E8EB;"
        "  border: 1px solid #3A3F47;"
        "  padding: 4px 8px;"
        "}"));

    MainWindow window;

    // ---- 单实例服务端：收到其他实例的 "show" 消息 → 显示主窗口 ----
    // 若 listen 失败（极端情况：管道残留或权限问题），程序照常运行，仅失去激活能力
    QLocalServer singletonServer;
    QLocalServer::removeServer(singletonKey); // 清理上次异常退出可能残留的管道
    if (singletonServer.listen(singletonKey)) {
        QObject::connect(&singletonServer, &QLocalServer::newConnection, &app,
                         [&singletonServer, &window, &app]() {
            while (singletonServer.hasPendingConnections()) {
                QLocalSocket *sock = singletonServer.nextPendingConnection();
                QObject::connect(sock, &QLocalSocket::readyRead, &app,
                                 [sock, &window]() {
                    const QByteArray msg = sock->readAll();
                    sock->deleteLater();
                    if (msg.contains("show")) {
                        // 激活已有实例的主窗口（从托盘/最小化恢复）
                        QMetaObject::invokeMethod(&window, "showWindow");
                    }
                });
            }
        });
    }

    // ---- 第 0 部分（图标 + 三行文本）----
    window.navigatorBar()->SetInfoCode(QStringLiteral("DSH"));
    window.navigatorBar()->SetInfoName(QStringLiteral("DSH-Environment"));
    window.navigatorBar()->SetInfoText(QStringLiteral("作者:shgaol"));
    // 第 0 部分图标：蓝色 D 字图标
    window.navigatorBar()->SetInfoIcon(makeDIcon(QColor(0x25, 0x63, 0xEB)));

    // ---- 第 1 部分按钮 ----
    const QString envId = window.navigatorBar()->AddTopBtn(QStringLiteral("环境"));

    // 点击“环境”按钮：弹出环境设置对话框（模态）
    QObject::connect(window.navigatorBar(), &CUINavBar::topbtnClicked, &window,
                     [&window, envId](const QString &id) {
                         if (id == envId) {
                             CEnvSettingDlg dlg(&window);
                             dlg.exec();
                         }
                     });

    // “对话”按钮：打开仿 DSH 的对话窗口（通过 WebSocket/HTTP 访问 DSH 服务）
    // “DSH 客户端”按钮：在“应用”页 MDI 区域新增一个 DSH 客户端窗口(每次新增)
    const QString chatId = window.navigatorBar()->AddTopBtn(QStringLiteral("DSH 客户端"));
    QObject::connect(window.navigatorBar(), &CUINavBar::topbtnClicked, &window,
                     [&window, chatId](const QString &id) {
                         if (id == chatId) {
                             window.openDshChat();
                         }
                     });

    // “终端”按钮：打开类似 CMD 的终端视图窗口（CDSCmdView，ConPTY 伪控制台）
    const QString cmdId = window.navigatorBar()->AddTopBtn(QStringLiteral("终端"));
    QObject::connect(window.navigatorBar(), &CUINavBar::topbtnClicked, &window,
                     [&window, cmdId](const QString &id) {
                         if (id == cmdId) {
                             window.openCmdView();
                         }
                     });

    // “网页小程序”按钮：在“应用”页 MDI 中打开“网页小程序”表页
    // （表页里可增加/修改/删除网页快捷方式，双击打开对应网页；重复点击只激活已存在的表页）
    // 说明：原来的 DeepSeek / 今日头条 / GitHub-shgaol 三个按钮已移除，
    //       这些站点改为在“网页小程序”里登记快捷方式打开（登录信息沿用 configure
    //       下原有的 deepseek-web / toutiao-web / github-shgaol-web 目录，不用重新登录）。
    const QString appletId = window.navigatorBar()->AddTopBtn(QStringLiteral("网页小程序"));
    QObject::connect(window.navigatorBar(), &CUINavBar::topbtnClicked, &window,
                     [&window, appletId](const QString &id) {
                         if (id == appletId) {
                             window.openWebApplets();
                         }
                     });

    // 侧边栏默认展开（宽度 = 主窗口宽度的 1/6，见 MainWindow::updateNavBarWidth；
    // 收起状态下为 72px 窄条，可点击导航栏上的按钮手动收/展）
    window.navigatorBar()->navBar()->setExpanded(true);

    window.showMaximized(); // 启动后按单个屏幕最大化（不跨屏）

    return app.exec();
}
