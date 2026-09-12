#include "SettingsDlg.h"
#include "DSHChatWindow/DSHCommander.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

CDSHSettingsDlg::CDSHSettingsDlg(CDSHCommander *cmd, QWidget *parent)
    : QDialog(parent)
    , m_cmd(cmd)
{
    buildUi();
    connect(m_cmd, &CDSHCommander::commandFinished,
            this, &CDSHSettingsDlg::onCommandFinished);
    m_describeRpcId = m_cmd->settingsDescribe(); // 读取通用设置
}

void CDSHSettingsDlg::buildUi()
{
    setWindowTitle(QStringLiteral("设置"));
    setModal(false);
    resize(820, 620);
    setStyleSheet(QStringLiteral(
        "QDialog { background-color:#151517; }"
        "QWidget { color:#f9fafb; font-family:'Segoe UI','Microsoft YaHei'; font-size:14px; }"
        "QListWidget, QScrollArea, QLineEdit { background-color:#1B1E24; color:#f9fafb;"
        "  border:1px solid #2C2F35; border-radius:6px; padding:6px; }"
        "QCheckBox { color:#f9fafb; }"
        "QPushButton { background-color:#2A2D33; color:#f9fafb; border:1px solid #2C2F35;"
        "  border-radius:6px; padding:6px 14px; }"
        "QPushButton:hover { background-color:#34373E; }"
        "QPushButton#closeBtn { background:transparent; border:none; font-size:18px; }"
        "QListWidget::item { padding:10px 12px; }"
        "QListWidget::item:selected { background-color:#2C2F35; color:#f9fafb; }"
    ));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 顶部标题栏:设置 + 打开配置文件 + 关闭
    auto *topBar = new QHBoxLayout;
    topBar->setContentsMargins(16, 12, 12, 10);
    auto *title = new QLabel(QStringLiteral("设置"));
    title->setStyleSheet(QStringLiteral("font-size:18px;font-weight:bold;"));
    topBar->addWidget(title);
    topBar->addStretch(1);
    auto *openCfg = new QPushButton(QStringLiteral("打开配置文件"));
    topBar->addWidget(openCfg);
    auto *closeBtn = new QPushButton(QStringLiteral("✕"));
    closeBtn->setObjectName(QStringLiteral("closeBtn"));
    closeBtn->setFixedWidth(32);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    topBar->addWidget(closeBtn);
    root->addLayout(topBar);

    // 主体:左导航 + 右侧堆叠
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    m_navList = new QListWidget(splitter);
    m_navList->setMinimumWidth(150);
    m_navList->setMaximumWidth(200);
    m_navList->addItem(QStringLiteral("通用设置"));
    m_navList->addItem(QStringLiteral("模型"));
    m_navList->addItem(QStringLiteral("插件"));
    m_navList->addItem(QStringLiteral("Agent 预设"));
    splitter->addWidget(m_navList);

    m_stack = new QStackedWidget(splitter);

    // ---- 页0:通用设置(读取 settings)----
    auto *page0 = new QWidget;
    auto *v0 = new QVBoxLayout(page0);
    v0->setContentsMargins(16, 4, 16, 12);
    v0->setSpacing(8);
    auto *gsSplit = new QSplitter(Qt::Horizontal, page0);
    m_nsList = new QListWidget(gsSplit);
    m_nsList->setMinimumWidth(180);
    gsSplit->addWidget(m_nsList);
    m_scroll = new QScrollArea(gsSplit);
    m_scroll->setWidgetResizable(true);
    m_formHost = new QWidget;
    m_formLayout = new QVBoxLayout(m_formHost);
    m_formLayout->setContentsMargins(8, 8, 8, 8);
    m_formLayout->setSpacing(8);
    m_scroll->setWidget(m_formHost);
    gsSplit->addWidget(m_scroll);
    gsSplit->setStretchFactor(0, 0);
    gsSplit->setStretchFactor(1, 1);
    v0->addWidget(gsSplit, 1);
    m_saveBtn = new QPushButton(QStringLiteral("保存"), page0);
    m_saveBtn->setFixedWidth(96);
    connect(m_saveBtn, &QPushButton::clicked, this, &CDSHSettingsDlg::onSave);
    v0->addWidget(m_saveBtn, 0, Qt::AlignRight);
    m_stack->addWidget(page0);

    // ---- 页1/2/3:占位(后续接入模型/插件/Agent 预设)----
    m_stack->addWidget(placeholderPage(QStringLiteral("模型:填入各提供方的 API 密钥即可使用其模型(待接入)。")));
    m_stack->addWidget(placeholderPage(QStringLiteral("插件:配置和查看本部署已安装的插件(待接入)。")));
    m_stack->addWidget(placeholderPage(QStringLiteral("Agent 预设:内置与自定义预设管理(待接入)。")));

    splitter->addWidget(m_stack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);

    connect(m_navList, &QListWidget::currentRowChanged, this, &CDSHSettingsDlg::onNavChanged);
    connect(m_nsList, &QListWidget::currentRowChanged, this, &CDSHSettingsDlg::onNamespaceChanged);
    m_navList->setCurrentRow(0);
}

