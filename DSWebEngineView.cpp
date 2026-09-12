#include "DSWebEngineView.h"

#ifdef DSH_HAVE_WEBENGINE

#include <QAction>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QMenu>
#include <QUrl>
#include <QWebEngineContextMenuRequest>

CDSWebEngineView::CDSWebEngineView(QWidget *parent)
    : QWebEngineView(parent)
{
}

void CDSWebEngineView::contextMenuEvent(QContextMenuEvent *event)
{
    // 标准右键菜单（含“在新标签页中打开链接”“复制链接地址”等 Qt 内置项）
    QMenu *menu = createStandardContextMenu();
    if (!menu) {
        event->accept();
        return;
    }

    // 右键落在链接上时，追加“使用默认浏览器打开链接”
    const QUrl linkUrl = lastContextMenuRequest()
                             ? lastContextMenuRequest()->linkUrl()
                             : QUrl();
    if (linkUrl.isValid() && !linkUrl.isEmpty()) {
        QAction *openDefault = menu->addAction(
            QStringLiteral("使用默认浏览器打开链接"));
        connect(openDefault, &QAction::triggered, this, [linkUrl]() {
            // 按用户要求走“系统默认浏览器”（不走内嵌窗口、也不强制 Edge）
            QDesktopServices::openUrl(linkUrl);
        });
    }

    menu->exec(event->globalPos());
    delete menu;
    event->accept();
}

#endif // DSH_HAVE_WEBENGINE
