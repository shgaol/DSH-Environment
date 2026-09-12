#include "MainWindow.h"
#include "NavBar/UINavBarItem.h"
#include "NavBar/UINavBar.h"
#include "DSHSrcManager.h"
#include "DSWebViewWindow.h"
#include "DSHChatWindow/DSHChatWindow.h"
#include "DSCmdView.h"
#include "WebApplet/DSWebAppletPage.h"
#include "WebApplet/DSWebAppletStore.h" // 小程序图标（作为 MDI tab 图标的兜底）

#include <QApplication>
#include <QCloseEvent>
#include <QEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QSize>
#include <QStyle>
#include <QStyleFactory>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#ifdef DSH_HAVE_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineView>
#endif

QIcon makeDIcon(const QColor &bg, int size)
{
    return CUINavBarItem::makeLetterIcon(QLatin1Char('D'), bg, size);
}

namespace {
// 兜底图标底色（与“网页小程序”表页里的字母图标同色）
const QColor kAppletIconColor(0x25, 0x63, 0xEB);

// 小程序窗口刚打开时的 tab 图标：
//   1) 已保存过该网页的图标（webapplets/icons/<uuid>.png）→ 直接用它；
//   2) 还没取到图标（或图标文件丢了）→ 用名称首字图标兜底，保证 tab 上一定有图标。
// 之后网页自己的 favicon 解出来后（CDSWebViewWindow::faviconChanged）会替换成它，
// 最终 tab 图标与网页图标一致。
QIcon appletTabIcon(const QString &name, const QString &url)
{
    const QList<DSWebApplet> applets = CDSWebAppletStore::load();
    for (const DSWebApplet &applet : applets) {
        if (applet.name != name || applet.url != url) {
            continue; // 名称与网址都一致才算同一个快捷方式
        }
        const QString path = CDSWebAppletStore::iconPath(applet.iconFile);
        if (!path.isEmpty() && QFile::exists(path)) {
            const QIcon icon(path);
            if (!icon.isNull()) {
                return icon;
            }
        }
        break;
    }

    QString first = name.left(1);
    if (first.isEmpty()) {
        first = QStringLiteral("W"); // 名称异常为空时的兜底字（与表页一致）
    }
    return CUINavBarItem::makeLetterIcon(first.at(0), kAppletIconColor);
}

#ifdef DSH_HAVE_WEBENGINE
// 让 root 里所有网页视图重新合成一帧。
// 背景：QWebEngineView 内部是 QQuickWidget（离屏渲染到 FBO），而 QMdiArea 的 TabbedView
// 只显示当前子窗口 —— 切换标签时其它子窗口会被隐藏，Qt 在 QWebEngineView::hideEvent 里
// 会把 page 置为不可见（qwebengineview.cpp），渲染随之暂停；切回来时不一定恢复。
// 做法：恢复 page 可见性并解除冻结，再 update() + 1px 尺寸微调强制走一次 resize，
// 逼 WebEngine 重新合成。
// 注意：这里刻意不做 hide()/show()（不做表面重建）—— 反复重建渲染表面会让“切几次标签后
// 表面失效”来得更快；表面真失效时由「刷新」按钮走 CDSWebViewWindow::rebuildView() 重建视图。
void kickWebEngineRenders(QWidget *root)
{
    if (!root) {
        return;
    }
    const QList<QWebEngineView *> views = root->findChildren<QWebEngineView *>();
    for (QWebEngineView *view : views) {
        if (QWebEnginePage *page = view->page()) {
            page->setVisible(true);
            page->setLifecycleState(QWebEnginePage::LifecycleState::Active);
        }
        view->update();
        const QSize size = view->size();
        if (size.isEmpty()) {
            continue;
        }
        view->resize(size.width() + 1, size.height());
        view->resize(size);
    }
}
#endif
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // 中央容器：左侧 CUINavBar + 右侧 QTabWidget
    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 左侧：导航工作区（宽度由 updateNavBarWidth 控制：展开 = 窗口宽 1/6，收起 = 72px）
    m_navigatorBar = new CUINavBar(central);
    layout->addWidget(m_navigatorBar);

