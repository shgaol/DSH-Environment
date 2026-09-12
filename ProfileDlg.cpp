#include "ProfileDlg.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
// 预置 Profile（与 DSH 自带的配置目录 profiles/web、profiles/desktop 对应）：
// 只是方便一键填入，文本框里可以输入任意自定义值（如 somebody、test1）。
const char *const kPresetProfiles[] = {"web", "desktop"};
} // namespace

CProfileDlg::CProfileDlg(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("增加实例"));
    setMinimumWidth(480);

    auto *layout = new QVBoxLayout(this);

    // 1. DSH_HOME 目录：输入框 + 浏览按钮
    m_homeEdit = new QLineEdit(this);
    auto *homeBtn = new QPushButton(QStringLiteral("浏览..."), this);
    auto *homeRow = new QHBoxLayout;
    homeRow->addWidget(m_homeEdit, 1);
    homeRow->addWidget(homeBtn);
    connect(homeBtn, &QPushButton::clicked, this, &CProfileDlg::chooseDshHome);

    // 2. Profile：普通文本框（一定可以输入）+ “web”“desktop” 两个一键填入按钮。
    //    用 QLineEdit 而不是可编辑 QComboBox：QComboBox 的输入要经过其内置行编辑器，
    //    个别平台/样式下会出现“看得见文字但敲不进字”；这里直接用一个文本框，
    //    想输入什么就输入什么，预置值只是省一次键盘输入。
    //    注意：这里**不设任何 validator/inputMask**（不阻止输入任何字符）；
    //    内容在 accept() 里统一校验：不能为空、且不能是 profile（不区分大小写）。
    m_typeEdit = new QLineEdit(QStringLiteral("web"), this); // 默认 web
    m_typeEdit->setPlaceholderText(QStringLiteral("默认 web，也可输入自定义 Profile"));
    m_typeEdit->setClearButtonEnabled(true);
    m_typeEdit->setToolTip(
        QStringLiteral("Profile：默认 web，可直接输入任意自定义值\n"
                       "（如 web / desktop / test1；但不能输入 profile）"));

    auto *profileRow = new QHBoxLayout;
    profileRow->setSpacing(4);
    profileRow->addWidget(m_typeEdit, 1);
    for (const char *preset : kPresetProfiles) {
        const QString name = QString::fromLatin1(preset);
        auto *presetBtn = new QPushButton(name, this); // 点一下就把预置值填进文本框
        presetBtn->setAutoDefault(false);              // 回车只作用于当前焦点控件
        presetBtn->setToolTip(QStringLiteral("填入 %1").arg(name));
        connect(presetBtn, &QPushButton::clicked, this,
                [this, name]() { applyPresetProfile(name); });
        profileRow->addWidget(presetBtn);
    }

    // 3. 端口：数字输入，1 ~ 65535，默认 8001
    //    下限给 1（0 不是可用端口，也能从根上避免"端口为 0"）；上限 65535 = TCP 端口上限。
    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(8001);               // 默认端口 8001
    m_portSpin->setAccelerated(true);
    m_portSpin->setToolTip(QStringLiteral("端口：1 ~ 65535，默认 8001，不能为 0，"
                                          "也不能与实例表中已有行的端口重复"));

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("DSH_HOME 目录:"), homeRow);
    form->addRow(QStringLiteral("Profile:"), profileRow);
    form->addRow(QStringLiteral("端口:"), m_portSpin);
    layout->addLayout(form);

    layout->addWidget(new QLabel(
        QStringLiteral("Profile 可直接输入自定义值，也可点右侧按钮填入 web / desktop；"
                       "注意：Profile 的值不能是 profile。"),
        this));

    // 标准按钮：确定 / 取消
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // 打开对话框即把焦点放在 Profile 上，回车/直接敲字都是改 Profile
    m_typeEdit->setFocus();
}

CProfileDlg::CProfileDlg(const QString &dshHome, const QString &runType,
                         const QString &port, QWidget *parent)
    : CProfileDlg(parent)
{
    // 回显已有配置
    m_homeEdit->setText(dshHome);
    // 已有值直接回显（不限于预置值）；空则用默认 web
    m_typeEdit->setText(runType.trimmed().isEmpty() ? QStringLiteral("web") : runType.trimmed());
    m_portSpin->setValue(port.toInt());
}

CProfileDlg::~CProfileDlg() = default;

QString CProfileDlg::dshHome() const
{
    return m_homeEdit->text().trimmed();
}

QString CProfileDlg::runType() const
{
    return m_typeEdit->text().trimmed();
}

QString CProfileDlg::port() const
{
    return QString::number(m_portSpin->value());
}

void CProfileDlg::setUsedPorts(const QStringList &ports)
{
    m_usedPorts.clear();
    for (const QString &p : ports) {
        const QString trimmed = p.trimmed();
        if (!trimmed.isEmpty()) {
            m_usedPorts.insert(trimmed);
        }
    }
}

bool CProfileDlg::portExists(const QString &port) const
{
    return m_usedPorts.contains(port.trimmed());
}

void CProfileDlg::accept()
{
    // 校验 1：Profile 必填（文本框内容；全空格视为没填）
    if (runType().isEmpty()) {
        QMessageBox::warning(
            this, QStringLiteral("提示"),
            QStringLiteral("Profile 未输入（读到的内容:「%1」）。\n"
                           "请直接在该文本框里输入 Profile（如 web / desktop），"
                           "或点右侧按钮填入预置值。")
                .arg(m_typeEdit->text()));
        m_typeEdit->setFocus();
        m_typeEdit->selectAll();
        return;
    }

    // 校验 2：Profile 的值不能是 "profile"（不区分大小写：profile / Profile / PROFILE
    // 都不允许，因为 "profile" 是 dsh 命令的参数名，不是配置名，写成它服务起不来）。
    // 做法与需求一致：把文本框的值转成大写，再与 "PROFILE" 比较，相同则不能保存。
    if (runType().toUpper() == QStringLiteral("PROFILE")) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("Profile的内容不能输入值不能为profile"));
        m_typeEdit->setFocus();
        m_typeEdit->selectAll();
        return;
    }

    // 校验 3+4：端口不能为 0（SpinBox 下限已是 1，这里再兜一道，程序内改值也不会漏过），
    //          也不能与「实例表」里已有记录的端口重复
    //（调用方通过 setUsedPorts 传入已占用端口；编辑已有行时自身端口已被排除）
    const QString portValue = port();
    if (m_portSpin->value() <= 0 || portValue == QStringLiteral("0")) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("端口不能为 0，请输入 1 ~ 65535 之间的端口！"));
        m_portSpin->setFocus();
        m_portSpin->selectAll();
        return;
    }
    if (portExists(portValue)) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("端口 %1 已存在,不能重复!").arg(portValue));
        m_portSpin->setFocus();
        m_portSpin->selectAll();
        return;
    }

    QDialog::accept();
}

void CProfileDlg::applyPresetProfile(const QString &profile)
{
    m_typeEdit->setText(profile); // 一键填入预置值（填入后仍可继续改成别的）
    m_typeEdit->setFocus();
}

void CProfileDlg::chooseDshHome()
{
    const QString startDir = m_homeEdit->text().isEmpty()
        ? QDir::homePath()
        : m_homeEdit->text();
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择 DSH_HOME 目录"), startDir);
    if (!dir.isEmpty()) {
        m_homeEdit->setText(dir);
    }
}
