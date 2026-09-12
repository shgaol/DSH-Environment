#ifndef DSDROPTEXTEDIT_H
#define DSDROPTEXTEDIT_H

// ============================================================================
// CDSDropTextEdit —— 支持把文件拖入的输入框(QPlainTextEdit 子类)
//
// 仅负责接收拖放: 把拖入的本地文件路径通过 fileDropped 信号交给外部
// (DSH 客户端按 DSH 附件协议处理: 仅支持图片, 转 base64 内容块)。
// 不再做“把文件内容内联成文本”的处理。
// ============================================================================

#include <QPlainTextEdit>

class QDragEnterEvent;
class QDropEvent;
class QDragMoveEvent;

class CDSDropTextEdit : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit CDSDropTextEdit(QWidget *parent = nullptr);
    ~CDSDropTextEdit() override;

signals:
    // 拖入一个本地文件(不区分类型), 由外部决定是附件还是提示不支持
    void fileDropped(const QString &localPath);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};

#endif // DSDROPTEXTEDIT_H
