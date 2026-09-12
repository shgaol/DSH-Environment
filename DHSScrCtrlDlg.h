#ifndef DHSSCRCTRLDLG_H
#define DHSSCRCTRLDLG_H

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QPushButton;
class QStandardItemModel;
class QTextEdit;
class QWidget;

class CTableView;
class CDSCmdView;

// DSH 源码记录查看对话框：字段与 CDHSSrcDlg 一致，但全部只读（只能看不能操作）
// 下方提供：是否执行 install / build 的复选框 + 对应执行按钮
// 右侧：CDSCmdView 命令执行终端（可继续输入命令）
class CDHSScrCtrlDlg : public QDialog
{
    Q_OBJECT

public:
    explicit CDHSScrCtrlDlg(const QString &id, const QString &name, const QString &srcDir,
                            const QString &version, const QString &desc,
                            QWidget *parent = nullptr);
    ~CDHSScrCtrlDlg() override;

    // 当前记录的唯一 ID（UUID）
    QString id() const;

    // 是否已执行 install / build（用于保存与回显）
    bool installChecked() const;
    bool buildChecked() const;
    // 打开时回显记录中已保存的执行状态
    void setInstallChecked(bool checked);
    void setBuildChecked(bool checked);

protected:
    // 点“确定”时：把执行状态同步到配置表，并保存 profiles.json
    void accept() override;

private slots:
    // 执行 pnpm install
    void execInstall();
    // 执行 pnpm run build
    void execBuild();
    // “增加”按钮：弹出 CProfileDlg 并追加到配置表
    void onAddProfile();
    // “删除”按钮：删除配置表当前选中行
    void onDelProfile();

private:
    // 生成命令预览文本（每行一条，供只读 QTextEdit 显示）
    QString cmdDisplayText(const QString &command, bool withGitInit) const;
    // 公共执行逻辑：把命令写入 CDSCmdView 终端执行
    void execCmd(const QString &command, bool withGitInit);
    // 把若干命令行逐条送入终端（每条带回车）
    void runInTerminal(const QStringList &lines);
    // 判断端口是否已被其它记录占用(excludePort: 编辑时不把记录自身算作重复)
    bool profilePortExists(const QString &port, const QString &excludePort = QString()) const;
    // 实例表里已占用的全部端口(第 7 列 + profiles.json; excludePort 非空时排除它):
    // 传给 CProfileDlg::setUsedPorts, 让「增加实例」对话框在点确定时就挡住重复端口
    QStringList usedPorts(const QString &excludePort = QString()) const;
    // 保存配置表到 配置目录/profiles.json（按 mainid 归并，保留其他记录）
    void saveProfiles() const;
    // 从 profiles.json 读取当前记录（mainid）的配置行
    void loadProfiles();

    QCheckBox *m_installCheck = nullptr; // 是否执行 pnpm install
    QPushButton *m_installBtn = nullptr; // pnpm install 执行按钮
    QCheckBox *m_buildCheck = nullptr;   // 是否执行 pnpm run build
    QPushButton *m_buildBtn = nullptr;   // pnpm run build 执行按钮
    QTextEdit *m_installCmdEdit = nullptr; // 安装命令预览（只读，可复制）
    QTextEdit *m_buildCmdEdit = nullptr;   // 构建命令预览（只读，可复制）
    QPushButton *m_addBtn = nullptr;       // 增加实例按钮
    QPushButton *m_delBtn = nullptr;       // 删除实例按钮
    CTableView *m_profileTable = nullptr;  // 实例配置表
    QStandardItemModel *m_profileModel = nullptr; // 配置表数据模型
    QString m_id;                          // 当前记录的唯一 ID（UUID）
    QString m_name;                        // 当前记录的源码名称（同步到配置表）
    QString m_srcDir;                      // 当前记录对应的源码目录（执行命令用）
    QString m_version;                     // 当前记录的版本（同步到配置表）
    CDSCmdView *m_terminal = nullptr;      // 右侧命令执行终端（CDSCmdView，ConPTY）
};

#endif // DHSSCRCTRLDLG_H
