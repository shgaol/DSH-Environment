#ifndef DSWEBENGINEPAGE_H
#define DSWEBENGINEPAGE_H

// 本文件仅在启用 Qt WebEngine（DSH_HAVE_WEBENGINE）时参与编译；
// 未启用时整份内容为空，保证工程（WebView 后端 / 无后端）仍可正常构建。
#ifdef DSH_HAVE_WEBENGINE

#include <QStringList>
#include <QWebEnginePage>

class QUrl;
class QWebEngineProfile;

// 网页页对象（QWebEnginePage 子类）：把“站外链接”交给外部浏览器（Edge）打开，
// 而不是把内嵌窗口导航走、或让新窗口请求被静默丢弃。
//
//  - 用户点击的站外链接（主框架、NavigationTypeLinkClicked）
//      → 外部浏览器（Edge）打开，内嵌窗口保持当前页面不动；
//  - target="_blank" / window.open / 中键点击 / 右键“在新标签页中打开链接”
//      （newWindowRequested）
//      → 站外链接同样交给外部浏览器；站内链接在内嵌窗口就地导航（不开新窗、也不丢请求）。
//
// 站内/站外由构造时传入的域名后缀表判定：如 {"deepseek.com"} 表示主机名等于
// "deepseek.com" 或以 ".deepseek.com" 结尾都算站内。
//
// 说明：本类不需要自定义信号/槽（连基类信号时用 lambda + 本对象作 context 即可），
// 因此不声明 Q_OBJECT —— 避免整份内容被 #ifdef 遮蔽时 moc 的处理差异。
class CDSWebEnginePage : public QWebEnginePage
{
public:
    explicit CDSWebEnginePage(const QStringList &internalHostSuffixes,
                              QWebEngineProfile *profile, QObject *parent = nullptr);

    // 是否算“站内”网址：
    //   http/https → 按域名后缀判定；
    //   javascript:/data:/blob:/about: → 页面内部协议，算站内（不拦截，交引擎自己处理）；
    //   mailto:/tel: 等 → 算站外（交系统默认程序处理）。
    bool isInternalUrl(const QUrl &url) const;

    // 用外部浏览器打开网址：优先 Edge（msedge.exe；Edge 已在运行时会被复用为新标签页），
    // 找不到 Edge 或不是 http(s) 网址时回退到系统默认处理（QDesktopServices）。
    static void openInExternalBrowser(const QUrl &url);

protected:
    // 拦截“点击链接”导致的主框架跳转：站外链接交给外部浏览器并取消内嵌跳转
    bool acceptNavigationRequest(const QUrl &url, NavigationType type, bool isMainFrame) override;

private:
    QStringList m_internalHostSuffixes; // 视为“本站”的域名后缀（小写比较）
};

#endif // DSH_HAVE_WEBENGINE

#endif // DSWEBENGINEPAGE_H