QWidget *CDSHSettingsDlg::placeholderPage(const QString &text)
{
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    auto *label = new QLabel(text);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color:#adb2b8;font-size:14px;"));
    l->addWidget(label);
    l->addStretch(1);
    return w;
}

void CDSHSettingsDlg::onNavChanged(int row)
{
    if (m_stack && row >= 0 && row < m_stack->count())
        m_stack->setCurrentIndex(row);
}

void CDSHSettingsDlg::onCommandFinished(const QString &rpcId, bool ok, const QJsonObject &result)
{
    if (rpcId == m_describeRpcId) {
        m_namespaces = result.value(QLatin1String("namespaces")).toArray();
        const bool writable = result.value(QLatin1String("writable")).toBool();
        m_saveBtn->setEnabled(writable);
        m_nsList->clear();
        for (const QJsonValue &v : m_namespaces) {
            const QJsonObject ns = v.toObject();
            m_nsList->addItem(ns.value(QLatin1String("ns")).toString());
        }
        if (m_nsList->count() > 0)
            m_nsList->setCurrentRow(0);
        return;
    }
    if (rpcId == m_updateRpcId) {
        m_saveBtn->setEnabled(true);
        if (!ok) {
            QMessageBox::warning(this, QStringLiteral("设置"),
                QStringLiteral("保存失败:%1").arg(result.value(QLatin1String("message")).toString()));
            return;
        }
        QMessageBox::information(this, QStringLiteral("设置"), QStringLiteral("设置已保存"));
        m_describeRpcId = m_cmd->settingsDescribe();
    }
}

void CDSHSettingsDlg::onNamespaceChanged(int row)
{
    if (row < 0 || row >= m_namespaces.size())
        return;
    showNamespace(m_namespaces.at(row).toObject());
}

void CDSHSettingsDlg::showNamespace(const QJsonObject &ns)
{
    m_ns = ns.value(QLatin1String("ns")).toString();
    m_value = ns.value(QLatin1String("value")).toObject();

    while (QLayoutItem *it = m_formLayout->takeAt(0)) {
        if (QWidget *w = it->widget())
            w->deleteLater();
        delete it;
    }
    m_editors.clear();

    auto *title = new QLabel(QStringLiteral("命名空间:%1(作用域:%2)")
        .arg(m_ns, ns.value(QLatin1String("applies")).toString()));
    title->setStyleSheet(QStringLiteral("font-weight:bold;"));
    m_formLayout->addWidget(title);

    const QJsonArray secrets = ns.value(QLatin1String("secrets")).toArray();
    if (!secrets.isEmpty()) {
        QString s;
        for (const QJsonValue &sv : secrets) {
            const QJsonObject so = sv.toObject();
            const QJsonArray path = so.value(QLatin1String("path")).toArray();
            s += QStringLiteral("%1(%2)  ")
                    .arg(path.isEmpty() ? QString() : path.last().toString(),
                         so.value(QLatin1String("set")).toBool()
                             ? QStringLiteral("已设置") : QStringLiteral("未设置"));
        }
        auto *secretLabel = new QLabel(QStringLiteral("密钥:%1").arg(s));
        secretLabel->setStyleSheet(QStringLiteral("color:#adb2b8;"));
        m_formLayout->addWidget(secretLabel);
    }

    for (auto it = m_value.constBegin(); it != m_value.constEnd(); ++it) {
        const QString key = it.key();
        const QJsonValue v = it.value();
        QWidget *ed = nullptr;
        if (v.isBool()) {
            auto *cb = new QCheckBox(key);
            cb->setChecked(v.toBool());
            ed = cb;
            m_formLayout->addWidget(ed);
        } else {
            auto *row = new QWidget;
            auto *hl = new QHBoxLayout(row);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->addWidget(new QLabel(QStringLiteral("%1:").arg(key)));
            auto *le = new QLineEdit(v.isDouble() ? QString::number(v.toDouble()) : v.toString());
            le->setMinimumWidth(340);
            hl->addWidget(le, 1);
            ed = le;
            m_formLayout->addWidget(row);
        }
        m_editors.append(qMakePair(key, ed));
    }

    m_formLayout->addStretch(1);
}

QJsonValue CDSHSettingsDlg::editorValue(const QWidget *w, const QJsonValue &orig) const
{
    if (const auto *cb = qobject_cast<const QCheckBox *>(w))
        return cb->isChecked();
    if (const auto *le = qobject_cast<const QLineEdit *>(w)) {
        const QString t = le->text().trimmed();
        if (orig.isDouble())
            return t.toDouble();
        if (orig.isBool())
            return QJsonValue(t.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
        return QJsonValue(t);
    }
    return QJsonValue(QJsonValue::Null);
}

void CDSHSettingsDlg::onSave()
{
    if (m_ns.isEmpty())
        return;
    QJsonObject patch;
    for (const auto &p : m_editors) {
        const QJsonValue nv = editorValue(p.second, m_value.value(p.first));
        if (nv != m_value.value(p.first))
            patch.insert(p.first, nv);
    }
    if (patch.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("设置"), QStringLiteral("没有修改"));
        return;
    }
    m_saveBtn->setEnabled(false);
    m_updateRpcId = m_cmd->settingsUpdate(m_ns, patch);
}
