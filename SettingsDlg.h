#ifndef SETTINGSDLG_H
#define SETTINGSDLG_H

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>

class CDSHCommander;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

// CDSHSettingsDlg:DSH 设置界面(参考官方 DeepSeek Harness 布局)。
//   左侧导航:通用设置 / 模型 / 插件 / Agent 预设;右侧按选中页显示。
//   通用设置页读取 settings.describe,按命名空间生成可编辑控件,保存走 settings.update。
class CDSHSettingsDlg : public QDialog
{
    Q_OBJECT

public:
    explicit CDSHSettingsDlg(CDSHCommander *cmd, QWidget *parent = nullptr);

private slots:
    void onCommandFinished(const QString &rpcId, bool ok, const QJsonObject &result);
    void onNavChanged(int row);
    void onNamespaceChanged(int row);
    void onSave();

private:
    void buildUi();
    QWidget *placeholderPage(const QString &text); // 占位页(后续接入)
    void showNamespace(const QJsonObject &ns);
    QJsonValue editorValue(const QWidget *w, const QJsonValue &orig) const;

    CDSHCommander *m_cmd = nullptr;
    QString m_describeRpcId;
    QString m_updateRpcId;
    QJsonArray m_namespaces; // settings.describe 返回的 namespaces
    QString m_ns;            // 当前命名空间
    QJsonObject m_value;     // 当前命名空间的 value

    QListWidget *m_navList = nullptr;   // 左侧导航(通用设置/模型/插件/Agent 预设)
    QStackedWidget *m_stack = nullptr;  // 右侧内容

    // 页0(通用设置)
    QListWidget *m_nsList = nullptr;
    QScrollArea *m_scroll = nullptr;
    QWidget *m_formHost = nullptr;
    QVBoxLayout *m_formLayout = nullptr;
    QPushButton *m_saveBtn = nullptr;
    QList<QPair<QString, QWidget *>> m_editors; // key → 编辑控件
};

#endif // SETTINGSDLG_H
