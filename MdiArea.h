#ifndef MDIAREA_H
#define MDIAREA_H

#include <QMdiArea>

class QWidget;
class QMdiSubWindow;

// MDI 多文档区域：提供按标题打开/激活子窗口的便捷方法
class CMdiArea : public QMdiArea
{
    Q_OBJECT

public:
    explicit CMdiArea(QWidget *parent = nullptr);
    ~CMdiArea() override;

    // 打开（或激活已打开的）指定标题的子窗口，返回该子窗口
    QMdiSubWindow *openWindow(QWidget *content, const QString &title);
};

#endif // MDIAREA_H
