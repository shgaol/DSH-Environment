#include "EnvSettingDlg.h"
#include "Application.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

CEnvSettingDlg::CEnvSettingDlg(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("环境设置"));
    setMinimumSize(560, 180);

    auto *layout = new QVBoxLayout(this);

    // Git 目录：输入框 + 浏览按钮
    m_gitDirEdit = new QLineEdit(this);
    auto *gitBtn = new QPushButton(QStringLiteral("浏览..."), this);
    auto *gitRow = new QHBoxLayout;
    gitRow->addWidget(m_gitDirEdit, 1);
    gitRow->addWidget(gitBtn);
    connect(gitBtn, &QPushButton::clicked, this,
            [this]() { chooseDirectory(m_gitDirEdit); });

    // Node 目录：输入框 + 浏览按钮
    m_nodeDirEdit = new QLineEdit(this);
    auto *nodeBtn = new QPushButton(QStringLiteral("浏览..."), this);
    auto *nodeRow = new QHBoxLayout;
    nodeRow->addWidget(m_nodeDirEdit, 1);
    nodeRow->addWidget(nodeBtn);
    connect(nodeBtn, &QPushButton::clicked, this,
            [this]() { chooseDirectory(m_nodeDirEdit); });

    // 插件市场镜像：输入框
    m_mirrorEdit = new QLineEdit(this);
    m_mirrorEdit->setPlaceholderText(QStringLiteral("如 https://registry.npmmirror.com"));

    // 表单：Git 目录 / Node 目录 / 插件市场镜像
    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Git 目录:"), gitRow);
    form->addRow(QStringLiteral("Node 目录:"), nodeRow);
    form->addRow(QStringLiteral("插件市场镜像:"), m_mirrorEdit);
    layout->addLayout(form);

    layout->addStretch(1);

    // 标准按钮：确定 / 取消
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // 打开时：如果 CApplication 中已设置，读取出来显示
    m_gitDirEdit->setText(CApplication::instance()->gitDir());
    m_nodeDirEdit->setText(CApplication::instance()->nodeDir());
    m_mirrorEdit->setText(CApplication::instance()->pluginMirror());

    // 未设置（为空）时，默认使用桌面目录 / 默认镜像地址
    if (m_gitDirEdit->text().isEmpty()) {
        m_gitDirEdit->setText(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    }
    if (m_nodeDirEdit->text().isEmpty()) {
        m_nodeDirEdit->setText(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    }
    if (m_mirrorEdit->text().isEmpty()) {
        m_mirrorEdit->setText(QStringLiteral("https://registry.npmmirror.com"));
    }
}

CEnvSettingDlg::~CEnvSettingDlg() = default;

void CEnvSettingDlg::chooseDirectory(QLineEdit *edit)
{
    // 目录选择框的起始目录：当前输入值，为空时用桌面
    const QString startDir = edit->text().isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
        : edit->text();
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择目录"), startDir);
    if (!dir.isEmpty()) {
        edit->setText(dir);
    }
}

void CEnvSettingDlg::accept()
{
    // 点击“确定”：把三个参数保存到 CApplication，并写入 JSON 文件持久化
    CApplication::instance()->setGitDir(m_gitDirEdit->text().trimmed());
    CApplication::instance()->setNodeDir(m_nodeDirEdit->text().trimmed());
    CApplication::instance()->setPluginMirror(m_mirrorEdit->text().trimmed());
    CApplication::instance()->saveEnvSettings();
    QDialog::accept();
}