    // 右侧：QTabWidget（DSH源码管理）
    m_tabWidget = new QTabWidget(central);
    m_tabWidget->setDocumentMode(true);

    m_srcManager = new CDSHSrcManager;
    connect(m_srcManager, &CDSHSrcManager::webViewRequested,
            this, &MainWindow::openWebView);
    // DSH客户端：按选中记录的端口打开/激活对应地址的对话窗口(带 token 登录并自动连接)
    connect(m_srcManager, &CDSHSrcManager::dshChatRequested,
            this, [this](const QString &port) {
                bool ok = false;
                const int p = port.toInt(&ok);
                const QString tokenUrl = ok && p > 0
                    ? m_srcManager->tokenUrlForPort(static_cast<quint16>(p)) : QString();
                openDshChatAt(QStringLiteral("127.0.0.1:") + port, tokenUrl, true);
            });
    // “以web方式打开服务”：在 MDI 中打开一个 CDSCmdView 终端窗口并执行对应命令
    connect(m_srcManager, &CDSHSrcManager::cmdTerminalRequested,
            this, &MainWindow::openCmdTerminal);
    // “关闭服务”：关闭 MDI 中标题为 profile:端口 的终端窗口
    connect(m_srcManager, &CDSHSrcManager::closeCmdTerminalRequested,
            this, &MainWindow::closeCmdTerminal);
    m_tabWidget->addTab(m_srcManager, QStringLiteral("DSH源码管理"));

    // ---- “应用”页：QMdiArea（Tab 模式、带关闭按钮、Fusion 主题）----
    m_appMdiArea = new QMdiArea;
    m_appMdiArea->setViewMode(QMdiArea::TabbedView); // Tab 模式
    // 开启 Tab 关闭按钮：点击 × 时 QMdiArea 内部自动关闭对应子窗口
    // （子窗口设置了 WA_DeleteOnClose，关闭后 destroyed 信号会清理映射）
    m_appMdiArea->setTabsClosable(true);
    // 子窗口尺寸由我们自己管（每个窗口都用 showMaximized() 打开），
    // 关掉“激活时自动最大化/还原”这一步：它会随每次切标签对子窗口做一次
    // showNormal()/showMaximized()，等于给里面的网页视图又多加一轮隐藏/显示与缩放，
    // 而网页视图（QWebEngineView）恰恰最怕这种反复折腾。
    m_appMdiArea->setOption(QMdiArea::DontMaximizeSubWindowOnActivation, true);
    // Fusion 主题（只作用于该 MDI 区域及其子窗口，不影响程序其他部分外观）
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        fusion->setParent(m_appMdiArea); // 生命周期交给 MDI 区域管理
        m_appMdiArea->setStyle(fusion);
    }
    // 限制 MDI 内部 tab 栏大小：tab 高度压缩到接近标题栏，紧凑显示；
    // tab 宽度与标题文本长度一致（qproperty-expanding:false 关闭拉伸，
    // 去掉固定 min-width，宽度由标题文本 + padding 决定）
    // （QMdiArea 的内部 QTabBar 无公开访问器，用 QSS 后代选择器命中）
    m_appMdiArea->setStyleSheet(QStringLiteral(
        "QMdiArea QTabBar {"
        "  min-height: 26px;"
        "  max-height: 26px;"
        "  qproperty-expanding: false;"
        "}"
        "QMdiArea QTabBar::tab {"
        "  height: 22px;"
        "  padding: 0px 8px;"
        "}"
        "QMdiArea QTabBar::close-button {"
        "  margin: 2px;"
        "}"));
    m_tabWidget->addTab(m_appMdiArea, QStringLiteral("应用"));

