#ifndef PROFILEDLG_H
#define PROFILEDLG_H

#include <QDialog>
#include <QSet>
#include <QString>
#include <QStringList>

class QLineEdit;
class QSpinBox;

// 实例配置对话框：DSH_HOME 目录 / Profile（**可直接输入的文本框**，默认 web；
// 右侧 “web” “desktop” 两个按钮一键填入预置值）/ 端口（默认 8001，1 ~ 65535）。
// 点“确定”时校验：Profile 不能为空、不能是 profile（不区分大小写）；
// 端口不能为 0、不能与「实例表」里已有的端口重复（见 setUsedPorts）。
class CProfileDlg : public QDialog
{
    Q_OBJECT

public:
    explicit CProfileDlg(QWidget *parent = nullptr);
    // 带初始数据构造（用于双击回显/编辑已有配置）
    CProfileDlg(const QString &dshHome, const QString &runType,
                const QString &port, QWidget *parent = nullptr);
    ~CProfileDlg() override;

    // 录入结果
    QString dshHome() const;
    QString runType() const;
    QString port() const;

    // 设置「实例表里已占用的端口」：点确定时端口与其中任何一个相同都拒绝保存。
    // 编辑已有行时调用方应把该行自身的端口排除掉（否则自己会和自己重复）。
    void setUsedPorts(const QStringList &ports);

protected:
    // 校验 Profile 与端口后关闭（Profile 不能为空/不能是 profile；端口不能为 0/不能重复）
    void accept() override;

private:
    // 弹出目录选择框，写入 DSH_HOME
    void chooseDshHome();
    // 把预置 Profile（web / desktop）一键填进文本框
    void applyPresetProfile(const QString &profile);
    // 端口是否已被实例表里的其它记录占用
    bool portExists(const QString &port) const;

    QLineEdit *m_homeEdit = nullptr;  // DSH_HOME 目录
    QLineEdit *m_typeEdit = nullptr;  // Profile（直接输入，默认 web）
    QSpinBox *m_portSpin = nullptr;   // 端口（1 ~ 65535，默认 8001）
    QSet<QString> m_usedPorts;        // 实例表里已占用的端口（用于查重）
};

#endif // PROFILEDLG_H
