#include "DHSScrCtrlDlg.h"
#include "Application.h"
#include "DSCmdView.h"
#include "ProfileDlg.h"
#include "TableView.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTextEdit>
#include <QVBoxLayout>

CDHSScrCtrlDlg::CDHSScrCtrlDlg(const QString &id, const QString &name, const QString &srcDir,
                               const QString &version, const QString &desc,
                               QWidget *parent)
    : QDialog(parent)
    , m_id(id)
    , m_name(name)
    , m_srcDir(srcDir)
    , m_version(version)
{
    setWindowTitle(QStringLiteral("DSH 源码记录"));
    resize(1150, 800);

    // 主布局：左侧表单 + 右侧命令输出
    auto *mainLayout = new QHBoxLayout(this);

    // ---- 左侧：表单与操作区 ----
    auto *leftWidget = new QWidget(this);
    auto *layout = new QVBoxLayout(leftWidget);

    // 名称（只读，蓝色显示，与录入对话框一致）
    auto *nameEdit = new QLineEdit(name, leftWidget);
    nameEdit->setReadOnly(true);
    nameEdit->setStyleSheet(QStringLiteral("color: #2563EB;"));
    auto *nameLabel = new QLabel(QStringLiteral("名称:"), leftWidget);
    nameLabel->setStyleSheet(QStringLiteral("color: #2563EB; font-weight: bold;"));

    // 源码目录（只读）
    auto *srcDirEdit = new QLineEdit(srcDir, leftWidget);
    srcDirEdit->setReadOnly(true);

    // 版本（只读）
    auto *versionEdit = new QLineEdit(version, leftWidget);
    versionEdit->setReadOnly(true);

    auto *form = new QFormLayout;
    form->addRow(nameLabel, nameEdit);
    form->addRow(QStringLiteral("源码目录:"), srcDirEdit);
    form->addRow(QStringLiteral("版本:"), versionEdit);
    layout->addLayout(form);

    // 说明（只读）
    auto *descLabel = new QLabel(QStringLiteral("说明(只读):"), leftWidget);
    layout->addWidget(descLabel);
    auto *descEdit = new QTextEdit(desc, leftWidget);
    descEdit->setReadOnly(true);
    descEdit->setFixedHeight(80);
    layout->addWidget(descEdit);

    // 1. 是否执行 pnpm install + 执行按钮
    m_installCheck = new QCheckBox(QStringLiteral("是否执行 pnpm install"), leftWidget);
    m_installBtn = new QPushButton(QStringLiteral("pnpm install执行"), leftWidget);
    auto *installRow = new QHBoxLayout;
    installRow->addWidget(m_installCheck, 1);
    installRow->addWidget(m_installBtn);
    layout->addLayout(installRow);
    connect(m_installBtn, &QPushButton::clicked, this, &CDHSScrCtrlDlg::execInstall);
    // 安装命令预览（只读，可复制，不能修改）
    m_installCmdEdit = new QTextEdit(leftWidget);
    m_installCmdEdit->setReadOnly(true);
    m_installCmdEdit->setFixedHeight(90);
    m_installCmdEdit->setPlainText(
        cmdDisplayText(QStringLiteral("npm install -g pnpm\npnpm install"), false));
    layout->addWidget(m_installCmdEdit);

    // 2. 是否执行 pnpm run build + 执行按钮
    m_buildCheck = new QCheckBox(QStringLiteral("是否执行 pnpm run build"), leftWidget);
    m_buildBtn = new QPushButton(QStringLiteral("pnpm run build执行"), leftWidget);
    auto *buildRow = new QHBoxLayout;
    buildRow->addWidget(m_buildCheck, 1);
    buildRow->addWidget(m_buildBtn);
    layout->addLayout(buildRow);
    connect(m_buildBtn, &QPushButton::clicked, this, &CDHSScrCtrlDlg::execBuild);
    // 构建命令预览（只读，可复制，不能修改）
    m_buildCmdEdit = new QTextEdit(leftWidget);
    m_buildCmdEdit->setReadOnly(true);
    m_buildCmdEdit->setFixedHeight(150); // 含 3 条 git 命令，稍高
    m_buildCmdEdit->setPlainText(
        cmdDisplayText(QStringLiteral("pnpm run build"), true));
    layout->addWidget(m_buildCmdEdit);

    // 3. 实例配置：增加 / 删除 按钮
    m_addBtn = new QPushButton(QStringLiteral("增加"), leftWidget);
    m_delBtn = new QPushButton(QStringLiteral("删除"), leftWidget);
    auto *profileBtnRow = new QHBoxLayout;
    profileBtnRow->addWidget(m_addBtn);
    profileBtnRow->addWidget(m_delBtn);
    profileBtnRow->addStretch(1);
    layout->addLayout(profileBtnRow);
    connect(m_addBtn, &QPushButton::clicked, this, &CDHSScrCtrlDlg::onAddProfile);
    connect(m_delBtn, &QPushButton::clicked, this, &CDHSScrCtrlDlg::onDelProfile);

    // 实例配置表：mainid / 源码名称 / 版本 / 是否install / 是否build / DSH_HOME / Profile / 端口
    m_profileModel = new QStandardItemModel(0, 8, this);
    m_profileModel->setHorizontalHeaderLabels({
        QStringLiteral("mainid"), QStringLiteral("源码名称"), QStringLiteral("版本"),
        QStringLiteral("是否install"), QStringLiteral("是否build"),
        QStringLiteral("DSH_HOME"), QStringLiteral("Profile"), QStringLiteral("端口")});
    m_profileTable = new CTableView(leftWidget);
    m_profileTable->setModel(m_profileModel);
    m_profileTable->setColumnWidth(0, 200);
    m_profileTable->setColumnWidth(5, 200);
    layout->addWidget(m_profileTable, 1); // 占据剩余空间

    // 双击配置表某行：弹出 CProfileDlg 回显已输入的数据（可修改，确定后更新该行）
    connect(m_profileTable, &QTableView::doubleClicked, this,
            [this](const QModelIndex &index) {
                const int row = index.row();
                const QStandardItem *portItem = m_profileModel->item(row, 7);
                const QString oldPort = portItem ? portItem->text() : QString();
                CProfileDlg dlg(
                    m_profileModel->item(row, 5)->text(), // DSH_HOME
                    m_profileModel->item(row, 6)->text(), // 运行类型
                    oldPort,                              // 端口
                    this);
                // 端口查重交给对话框自己做（点确定时就报错，不必等回到这里）：
                // 传入实例表里已占用的端口，但排除本行自身的端口，否则自己会和自己重复。
                dlg.setUsedPorts(usedPorts(oldPort));
                if (dlg.exec() != QDialog::Accepted) {
                    return; // 取消：不改动
                }
                // 兜底再查一次（模型在对话框打开期间理论上不会变）
                if (profilePortExists(dlg.port(), oldPort)) {
                    QMessageBox::warning(this, QStringLiteral("提示"),
                                         QStringLiteral("端口已存在,不能重复!"));
                    return;
                }
                m_profileModel->item(row, 5)->setText(dlg.dshHome());
                m_profileModel->item(row, 6)->setText(dlg.runType());
                m_profileModel->item(row, 7)->setText(dlg.port());
            });

    // 关闭按钮：确定（保存并同步）/ 关闭
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Close, leftWidget);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    mainLayout->addWidget(leftWidget, 1);

    // ---- 右侧：CDSCmdView 终端（命令执行区，可继续输入命令）----
    auto *rightWidget = new QWidget(this);
    auto *rv = new QVBoxLayout(rightWidget);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(4);
    m_terminal = new CDSCmdView(rightWidget);
    rv->addWidget(m_terminal, 1);
    // 启动 cmd.exe 终端，工作目录设为当前源码目录（后续命令在此执行）
    m_terminal->start(QStringLiteral("cmd.exe"), {}, m_srcDir);
    mainLayout->addWidget(rightWidget, 1);

    // 打开时读取当前记录已保存的配置
    loadProfiles();
}

