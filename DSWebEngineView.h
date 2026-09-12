#ifndef DSWEBENGINEVIEW_H
#define DSWEBENGINEVIEW_H

// 本文件仅在启用 Qt WebEngine（DSH_HAVE_WEBENGINE）时参与编译；
// 未启用时整份内容为空，保证工程（WebView 后端 / 无后端）仍可正常构建。
#ifdef DSH_HAVE_WEBENGINE

#include <QWebEngineView>

class QContextMenuEvent;

// 网页视图（QWebEngineView 子类）：在标准右键菜单基础上追加一项
// 「使用默认浏览器打开链接」。
// 只用于外部网站窗口（网页小程序窗口，以及内置站点预设 DeepSeek / 今日头条 / GitHub）：
// 右键某个链接 → 直接用系统默认浏览器打开（不走内嵌窗口、也不强制 Edge）。
//
// 说明：本类没有自定义信号/槽，因此不声明 Q_OBJECT（避免整份内容被
// #ifdef 遮蔽时 moc 的处理差异；覆盖虚函数不需要元对象）。
class CDSWebEngineView : public QWebEngineView
{
public:
    explicit CDSWebEngineView(QWidget *parent = nullptr);

protected:
    // 弹出标准右键菜单 + 追加“使用默认浏览器打开链接”（仅当右键在链接上时可用）
    void contextMenuEvent(QContextMenuEvent *event) override;
};

#endif // DSH_HAVE_WEBENGINE

#endif // DSWEBENGINEVIEW_H