#ifdef DSH_HAVE_WEBENGINE
    // 切换 MDI 标签（或从“DSH源码管理”页切回“应用”页）时，让当前网页视图重新合成一帧，
    // 避免个别网站停在空白画面上（原因见上面的 kickWebEngineRenders）。
    // 延到事件循环下一轮执行：此时 MDI 已经把子窗口摆好，且避开激活流程中的重入。
    connect(m_appMdiArea, &QMdiArea::subWindowActivated, this,
            [this](QMdiSubWindow *) {
                QTimer::singleShot(0, this, [this]() {
                    if (QMdiSubWindow *active = m_appMdiArea->activeSubWindow()) {
                        kickWebEngineRenders(active->widget());
                    }
                });
            });
    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int) {
        if (m_tabWidget->currentWidget() != m_appMdiArea) {
            return; // 只看“切回应用页”
        }
        QTimer::singleShot(0, this, [this]() {
            if (QMdiSubWindow *active = m_appMdiArea->activeSubWindow()) {
                kickWebEngineRenders(active->widget());
            }
        });
    });
#endif

    layout->addWidget(m_tabWidget, 1); // 右侧占满剩余空间

    setCentralWidget(central);
    setWindowTitle(QStringLiteral("DSH-Environment"));
    resize(1100, 700);

    // 导航栏展开/收起时同步宽度（展开 = 窗口宽 1/6，收起 72px）
    connect(m_navigatorBar->navBar(), &CUINavBarItem::expandedChanged,
            this, [this](bool) { updateNavBarWidth(); });

    // 侧边栏默认展开：由 main.cpp 在显示窗口前调用
    // window.navigatorBar()->navBar()->setExpanded(true)（会经上面的连接同步宽度）
    updateNavBarWidth();

    // ---- 系统托盘：关闭/最小化隐藏到托盘，右键“显示/退出” ----
    m_trayMenu = new QMenu(this);
    QAction *showAct = m_trayMenu->addAction(QStringLiteral("显示"));
    QAction *quitAct = m_trayMenu->addAction(QStringLiteral("退出"));
    connect(showAct, &QAction::triggered, this, &MainWindow::showWindow);
    connect(quitAct, &QAction::triggered, this, &MainWindow::quitApplication);

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(makeDIcon(QColor(0x25, 0x63, 0xEB))); // 蓝色 D 图标
    m_trayIcon->setToolTip(QStringLiteral("DSH-Environment"));
    m_trayIcon->setContextMenu(m_trayMenu);
    // 双击托盘图标：显示主窗口
    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick) {
                    showWindow();
                }
            });
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon->show();
    }
    // 真正退出时先移除托盘图标, 避免进程退出后图标残留残影
    // (Windows 上常要鼠标移到托盘区才刷新消失)。挂在 aboutToQuit
    // 以覆盖一切合法退出路径(托盘“退出”/ quit() 等)。
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        if (m_trayIcon)
            m_trayIcon->hide();
    });
}

MainWindow::~MainWindow() = default;

CUINavBar *MainWindow::navigatorBar() const
{
    return m_navigatorBar;
}

void MainWindow::openWebView(const QString &url)
{
    // 以网址为唯一标识:同一网址只保留一个网页窗口
    const QString key = url;

    // 已存在 → 激活该网址对应的网页窗口(并切到“应用”页)
    if (QMdiSubWindow *existing = m_webWindows.value(key, nullptr)) {
        m_tabWidget->setCurrentWidget(m_appMdiArea);
        m_appMdiArea->setActiveSubWindow(existing);
        existing->show();
        existing->raise();
        return;
    }

    // 新建网页窗口(在 MDI 区域内最大化),以网址为标题
    auto *view = new CDSWebViewWindow;
    QMdiSubWindow *sub = m_appMdiArea->addSubWindow(view);
    sub->setWindowTitle(key);
    sub->setAttribute(Qt::WA_DeleteOnClose); // 关闭时真正销毁(触发 destroyed 清理映射)
    // 网页实际跳转(如 token→cookie 交换后加载干净的 /)时,让 MDI 标题跟随当前网址
    connect(view, &CDSWebViewWindow::urlChanged, this, [sub](const QString &u) {
        sub->setWindowTitle(u);
    });
    view->openUrl(url);
    sub->showMaximized();
    m_webWindows.insert(key, sub);

    // 子窗口销毁时同步移除映射,下次打开同网址会重新创建
    connect(sub, &QObject::destroyed, this, [this, key, sub](QObject *) {
        if (m_webWindows.value(key) == sub)
            m_webWindows.remove(key);
    });

    // 切到“应用”页
    m_tabWidget->setCurrentWidget(m_appMdiArea);
}

