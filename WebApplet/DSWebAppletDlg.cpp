#include "DSWebAppletDlg.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QRegularExpression>
#include <QUrl>
#include <QVBoxLayout>

CDSWebAppletDlg::CDSWebAppletDlg(const QStringList &namesInUse, bool editing, QWidget *parent)
    : QDialog(parent)
    , m_namesInUse(namesInUse)
{
    setWindowTitle(editing ? QStringLiteral("修改网页小程序") : QStringLiteral("增加网页小程序"));
    setMinimumWidth(480);

    auto *layout = new QVBoxLayout(this);

    // 名称与网址都是必填项：标签用蓝色加粗提示（与源码录入对话框同一风格）
    const QString requiredStyle = QStringLiteral("color: #2563EB; font-weight: bold;");

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(QStringLiteral("必填，不能与已有名称重复"));
    m_nameEdit->setStyleSheet(QStringLiteral("color: #2563EB;"));
    auto *nameLabel = new QLabel(QStringLiteral("名称:"), this);
    nameLabel->setStyleSheet(requiredStyle);

    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setPlaceholderText(QStringLiteral("必填，如 www.baidu.com 或 https://www.baidu.com"));
    m_urlEdit->setStyleSheet(QStringLiteral("color: #2563EB;"));
    auto *urlLabel = new QLabel(QStringLiteral("网址:"), this);
    urlLabel->setStyleSheet(requiredStyle);

    auto *form = new QFormLayout;
    form->addRow(nameLabel, m_nameEdit);
    form->addRow(urlLabel, m_urlEdit);
    layout->addLayout(form);

    auto *tip = new QLabel(QStringLiteral("保存后会读取该网页的图标，作为快捷方式显示在“网页小程序”中。"),
                           this);
    tip->setWordWrap(true);
    layout->addWidget(tip);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

CDSWebAppletDlg::~CDSWebAppletDlg() = default;

QString CDSWebAppletDlg::name() const
{
    return m_nameEdit->text().trimmed();
}

QString CDSWebAppletDlg::url() const
{
    return normalizeUrl(m_urlEdit->text());
}

void CDSWebAppletDlg::setName(const QString &name)
{
    m_nameEdit->setText(name);
}

void CDSWebAppletDlg::setUrl(const QString &url)
{
    m_urlEdit->setText(url);
}

QString CDSWebAppletDlg::normalizeUrl(const QString &raw)
{
    QString url = raw.trimmed();
    if (url.isEmpty()) {
        return QString();
    }
    // 没写协议（http:// / https:// 等）时自动补 https://，
    // 让“www.baidu.com”“127.0.0.1:8888”这类输入也能直接用。
    static const QRegularExpression schemeRe(QStringLiteral("^[A-Za-z][A-Za-z0-9+.\\-]*:"));
    if (!schemeRe.match(url).hasMatch()) {
        url.prepend(QStringLiteral("https://"));
    }
    return url;
}

void CDSWebAppletDlg::accept()
{
    // 1. 名称必填
    const QString appletName = name();
    if (appletName.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("名称不能为空！"));
        m_nameEdit->setFocus();
        return;
    }

    // 2. 网址必填
    const QString appletUrl = url();
    if (appletUrl.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("网址不能为空！"));
        m_urlEdit->setFocus();
        return;
    }
    // 规范化结果回显（用户能看到实际保存的网址）
    if (m_urlEdit->text() != appletUrl) {
        m_urlEdit->setText(appletUrl);
    }

    // 3. 名称不能重复（忽略大小写；修改时 m_namesInUse 已排除自己）
    for (const QString &used : m_namesInUse) {
        if (used.trimmed().compare(appletName, Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(this, QStringLiteral("提示"),
                                 QStringLiteral("名称【%1】已存在，请换一个名称！").arg(appletName));
            m_nameEdit->setFocus();
            m_nameEdit->selectAll();
            return;
        }
    }

    // 4. 网址格式：必须是带主机名的合法网址（如 https://www.baidu.com）
    const QUrl parsed(appletUrl);
    if (!parsed.isValid() || parsed.host().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("网址格式不正确：\n%1").arg(appletUrl));
        m_urlEdit->setFocus();
        m_urlEdit->selectAll();
        return;
    }

    QDialog::accept();
}