CDHSScrCtrlDlg::~CDHSScrCtrlDlg()
{
    // 结束终端会话（关闭 ConPTY 读线程，避免析构时仍在运行）
    if (m_terminal) {
        m_terminal->closeSession();
    }
}

QString CDHSScrCtrlDlg::id() const
{
    return m_id;
}

bool CDHSScrCtrlDlg::installChecked() const
{
    return m_installCheck->isChecked();
}

bool CDHSScrCtrlDlg::buildChecked() const
{
    return m_buildCheck->isChecked();
}

void CDHSScrCtrlDlg::setInstallChecked(bool checked)
{
    m_installCheck->setChecked(checked);
}

void CDHSScrCtrlDlg::setBuildChecked(bool checked)
{
    m_buildCheck->setChecked(checked);
}

void CDHSScrCtrlDlg::execInstall()
{
    execCmd(QStringLiteral("npm install -g pnpm\r\npnpm install"), false);
}

void CDHSScrCtrlDlg::execBuild()
{
    execCmd(QStringLiteral("pnpm run build"), true);
}

void CDHSScrCtrlDlg::execCmd(const QString &command, bool withGitInit)
{
    if (!m_terminal) {
        return;
    }

    // 从 CApplication 读取环境设置中的 Git / Node 目录
    const QString gitDir = CApplication::instance()->gitDir();
    const QString nodeDir = CApplication::instance()->nodeDir();
    const QString srcDir = m_srcDir;

    // 逐行命令（终端已在 srcDir 启动，无需 cd；cmd 会逐条回显执行）
    QStringList lines;

    // 若需要 git 初始化
    if (withGitInit) {
        lines << QStringLiteral("git init");
        lines << QStringLiteral("git add .");
        lines << QStringLiteral("git commit -m \"initial commit\"");
    }

    // path Git目录;Node目录;源码目录\node_modules\.bin;源码目录;%PATH%
    QStringList pathParts;
    if (!gitDir.isEmpty()) {
        pathParts << QStringLiteral("\"%1\"").arg(gitDir);
    }
    if (!nodeDir.isEmpty()) {
        pathParts << QStringLiteral("\"%1\"").arg(nodeDir);
    }
    pathParts << QStringLiteral("\"%1\\node_modules\\.bin\"").arg(srcDir);
    pathParts << QStringLiteral("\"%1\"").arg(srcDir);
    pathParts << QStringLiteral("%PATH%");
    lines << QStringLiteral("path %1").arg(pathParts.join(QLatin1Char(';')));
    lines << command;

    runInTerminal(lines);
}

