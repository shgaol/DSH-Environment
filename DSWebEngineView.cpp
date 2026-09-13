#include "DSWebEngineView.h"

#ifdef DSH_HAVE_WEBENGINE

#include <QAction>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QMenu>
#include <QUrl>
#include <QVariant>
#include <QWebEngineContextMenuRequest>
#include <QWebEnginePage>

namespace {
// 只有 http/https 才交给系统默认浏览器（javascript:/data: 之类打开没有意义）
bool isOpenableLink(const QUrl &url)
{
    if (!url.isValid() || url.isEmpty()) {
        return false;
    }
    const QString scheme = url.scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https");
}
} // namespace

CDSWebEngineView::CDSWebEngineView(QWidget *parent)
    : QWebEngineView(parent)
{
}

void CDSWebEngineView::contextMenuEvent(QContextMenuEvent *event)
{
    // WebEngine 自己给的 linkUrl 最准（右键时同步可用），但它只认 Chromium 识别出来的链接：
    // 被浮层（透明覆盖层/自定义控件）盖住的链接、SVG 里的 <a>、JS 跳转的元素都拿不到。
    // 所以这里分两步：
    //   ① linkUrl 可用就直接用它（常见情况，零延迟）；
    //   ② 拿不到时菜单项照样加上，触发时再向页面要「右键点到的那个链接」——
    //      由 CDSWebViewWindow 注入的 window.__dshLinkUnderCursor 记录
    //      （页面里用 elementsFromPoint + 向上找 <a> 得到绝对地址）。
    // 注：只认链接（linkUrl），不用 mediaUrl —— 免得右键普通图片也被当成“打开链接”。
    const QWebEngineContextMenuRequest *request = lastContextMenuRequest();
    QUrl knownUrl;
    if (request && isOpenableLink(request->linkUrl())) {
        knownUrl = request->linkUrl();
    }

    QMenu *menu = createStandardContextMenu();
    if (!menu) {
        event->accept();
        return;
    }

    QAction *openDefault = menu->addAction(QStringLiteral("使用默认浏览器打开链接"));
    // 按用户要求走“系统默认浏览器”（不走内嵌窗口、也不强制 Edge）
    connect(openDefault, &QAction::triggered, this, [this, knownUrl]() {
        if (isOpenableLink(knownUrl)) {
            QDesktopServices::openUrl(knownUrl);
            return;
        }
        // 兜底：问页面“右键点到的那个链接是什么”
        QWebEnginePage *p = page();
        if (!p) {
            return;
        }
        p->runJavaScript(QStringLiteral("window.__dshLinkUnderCursor || ''"),
                         [](const QVariant &value) {
                             const QUrl url(value.toString());
                             if (isOpenableLink(url)) {
                                 QDesktopServices::openUrl(url);
                             }
                         });
    });

    // 注意：createStandardContextMenu() 已给菜单设了 WA_DeleteOnClose，关闭后由 Qt 自行销毁，
    // 这里不能再 delete（否则重复释放）。
    menu->exec(event->globalPos());
    event->accept();
}

#endif // DSH_HAVE_WEBENGINE
