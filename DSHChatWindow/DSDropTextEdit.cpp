#include "DSDropTextEdit.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>

CDSDropTextEdit::CDSDropTextEdit(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setAcceptDrops(true);
}

CDSDropTextEdit::~CDSDropTextEdit() = default;

void CDSDropTextEdit::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        QPlainTextEdit::dragEnterEvent(event);
}

void CDSDropTextEdit::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        QPlainTextEdit::dragMoveEvent(event);
}

void CDSDropTextEdit::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        QPlainTextEdit::dropEvent(event);
        return;
    }
    for (const QUrl &url : urls) {
        if (url.isLocalFile())
            emit fileDropped(url.toLocalFile()); // 交给外部按 DSH 附件协议处理
    }
    event->acceptProposedAction();
}