void MainWindow::openDshChat()
{
    // “DSH 客户端”按钮:仅默认地址,不自动连接,由用户手工录入并点“连接”。
    openDshChatAt(QStringLiteral("127.0.0.1:8888"), QString(), false);
}

// address   host:port;  tokenUrl  带 token 的登录网址(可为空);
// autoConnect  是否连接后自动开始连接(源码管理右键打开 = true; 手工打开 = false)。
void MainWindow::openDshChatAt(const QString &address, const QString &tokenUrl, bool autoConnect)
{
    // 以“客户端: 地址”为标识;重复则激活,否则新增
    const QString key = QStringLiteral("客户端: ") + address;
    if (QMdiSubWindow *existing = m_webWindows.value(key, nullptr)) {
        m_tabWidget->setCurrentWidget(m_appMdiArea);
        m_appMdiArea->setActiveSubWindow(existing);
        existing->show();
        existing->raise();
        return;
    }
    auto *chat = new CDSHChatWindow(address, tokenUrl, autoConnect);
    QMdiSubWindow *sub = m_appMdiArea->addSubWindow(chat);
    sub->setWindowTitle(key);
    sub->setAttribute(Qt::WA_DeleteOnClose);
    sub->showMaximized();
    m_webWindows.insert(key, sub);
    connect(sub, &QObject::destroyed, this, [this, key, sub](QObject *) {
        if (m_webWindows.value(key) == sub)
            m_webWindows.remove(key);
    });
    m_tabWidget->setCurrentWidget(m_appMdiArea);
}

void MainWindow::openWebApplets()
{
    // 表页标题固定为“网页小程序”，同时作为 MDI 去重标识：
    // 已打开则激活，未打开才新建（表页里的数据来自 JSON，关闭再打开不会丢）
    const QString key = QStringLiteral("网页小程序");
    if (QMdiSubWindow *existing = m_webWindows.value(key, nullptr)) {
        m_tabWidget->setCurrentWidget(m_appMdiArea);
        m_appMdiArea->setActiveSubWindow(existing);
        existing->show();
        existing->raise();
        return;
    }

    auto *page = new CDSWebAppletPage;
    // 表页里双击小程序 → 在 MDI 中打开对应网页（按小程序名称去重）
    connect(page, &CDSWebAppletPage::openRequested,
            this, &MainWindow::openWebAppletWindow);
    QMdiSubWindow *sub = m_appMdiArea->addSubWindow(page);
    sub->setWindowTitle(key);
    sub->setAttribute(Qt::WA_DeleteOnClose); // 关闭时真正销毁(触发 destroyed 清理映射)
    sub->showMaximized();
    m_webWindows.insert(key, sub);

    connect(sub, &QObject::destroyed, this, [this, key, sub](QObject *) {
        if (m_webWindows.value(key) == sub)
            m_webWindows.remove(key);
    });

    m_tabWidget->setCurrentWidget(m_appMdiArea);
}

