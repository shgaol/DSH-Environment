#ifndef DHSSRCDLG_H
#define DHSSRCDLG_H

#include <QDialog>

class QLineEdit;
class QTextEdit;

// DSH 源码信息录入对话框：名称（蓝色）/ 源码目录 / 版本（三者必填）/ 说明（只读）
// 选目录时若名称、版本为空，用目录名（非路径）自动填充两者
class CDHSSrcDlg : public QDialog
{
    Q_OBJECT

public:
    explicit CDHSSrcDlg(QWidget *parent = nullptr);
    ~CDHSSrcDlg() override;

    // 录入结果
    QString name() const;
    QString srcDir() const;
    QString version() const;
    QString desc() const;

protected:
    // 校验名称必填后关闭
    void accept() override;

private:
    // 弹出目录选择框，写入源码目录
    void chooseSrcDir();

    QLineEdit *m_nameEdit = nullptr;    // 名称（必填，显示为蓝色）
    QLineEdit *m_srcDirEdit = nullptr;  // 源码目录
    QLineEdit *m_versionEdit = nullptr; // 版本
    QTextEdit *m_descEdit = nullptr;    // 说明（只读，不能输入）
};

#endif // DHSSRCDLG_H
