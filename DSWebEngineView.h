#ifndef DSWEBENGINEVIEW_H
#define DSWEBENGINEVIEW_H

// 本文件仅在启用 Qt WebEngine（DSH_HAVE_WEBENGINE）时参与编译；
// 未启用时整份内容为空，保证工程（WebView 后端 / 无后端）仍可正常构建。
#ifdef DSH_HAVE_WEBENGINE

#include <QWebEngineView>

class QContextMenuEvent;

// 网页视图（QWebEngineView 子类）：在标准右键菜单基础上追加一项
// 「使用默认浏览器打开链接」。
// 只用于外部网站窗口（内置站点预设 DeepSeek / 今日头条 / GitHub）：
// 右键 → 直接用系统默认浏览器打开（不走内嵌窗口、也不强制 Edge）。
//
// 这一项**始终**在菜单里，不依赖 WebEngine 是否认出链接：Chromium 只对它自己识别出的
// 链接给 linkUrl()，被浮层盖住的链接、SVG 里的 <a>、JS 跳转的元素都拿不到；
// 拿不到时改用页面里的钩子 window.__dshLinkUnderCursor 兜底
// （由 CDSWebViewWindow 注入，见那里的 kLinkHookScript）。真正没有链接时该项点了不会有反应。
//
// 说明：本类没有自定义信号/槽，因此不声明 Q_OBJECT（避免整份内容被
// #ifdef 遮蔽时 moc 的处理差异；覆盖虚函数不需要元对象）。
class CDSWebEngineView : public QWebEngineView
{
public:
    explicit CDSWebEngineView(QWidget *parent = nullptr);

protected:
    // 弹出标准右键菜单 + 追加“使用默认浏览器打开链接”
    void contextMenuEvent(QContextMenuEvent *event) override;
};

#endif // DSH_HAVE_WEBENGINE

#endif // DSWEBENGINEVIEW_H
