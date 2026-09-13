#ifndef DSWEBVIEWWINDOW_H
#define DSWEBVIEWWINDOW_H

#include <QIcon>
#include <QMainWindow>
#include <QString>

class QLineEdit;
class QToolButton;
class QUrl;
class QWidget;

// 网页窗口使用的持久化数据（profile）种类：
//  - DshService：本机 DSH 服务网页（127.0.0.1:port）。登录靠 token→cookie 交换，
//                每次打开都重新登录，数据用应用共享 profile "dsh-web"。
//  - DeepSeek / Toutiao / GitHub：**内置站点 profile 预设**（DeepSeek 官网、今日头条官网、
//                GitHub 个人主页）。这三个站点原先各有导航栏按钮，按钮已移除；
//                预设保留了各自的专属 profile 与数据目录（deepseek-web / toutiao-web /
//                github-shgaol-web），目前没有调用方（预留给以后再用）。
//
// 除 DshService 外都是“外部网站窗口”，与 DshService 的差别：
//   1) 登录信息（cookie / localStorage / IndexedDB / 站点权限 / 缓存）显式落在
//      「文档/DSH-Environment/configure/<站点目录>」下的专属 profile 里，
//      与 Edge 等系统浏览器完全隔离 —— 清理 Edge 缓存/Cookie 不会影响这里的登录状态；
//      备份该目录即可带走/迁移登录信息；
//   2) 站外链接（如回答里的「引用」来源）点击后交给外部浏览器（Edge）打开，
//      内嵌窗口留在当前页面（见 CDSWebEnginePage）；
//   3) 窗口工具栏多一个「登录数据」按钮，直接打开上面那个数据目录。
enum class CDSWebProfileKind {
    DshService, // 本机 DSH 服务（默认）
    DeepSeek,   // 内置站点预设：DeepSeek 官网（数据目录 deepseek-web）
    Toutiao,    // 内置站点预设：今日头条官网（数据目录 toutiao-web）
    GitHub,     // 内置站点预设：GitHub 个人主页（数据目录 github-shgaol-web）
};

#ifdef DSH_HAVE_WEBENGINE
class QWebEngineProfile;
#endif

// Web 视图窗口：继承 QMainWindow
// - 编译时检测到 Qt WebEngine（DSH_HAVE_WEBENGINE）→ 用 QWebEngineView 显示网页
// - 否则检测到 Qt WebView（DSH_HAVE_WEBVIEW，Windows 上基于 Edge WebView2）→ 用 QWebView
// - 两者都没有 → 只读文本占位，保证工程可正常编译运行
class CDSWebViewWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit CDSWebViewWindow(CDSWebProfileKind kind = CDSWebProfileKind::DshService,
                              QWidget *parent = nullptr);
    ~CDSWebViewWindow() override;

    // 打开指定网址（未启用 WebEngine 时显示占位提示）
    void openUrl(const QString &url);
    // 刷新当前网页：内部走 rebuildView()（重建视图 + 重新加载当前网址）
    void reload();
    // 重建网页视图：摘掉旧视图（连同页对象），换一套全新的再按当前网址加载。
    // 用于救回“切标签切多了以后一直空白、刷新也刷不出来”的窗口 ——
    // 网页视图内部是离屏渲染的 QQuickWidget，反复隐藏/显示后那一个视图的渲染表面会失效，
    // 只有重建视图才能恢复（等价于手动“关掉这一页再重新打开”）。
    void rebuildView();

    // 该窗口登录数据所在的磁盘目录（仅“外部网站窗口”有意义；DshService 返回空串）
    QString dataDir() const;
    // 在资源管理器中打开登录数据目录（便于查看/备份登录信息）
    void openDataDir();

    // ---- 各类窗口的静态信息（供 MainWindow 使用）----
    // 窗口标题（未指定标题时的兜底：DeepSeek / 今日头条 等）
    static QString defaultTitle(CDSWebProfileKind kind);
    // 是否为“外部网站窗口”（专属 profile + 站外链接交给 Edge + “登录数据”按钮；
    // 即除 DshService 外的所有类型）
    static bool isExternalSite(CDSWebProfileKind kind);

signals:
    // 网页加载到的网址变化时发出(用于让外层 MDI 标题保持一致)
    void urlChanged(const QString &url);
    // 网页图标(favicon)变化时发出: 外层 MDI 用它做 tab 图标, 使 tab 图标与网页自身图标一致。
    // 未取到图标(或后端不支持)时不发信号, 外层继续用调用方给的兜底图标。
    void faviconChanged(const QIcon &icon);

private:
    // token→cookie 交换: 用本地 HTTP 请求读取 Set-Cookie 并注入 WebView, 再加载干净的 /。
    // attempt 为重试计数; 服务未就绪时自动重试, 超出次数放弃。
    void tryTokenExchange(const QUrl &tokenUrl, const QUrl &clean, int attempt);
    // 创建本窗口的网页视图（profile/页对象选择 + 信号接线）：
    // 构造函数与 rebuildView() 共用，保证重建出的视图与原来的完全一致。
    QWidget *createWebView(QWidget *parent);

    CDSWebProfileKind m_kind = CDSWebProfileKind::DshService; // 数据(profile)种类
    int m_renderRecoverTries = 0; // 渲染进程异常结束后的自动重建次数（见 createWebView）
    bool m_rebuilding = false;    // 是否正在重建网页视图（防重入）
    QWidget *m_view = nullptr;   // 中央内容（QWebEngineView / 容器 / 占位）
    void *m_webView = nullptr;   // QWebView 指针（QWindow 子类，需 createWindowContainer 包装）
    QLineEdit *m_addrEdit = nullptr; // 地址栏（像 Edge，可输入并显示当前网址）
    QToolButton *m_dataDirBtn = nullptr; // “登录数据”按钮（仅外部网站窗口创建）
};

#endif // DSWEBVIEWWINDOW_H
