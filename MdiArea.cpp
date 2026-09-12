#include "MdiArea.h"

#include <QMdiSubWindow>

CMdiArea::CMdiArea(QWidget *parent)
    : QMdiArea(parent)
{
}

CMdiArea::~CMdiArea() = default;

QMdiSubWindow *CMdiArea::openWindow(QWidget *content, const QString &title)
{
    // 已打开同标题的子窗口：直接激活
    const QList<QMdiSubWindow *> subs = subWindowList();
    for (QMdiSubWindow *sub : subs) {
        if (sub->windowTitle() == title) {
            sub->show();
            setActiveSubWindow(sub);
            return sub;
        }
    }

    // 未打开：新建子窗口（关闭后连同内容窗口一起销毁）
    auto *sub = addSubWindow(content);
    sub->setAttribute(Qt::WA_DeleteOnClose);
    sub->setWindowTitle(title);
    sub->show();
    return sub;
}
