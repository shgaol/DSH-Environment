#ifndef ENVSETTINGDLG_H
#define ENVSETTINGDLG_H

#include <QDialog>

class QLineEdit;

// 环境设置对话框：Git 目录 / Node 目录 / 插件市场镜像
class CEnvSettingDlg : public QDialog
{
    Q_OBJECT

public:
    explicit CEnvSettingDlg(QWidget *parent = nullptr);
    ~CEnvSettingDlg() override;

protected:
    // 点击“确定”时把三个参数保存到 CApplication 中
    void accept() override;

private:
    // 弹出目录选择框，选中后写入指定编辑框
    void chooseDirectory(QLineEdit *edit);

    QLineEdit *m_gitDirEdit = nullptr;    // Git 目录
    QLineEdit *m_nodeDirEdit = nullptr;   // Node 目录
    QLineEdit *m_mirrorEdit = nullptr;    // 插件市场镜像
};

#endif // ENVSETTINGDLG_H
