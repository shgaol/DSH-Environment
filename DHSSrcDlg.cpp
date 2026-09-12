#include "DHSSrcDlg.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

CDHSSrcDlg::CDHSSrcDlg(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("增加 DSH 源码"));
    setMinimumWidth(560);

    auto *layout = new QVBoxLayout(this);

    // 1. 名称（必填，显示为蓝色）
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(QStringLiteral("必填"));
    m_nameEdit->setStyleSheet(QStringLiteral("color: #2563EB;"));
    auto *nameLabel = new QLabel(QStringLiteral("名称:"), this);
    nameLabel->setStyleSheet(QStringLiteral("color: #2563EB; font-weight: bold;"));
    auto *nameRow = new QHBoxLayout;
    nameRow->addWidget(m_nameEdit, 1);

    // 2. 源码目录：输入框 + 浏览按钮
    m_srcDirEdit = new QLineEdit(this);
    auto *srcBtn = new QPushButton(QStringLiteral("浏览..."), this);
    auto *srcRow = new QHBoxLayout;
    srcRow->addWidget(m_srcDirEdit, 1);
    srcRow->addWidget(srcBtn);
    connect(srcBtn, &QPushButton::clicked, this, &CDHSSrcDlg::chooseSrcDir);

    // 3. 版本
    m_versionEdit = new QLineEdit(this);
    m_versionEdit->setPlaceholderText(QStringLiteral("如 1.0.0"));

    // 表单：名称 / 源码目录 / 版本
    auto *form = new QFormLayout;
    form->addRow(nameLabel, nameRow);
    form->addRow(QStringLiteral("源码目录:"), srcRow);
    form->addRow(QStringLiteral("版本:"), m_versionEdit);
    layout->addLayout(form);

    // 4. 说明：只读，不能输入
    auto *descLabel = new QLabel(QStringLiteral("说明(只读):"), this);
    layout->addWidget(descLabel);
    m_descEdit = new QTextEdit(this);
    m_descEdit->setReadOnly(true);
    m_descEdit->setPlaceholderText(QStringLiteral("只读说明区，不可输入"));
    m_descEdit->setFixedHeight(80);
    layout->addWidget(m_descEdit);

    // 标准按钮：确定 / 取消
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

CDHSSrcDlg::~CDHSSrcDlg() = default;

QString CDHSSrcDlg::name() const
{
    return m_nameEdit->text().trimmed();
}

QString CDHSSrcDlg::srcDir() const
{
    return m_srcDirEdit->text().trimmed();
}

QString CDHSSrcDlg::version() const
{
    return m_versionEdit->text().trimmed();
}

QString CDHSSrcDlg::desc() const
{
    return m_descEdit->toPlainText();
}

void CDHSSrcDlg::chooseSrcDir()
{
    const QString startDir = m_srcDirEdit->text().isEmpty()
        ? QDir::homePath()
        : m_srcDirEdit->text();
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择源码目录"), startDir);
    if (dir.isEmpty()) {
        return;
    }
    m_srcDirEdit->setText(dir);

    // 名称和版本都为空时：用所选目录的名称（非路径）自动填充名称和版本
    if (m_nameEdit->text().trimmed().isEmpty()
        && m_versionEdit->text().trimmed().isEmpty()) {
        const QString dirName = QFileInfo(dir).fileName();
        m_nameEdit->setText(dirName);
        m_versionEdit->setText(dirName);
    }
}

void CDHSSrcDlg::accept()
{
    // 必填校验：名称 / 源码目录 / 版本
    QStringList missing;
    if (m_nameEdit->text().trimmed().isEmpty()) {
        missing << QStringLiteral("名称");
    }
    if (m_srcDirEdit->text().trimmed().isEmpty()) {
        missing << QStringLiteral("源码目录");
    }
    if (m_versionEdit->text().trimmed().isEmpty()) {
        missing << QStringLiteral("版本");
    }
    if (!missing.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("以下字段未输入：\n%1")
                                 .arg(missing.join(QStringLiteral("、"))));
        return;
    }
    QDialog::accept();
}