void CDHSScrCtrlDlg::runInTerminal(const QStringList &lines)
{
    if (!m_terminal) {
        return;
    }
    // 每条命令以回车送入终端执行（ConPTY 会回显并执行）
    for (const QString &line : lines) {
        m_terminal->sendText(line + QStringLiteral("\r\n"));
    }
}

QStringList CDHSScrCtrlDlg::usedPorts(const QString &excludePort) const
{
    QStringList ports;

    // 1) 当前对话框模型中的记录(可能尚未保存到 profiles.json)
    for (int r = 0; r < int(m_profileModel->rowCount()); ++r) {
        const QStandardItem *it = m_profileModel->item(r, 7);
        if (!it) {
            continue;
        }
        const QString p = it->text().trimmed();
        if (!p.isEmpty() && p != excludePort && !ports.contains(p)) {
            ports << p;
        }
    }

    // 2) profiles.json 中所有记录(mainid 无关)
    const QString path = CApplication::instance()->configDir()
                         + QStringLiteral("/profiles.json");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return ports; // 无配置文件 → 只有模型里的端口
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return ports;
    }
    const QJsonArray arr = doc.object().value(QStringLiteral("profiles")).toArray();
    for (const QJsonValue &v : arr) {
        const QString p = v.toObject().value(QStringLiteral("port")).toString().trimmed();
        if (!p.isEmpty() && p != excludePort && !ports.contains(p)) {
            ports << p;
        }
    }
    return ports;
}

