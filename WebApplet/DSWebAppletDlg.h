#ifndef DSWEBAPPLETDLG_H
#define DSWEBAPPLETDLG_H

#include <QDialog>
#include <QString>
#include <QStringList>

class QLineEdit;

// 网页小程序录入对话框（“增加”与“修改”共用）：
//   名称：必填，且不能与已有名称重复（忽略大小写与首尾空格；修改时排除自己）
//   网址：必填，没写协议时自动补 https://（如 www.baidu.com → https://www.baidu.com）
class CDSWebAppletDlg : public QDialog
{
    Q_OBJECT

public:
    // namesInUse 当前已占用的名称（增加 = 全部；修改 = 除自己以外的全部）
    // editing    仅影响标题文字（true = “修改网页小程序”）
    explicit CDSWebAppletDlg(const QStringList &namesInUse, bool editing = false,
                             QWidget *parent = nullptr);
    ~CDSWebAppletDlg() override;

    // 录入结果（名称已去首尾空格；网址已规范化）
    QString name() const;
    QString url() const;

    // 修改时回显原值
    void setName(const QString &name);
    void setUrl(const QString &url);

    // 网址规范化：去首尾空格；未写协议时补 https://（供外部复用/校验）
    static QString normalizeUrl(const QString &raw);

protected:
    // 校验“名称/网址非空 + 名称不重复 + 网址格式”后关闭
    void accept() override;

private:
    QLineEdit *m_nameEdit = nullptr; // 名称（必填）
    QLineEdit *m_urlEdit = nullptr;  // 网址（必填）
    QStringList m_namesInUse;        // 已占用的名称
};

#endif // DSWEBAPPLETDLG_H
