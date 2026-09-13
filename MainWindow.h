#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "DSWebViewWindow.h" // CDSWebViewWindow / CDSWebProfileKind（DSH 服务网页窗口等）

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QMainWindow>
#include <QString>
#include <QStringList>

class CUINavBar;
class CDSHChatWindow;
class CDSHSrcManager;
class CDSCmdView;
class QCloseEvent;
class QEvent;
class QMenu;
class QMdiArea;
class QMdiSubWindow;
class QResizeEvent;
class QSystemTrayIcon;
class QTabWidget;

// 生成应用图标：蓝色圆角方块 + 白色 D 字
QIcon makeDIcon(const QColor &bg, int size = 64);

// 主窗口：左侧 CUINavBar 导航 + 右侧 QTabWidget（DSH源码管理 / 应用）
// “应用”页内嵌 QMdiArea（Tab 模式、带关闭按钮、Fusion 主题），
// 网页请求在 MDI 内打开子窗口；以端口为唯一标识，同一端口只打开一个，
// 重复打开则激活已存在的网页窗口
// 关闭/最小化 → 隐藏到系统托盘；托盘右键“显示/退出”，只有“退出”才是真正退出
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // 获取左侧导航工作区（便于外部连接信号）
    CUINavBar *navigatorBar() const;

    // 打开“DSH 对话”窗口（仿 DSH 界面，通过 WebSocket/HTTP 访问 DSH 服务）
    void openDshChat();
    // 打开指定地址的 DSH 对话窗口（按 address 去重，重复则激活）。
    // tokenUrl 为带 token 的登录网址(可为空); autoConnect=是否连接后自动开始连接。
    void openDshChatAt(const QString &address, const QString &tokenUrl = QString(), bool autoConnect = false);
    // 打开类似 CMD 的终端视图窗口（CDSCmdView，按标题去重，重复则激活）
    void openCmdView();
    // 在 MDI 中打开一个 CDSCmdView 终端窗口，标题为 title，工作目录 workingDir，
    // 并按序执行 lines 命令（按 title 去重，重复则激活）
    void openCmdTerminal(const QString &title, const QString &workingDir,
                         const QStringList &lines);
    // 关闭 MDI 中标题为 title 的终端窗口（“关闭服务”先关窗口用；从去重表移除再关闭）
    void closeCmdTerminal(const QString &title);

private slots:
    // 打开网页：在“应用”页 QMdiArea 中打开子窗口（按端口去重）
    void openWebView(const QString &url);
    // 托盘菜单：显示主窗口
    void showWindow();
    // 托盘菜单：真正退出程序
    void quitApplication();

protected:
    // 窗口尺寸变化时同步导航栏宽度（展开 = 窗口宽 1/6，收起 72px）
    void resizeEvent(QResizeEvent *event) override;
    // 关闭 = 隐藏到托盘（不退出）
    void closeEvent(QCloseEvent *event) override;
    // 最小化 = 隐藏到托盘
    void changeEvent(QEvent *event) override;

private:
    // 导航栏宽度：展开时 = 主窗口宽度的 1/6，收起时 72px
    void updateNavBarWidth();

    CUINavBar *m_navigatorBar = nullptr; // 左侧导航工作区
    QTabWidget *m_tabWidget = nullptr;   // 右侧标签页容器
    CDSHSrcManager *m_srcManager = nullptr; // DSH 源码管理
    QMdiArea *m_appMdiArea = nullptr;    // “应用”页：MDI 区域（Tab 模式）
    QHash<QString, QMdiSubWindow *> m_webWindows; // 端口/网址 → 网页子窗口
    QSystemTrayIcon *m_trayIcon = nullptr;  // 系统托盘图标
    QMenu *m_trayMenu = nullptr;            // 托盘右键菜单（显示 / 退出）
    CDSHChatWindow *m_dshChatWindow = nullptr; // “DSH 对话”窗口（懒创建，关闭=隐藏）
};

#endif // MAINWINDOW_H