// 网页小程序窗口：在“应用”页 MDI 中打开小程序网址。
// 行为与内置站点预设窗口（原来的 DeepSeek / 今日头条 / GitHub）完全一致：
// - **profile（登录信息 + cache）按网址复用**：小程序网址若属于内置站点预设
//   （deepseek.com / toutiao.com / github.com），就用该预设的类型与 profile ——
//   与原来侧边栏按钮打开的窗口是同一份数据（configure/deepseek-web 等），因此不用重新登录；
//   其它网址用网页小程序自己的 profile（configure/webapplet-web）；
// - 数据都落在「文档/DSH-Environment/configure」下、与 Edge 隔离，
//   窗口内「登录数据」按钮可直达该目录；
// - 站内链接（小程序自己的站点，如 chat.deepseek.com → deepseek.com）在窗口内导航，
//   站外链接交给外部浏览器（Edge）打开，右键菜单也有“使用默认浏览器打开链接”；
// - MDI 标题用小程序名称（不跟随网址），按小程序名称去重，重复双击只激活已打开的窗口。
void MainWindow::openWebAppletWindow(const QString &name, const QString &url)
{
    const QString key = QStringLiteral("小程序: ") + name;
    if (QMdiSubWindow *existing = m_webWindows.value(key, nullptr)) {
        m_tabWidget->setCurrentWidget(m_appMdiArea);
        m_appMdiArea->setActiveSubWindow(existing);
        existing->show();
        existing->raise();
        return;
    }

    // 网址属于内置站点（DeepSeek / 今日头条 / GitHub）→ 复用该站点类型：
    // profile 与侧边栏按钮打开的窗口完全相同，登录状态直接沿用，不必再登录一次。
    const CDSWebProfileKind kind = CDSWebViewWindow::kindForUrl(url);
    // 第三个参数：本窗口的“站内域名后缀”（由小程序网址推出），
    // 与内置站点预设里 DeepSeek 用 deepseek.com、今日头条用 toutiao.com 同理
    auto *view = new CDSWebViewWindow(kind, nullptr, CDSWebAppletPage::siteHostSuffix(url));
    QMdiSubWindow *sub = m_appMdiArea->addSubWindow(view);
    sub->setWindowTitle(name); // 表页里的快捷方式名称
    // tab 图标：先放“已保存的网页图标 / 名称首字兜底图标”，保证 tab 上立刻有图标；
    // 网页自己的 favicon 解出来后（下面的 faviconChanged）替换成它 —— 最终 tab 图标与网页图标一致。
    const QIcon fallbackIcon = appletTabIcon(name, url);
    sub->setWindowIcon(fallbackIcon);
    connect(view, &CDSWebViewWindow::faviconChanged, sub, [sub, fallbackIcon](const QIcon &icon) {
        if (icon.cacheKey() == fallbackIcon.cacheKey()) {
            return; // 与当前图标相同：不必重复刷新 tab
        }
        sub->setWindowIcon(icon);
    });
    sub->setAttribute(Qt::WA_DeleteOnClose); // 关闭时真正销毁(触发 destroyed 清理映射)
    // 注意：不连接 urlChanged 改标题——小程序窗口标题保持快捷方式名称，不跟随网址。
    view->openUrl(url);
    sub->showMaximized();
    m_webWindows.insert(key, sub);

    connect(sub, &QObject::destroyed, this, [this, key, sub](QObject *) {
        if (m_webWindows.value(key) == sub)
            m_webWindows.remove(key);
    });

    m_tabWidget->setCurrentWidget(m_appMdiArea);
}

