#ifndef TABLEVIEW_H
#define TABLEVIEW_H

#include <QTableView>

// 通用表格视图（继承 QTableView）
class CTableView : public QTableView
{
    Q_OBJECT

public:
    explicit CTableView(QWidget *parent = nullptr);
    ~CTableView() override;
};

#endif // TABLEVIEW_H
