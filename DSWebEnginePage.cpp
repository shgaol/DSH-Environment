#include "DSWebEnginePage.h"

#ifdef DSH_HAVE_WEBENGINE

#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QWebEngineNewWindowRequest>
#include <QWebEngineProfile>

namespace {

// 定位 Edge 的 msedge.exe：先查注册表 App Paths（当前用户 → 本机），再查常见安装目录。
// 找不到返回空串（调用方回退到系统默认浏览器）。
QString findEdgeExecutable()
{
    const QString subKey = QStringLiteral(
        "\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe");
    const QStringList roots{
        QStringLiteral("HKEY_CURRENT_USER") + subKey,
        QStringLiteral("HKEY_LOCAL_MACHINE") + subKey,
    };
    for (const QString &root : roots) {
        QSettings registry(root, QSettings::NativeFormat);
        // App Paths 子项的默认值就是可执行文件完整路径
        const QString path = registry.value(QStringLiteral(".")).toString();
        if (!path.isEmpty() && QFileInfo::exists(path)) {
            return path;
        }
    }
    const QStringList dirs{
        qEnvironmentVariable("ProgramFiles(x86)"),
        qEnvironmentVariable("ProgramFiles"),
        qEnvironmentVariable("LOCALAPPDATA"),
    };
    for (const QString &dir : dirs) {
        if (dir.isEmpty()) {
            continue;
        }
        const QString path = dir + QStringLiteral("/Microsoft/Edge/Application/msedge.exe");
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return QString();
}

} // namespace

CDSWebEnginePage::CDSWebEnginePage(const QStringList &internalHostSuffixes,
                                   QWebEngineProfile *profile, QObject *parent)
    : QWebEnginePage(profile, parent)
    , m_internalHostSuffixes(internalHostSuffixes)
{
    // 新窗口/新标签页请求：Qt 默认行为是（QWebEngineView::createWindow 返回 nullptr →
    // newWindowRequested 发出后 request 无人处理）把请求直接丢弃，表现为“点了没反应”。
    // 这里改为：站内链接在内嵌窗口就地导航；站外链接交给外部浏览器（Edge）打开。
    connect(this, &QWebEnginePage::newWindowRequested, this,
            [this](QWebEngineNewWindowRequest &request) {
                const QUrl url = request.requestedUrl();
                if (isInternalUrl(url)) {
                    // 不同步导航：避免在 Chromium 正在处理新窗口请求的过程中重入导航
                    QTimer::singleShot(0, this, [this, url]() { setUrl(url); });
                    return;
                }
                // 不调用 request.openIn(...)，即内嵌窗口不开新窗；交由 Edge 显示
                openInExternalBrowser(url);
            });
}

bool CDSWebEnginePage::isInternalUrl(const QUrl &url) const
{
    const QString scheme = url.scheme().toLower();
    // 非 http(s) 的两种情况（见头文件说明）：
    //   mailto:/tel: → 算站外，交系统默认程序处理；
    //   其余（javascript:/data:/blob:/about: 等页面内部协议）→ 算站内，不拦截。
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
        return scheme != QLatin1String("mailto") && scheme != QLatin1String("tel");
    }

    const QString host = url.host().toLower();
    for (const QString &suffix : m_internalHostSuffixes) {
        const QString s = suffix.toLower();
        if (s.isEmpty()) {
            continue;
        }
        if (host == s || host.endsWith(QLatin1Char('.') + s)) {
            return true;
        }
    }
    return false;
}

void CDSWebEnginePage::openInExternalBrowser(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty()) {
        return;
    }
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("http") || scheme == QLatin1String("https")) {
        const QString edge = findEdgeExecutable();
        // Edge 已在运行时，命令行带网址会被复用为已有窗口里的新标签页
        if (!edge.isEmpty() && QProcess::startDetached(edge, QStringList{url.toString()})) {
            return;
        }
    }
    // 没装 Edge、启动失败，或非 http(s)（mailto:/tel: 等）→ 交回系统默认处理
    QDesktopServices::openUrl(url);
}

bool CDSWebEnginePage::acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame)
{
    // 只拦“用户点击链接”造成的主框架跳转：站内照常在内嵌窗口内导航；
    // 站外（如回答里的「引用」来源）改由外部浏览器打开，内嵌窗口保持当前页面不动。
    // 其它导航类型（程序式加载、表单提交、重定向、后退前进）一律放行，
    // 以免影响站内流程（如登录跳转）。
    if (isMainFrame && type == NavigationTypeLinkClicked && !isInternalUrl(url)) {
        openInExternalBrowser(url);
        return false;
    }
    return QWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}

#endif // DSH_HAVE_WEBENGINE