void MainWindow::openCmdView()
{
    // 以标题为唯一标识;重复则激活已存在的终端窗口
    const QString key = QStringLiteral("终端");
    if (QMdiSubWindow *existing = m_webWindows.value(key, nullptr)) {
        m_tabWidget->setCurrentWidget(m_appMdiArea);
        m_appMdiArea->setActiveSubWindow(existing);
        existing->show();
        existing->raise();
        return;
    }

    auto *cmd = new CDSCmdView;
    QMdiSubWindow *sub = m_appMdiArea->addSubWindow(cmd);
    sub->setWindowTitle(key);
    sub->setAttribute(Qt::WA_DeleteOnClose);
    // 启动 cmd.exe(CDSCmdView 底层为 ConPTY 伪控制台)
    cmd->start();
    sub->showMaximized();
    m_webWindows.insert(key, sub);

    connect(sub, &QObject::destroyed, this, [this, key, sub](QObject *) {
        if (m_webWindows.value(key) == sub)
            m_webWindows.remove(key);
    });

    m_tabWidget->setCurrentWidget(m_appMdiArea);
}

void MainWindow::openCmdTerminal(const QString &title, const QString &workingDir,
                                 const QStringList &lines)
{
    // 以标题为唯一标识;重复则激活已存在的终端窗口
    if (QMdiSubWindow *existing = m_webWindows.value(title, nullptr)) {
        m_tabWidget->setCurrentWidget(m_appMdiArea);
        m_appMdiArea->setActiveSubWindow(existing);
        existing->show();
        existing->raise();
        return;
    }

    auto *cmd = new CDSCmdView;
    QMdiSubWindow *sub = m_appMdiArea->addSubWindow(cmd);
    sub->setWindowTitle(title);
    sub->setAttribute(Qt::WA_DeleteOnClose);
    // 在指定工作目录启动, 并执行这些命令(如 pnpm dsh --profile web ...)
    cmd->startScript(workingDir, lines);
    // “开启服务”(命令带 --no-open, dsh 不会自动开浏览器)时,
    // 服务启动后终端会打印 "dsh web: <带token网址>" 行, 用程序内网页窗口打开对应网站。
    if (lines.join(QLatin1Char(' ')).contains(QLatin1String("--no-open"))) {
        connect(cmd, &CDSCmdView::webUrlCaptured, this, [this](const QString &url) {
            if (m_srcManager)
                m_srcManager->recordWebUrl(url); // 记录带 token 网址(供“打开网站”等使用)
            openWebView(url);
        });
    }
    sub->showMaximized();
    m_webWindows.insert(title, sub);

    // 子窗口销毁时移除映射; 仅当映射仍指向本窗口时才移除,
    // 避免“先关旧窗紧接重开同名新窗”时旧窗销毁误删新窗的条目
    connect(sub, &QObject::destroyed, this, [this, title, sub](QObject *) {
        if (m_webWindows.value(title) == sub)
            m_webWindows.remove(title);
    });

    m_tabWidget->setCurrentWidget(m_appMdiArea);
}

void MainWindow::closeCmdTerminal(const QString &title)
{
    // 找到标题为 title 的终端窗口, 先从去重表移除(避免重开时复用), 再关闭
    if (QMdiSubWindow *sub = m_webWindows.take(title)) {
        sub->close(); // WA_DeleteOnClose → 真正销毁(内部 ConPTY 关闭, 服务停止)
    }
}

void MainWindow::showWindow()
{
    // 从最小化/隐藏状态恢复显示（保留之前的最大化状态）
    setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
    raise();
    activateWindow();
}

void MainWindow::quitApplication()
{
    // 真正的退出：会触发 aboutToQuit(已挂接隐藏托盘图标), 并自动关闭所有已登记的服务
    QApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 关闭 = 隐藏到托盘，不真正退出
    hide();
    event->ignore();
}

void MainWindow::changeEvent(QEvent *event)
{
    // 最小化 = 隐藏到托盘
    if (event->type() == QEvent::WindowStateChange && isMinimized()) {
        QTimer::singleShot(0, this, [this]() { hide(); });
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::updateNavBarWidth()
{
    const bool expanded = m_navigatorBar->navBar()->isExpanded();
    // 展开时导航栏宽度 = 主窗口宽度的 1/6；收起时固定 72px
    m_navigatorBar->setFixedWidth(expanded ? width() / 6 : 72);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateNavBarWidth();
}