bool CDHSScrCtrlDlg::profilePortExists(const QString &port, const QString &excludePort) const
{
    const QString p = port.trimmed();
    // 空端口、以及编辑时自身那一行, 都不算重复
    return !p.isEmpty() && usedPorts(excludePort).contains(p);
}

QString CDHSScrCtrlDlg::cmdDisplayText(const QString &command, bool withGitInit) const
{
    const QString srcDir = m_srcDir;
    const QString gitDir = CApplication::instance()->gitDir();
    const QString nodeDir = CApplication::instance()->nodeDir();

    // 每行一条命令，与执行序列一致
    QStringList lines;
    lines << QStringLiteral("cd /d \"%1\"").arg(srcDir);
    if (withGitInit) {
        lines << QStringLiteral("git init");
        lines << QStringLiteral("git add .");
        lines << QStringLiteral("git commit -m \"initial commit\"");
    }
    // path Git目录;Node目录;源码目录\node_modules\.bin;源码目录;%PATH%
    QStringList pathParts;
    if (!gitDir.isEmpty()) {
        pathParts << QStringLiteral("\"%1\"").arg(gitDir);
    }
    if (!nodeDir.isEmpty()) {
        pathParts << QStringLiteral("\"%1\"").arg(nodeDir);
    }
    pathParts << QStringLiteral("\"%1\\node_modules\\.bin\"").arg(srcDir);
    pathParts << QStringLiteral("\"%1\"").arg(srcDir);
    pathParts << QStringLiteral("%PATH%");
    lines << QStringLiteral("path %1").arg(pathParts.join(QLatin1Char(';')));
    lines << command;

    return lines.join(QLatin1Char('\n'));
}

void CDHSScrCtrlDlg::onAddProfile()
{
    CProfileDlg dlg(this);
    // 把实例表里已占用的端口交给对话框：点“确定”时端口与已有记录重复就直接报错
    dlg.setUsedPorts(usedPorts());
    if (dlg.exec() != QDialog::Accepted) {
        return; // 取消
    }

    // 端口不能与已有记录重复（对话框内已查过一次，这里兜底）
    if (profilePortExists(dlg.port())) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("端口已存在,不能重复!"));
        return;
    }

    // 追加一行：mainid/源码名称/版本/执行状态自动填充，其余来自 CProfileDlg
    const int row = m_profileModel->rowCount();
    m_profileModel->insertRow(row);
    m_profileModel->setItem(row, 0, new QStandardItem(m_id));
    m_profileModel->setItem(row, 1, new QStandardItem(m_name));
    m_profileModel->setItem(row, 2, new QStandardItem(m_version));
    m_profileModel->setItem(row, 3,
        new QStandardItem(m_installCheck->isChecked() ? QStringLiteral("是")
                                                      : QStringLiteral("否")));
    m_profileModel->setItem(row, 4,
        new QStandardItem(m_buildCheck->isChecked() ? QStringLiteral("是")
                                                    : QStringLiteral("否")));
    m_profileModel->setItem(row, 5, new QStandardItem(dlg.dshHome()));
    m_profileModel->setItem(row, 6, new QStandardItem(dlg.runType()));
    m_profileModel->setItem(row, 7, new QStandardItem(dlg.port()));
}

void CDHSScrCtrlDlg::onDelProfile()
{
    const QModelIndex idx = m_profileTable->currentIndex();
    if (!idx.isValid()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先选中要删除的行！"));
        return;
    }
    m_profileModel->removeRow(idx.row());
}

void CDHSScrCtrlDlg::accept()
{
    // 点“确定”：把两个执行状态同步到配置表所有行
    const QString install = m_installCheck->isChecked() ? QStringLiteral("是")
                                                        : QStringLiteral("否");
    const QString build = m_buildCheck->isChecked() ? QStringLiteral("是")
                                                    : QStringLiteral("否");
    for (int r = 0; r < m_profileModel->rowCount(); ++r) {
        m_profileModel->item(r, 3)->setText(install);
        m_profileModel->item(r, 4)->setText(build);
    }

    // 保存到 JSON
    saveProfiles();
    QDialog::accept();
}

void CDHSScrCtrlDlg::saveProfiles() const
{
    const QString path = CApplication::instance()->configDir()
                         + QStringLiteral("/profiles.json");

    // 读取已有数据（保留其他记录的表）
    QJsonArray arr;
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (doc.isObject()) {
            arr = doc.object().value(QStringLiteral("profiles")).toArray();
        }
        file.close();
    }

    // 移除当前 mainid 的旧条目，避免重复
    QJsonArray kept;
    for (const QJsonValue &value : arr) {
        if (value.toObject().value(QStringLiteral("mainid")).toString() != m_id) {
            kept.append(value);
        }
    }

    // 追加当前配置表的行
    for (int r = 0; r < m_profileModel->rowCount(); ++r) {
        QJsonObject obj;
        obj.insert(QStringLiteral("mainid"), m_profileModel->item(r, 0)->text());
        obj.insert(QStringLiteral("srcName"), m_profileModel->item(r, 1)->text());
        obj.insert(QStringLiteral("version"), m_profileModel->item(r, 2)->text());
        obj.insert(QStringLiteral("install"),
                   m_profileModel->item(r, 3)->text() == QStringLiteral("是"));
        obj.insert(QStringLiteral("build"),
                   m_profileModel->item(r, 4)->text() == QStringLiteral("是"));
        obj.insert(QStringLiteral("dshHome"), m_profileModel->item(r, 5)->text());
        obj.insert(QStringLiteral("runType"), m_profileModel->item(r, 6)->text());
        obj.insert(QStringLiteral("port"), m_profileModel->item(r, 7)->text());
        kept.append(obj);
    }

    // 写回
    QJsonObject root;
    root.insert(QStringLiteral("profiles"), kept);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile out(path);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.close();
    }
}

void CDHSScrCtrlDlg::loadProfiles()
{
    const QString path = CApplication::instance()->configDir()
                         + QStringLiteral("/profiles.json");
    if (!QFile::exists(path)) {
        return; // 文件不存在不读取
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return;
    }

    const QJsonArray arr = doc.object().value(QStringLiteral("profiles")).toArray();
    for (const QJsonValue &value : arr) {
        const QJsonObject obj = value.toObject();
        // 只显示当前记录（mainid 匹配）的配置
        if (obj.value(QStringLiteral("mainid")).toString() != m_id) {
            continue;
        }
        const int row = m_profileModel->rowCount();
        m_profileModel->insertRow(row);
        m_profileModel->setItem(row, 0, new QStandardItem(obj.value(QStringLiteral("mainid")).toString()));
        m_profileModel->setItem(row, 1, new QStandardItem(obj.value(QStringLiteral("srcName")).toString()));
        m_profileModel->setItem(row, 2, new QStandardItem(obj.value(QStringLiteral("version")).toString()));
        m_profileModel->setItem(row, 3,
            new QStandardItem(obj.value(QStringLiteral("install")).toBool()
                                  ? QStringLiteral("是") : QStringLiteral("否")));
        m_profileModel->setItem(row, 4,
            new QStandardItem(obj.value(QStringLiteral("build")).toBool()
                                  ? QStringLiteral("是") : QStringLiteral("否")));
        m_profileModel->setItem(row, 5, new QStandardItem(obj.value(QStringLiteral("dshHome")).toString()));
        // 运行类型：兼容旧字段 instanceName
        m_profileModel->setItem(row, 6,
            new QStandardItem(obj.value(QStringLiteral("runType"))
                                  .toString(obj.value(QStringLiteral("instanceName")).toString())));
        m_profileModel->setItem(row, 7, new QStandardItem(obj.value(QStringLiteral("port")).toString()));
    }
}
