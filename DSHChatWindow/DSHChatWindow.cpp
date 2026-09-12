#include "DSHChatWindow.h"
#include "DSHCommander.h"
#include "DSDropTextEdit.h"

#include <algorithm>
#include <utility>

#include <QAbstractTextDocumentLayout>
#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPageSize>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QShortcut>
#include <QSplitter>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QSystemTrayIcon>

namespace {
// 节点 data 的字段名
const char *kNodeKind = "kind";   // "workspace" | "session"
const char *kNodeId   = "id";

// 相对时间:刚刚 / X分钟 / X小时 / X天 / X月 / X年
QString relativeTime(qint64 ms)
{
    if (ms <= 0)
        return QString();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 diff = now - ms;
    if (diff < 0)
        diff = 0;
    const qint64 sec = diff / 1000;
    if (sec < 60)
        return QStringLiteral("刚刚");
    const qint64 min = sec / 60;
    if (min < 60)
        return QStringLiteral("%1分钟").arg(min);
    const qint64 hour = min / 60;
    if (hour < 24)
        return QStringLiteral("%1小时").arg(hour);
    const qint64 day = hour / 24;
    if (day < 30)
        return QStringLiteral("%1天").arg(day);
    const qint64 month = day / 30;
    if (month < 12)
        return QStringLiteral("%1月").arg(month);
    return QStringLiteral("%1年").arg(day / 365);
}

// 去掉 "session-" 前缀后的短 id(无标题时的占位,不再显示 session-xxxx)
QString shortSessionId(const QString &sid)
{
    QString s = sid;
    if (s.startsWith(QStringLiteral("session-")))
        s = s.mid(8);
    return s.left(8);
}

// Markdown → HTML(用 Qt 的 setMarkdown 渲染,取 body 内容)
QString markdownToHtml(const QString &md)
{
    if (md.trimmed().isEmpty())
        return QString();
    QTextDocument doc;
    doc.setMarkdown(md);
    const QString html = doc.toHtml();
    const int s = html.indexOf(QLatin1String("<body"));
    if (s < 0)
        return QString();
    const int gt = html.indexOf('>', s);
    if (gt < 0)
        return QString();
    const int e = html.lastIndexOf(QLatin1String("</body>"));
    if (e < 0)
        return QString();
    return html.mid(gt + 1, e - gt - 1);
}

// 只取 thinking 文本的最后一行(折叠显示,避免一长串原始思考)
QString thinkLastLine(const QString &text)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    QString last;
    for (const QString &line : lines) {
        if (!line.trimmed().isEmpty())
            last = line.trimmed();
    }
    if (last.length() > 160)
        last = last.left(160) + QStringLiteral("...");
    return last;
}

// 权限 preset 值 → 显示名
QString permissionDisplayName(const QString &v)
{
    if (v == QStringLiteral("read-only"))
        return QStringLiteral("Read Only");
    if (v == QStringLiteral("workspace-write"))
        return QStringLiteral("Workspace Write");
    if (v == QStringLiteral("danger-full-access"))
        return QStringLiteral("Full access");
    if (v == QStringLiteral("custom"))
        return QStringLiteral("Custom");
    return v;
}

// 判断是否系统上下文注入(dsh-system-prompt 的 runtime/policy 信息),这类内容不显示
bool isSystemInjection(const QString &text)
{
    const QString t = text.trimmed();
    return t.startsWith(QStringLiteral("Current runtime context"))
        || t.startsWith(QStringLiteral("Current DSH file policy"))
        || t.startsWith(QStringLiteral("Approval policy"))
        || text.contains(QStringLiteral("@deepseek-ai/dsh-system-prompt"));
}

// 是否包含中文字符(含中文的提问才建立索引;全英文多为编译报错,不建索引)
bool containsCjk(const QString &text)
{
    for (const QChar &c : text) {
        const ushort u = c.unicode();
        if (u >= 0x4E00 && u <= 0x9FFF)   // CJK 统一表意
            return true;
        if (u >= 0x3400 && u <= 0x4DBF)   // CJK 扩展 A
            return true;
    }
    return false;
}

// 从工具参数里取文件名(file_path / path 的 basename)
QString toolFileName(const QJsonValue &args)
{
    QJsonObject obj;
    if (args.isObject()) {
        obj = args.toObject();
    } else if (args.isString()) {
        QJsonParseError pe;
        const QJsonDocument d = QJsonDocument::fromJson(args.toString().toUtf8(), &pe);
        if (pe.error == QJsonParseError::NoError && d.isObject())
            obj = d.object();
    }
    if (obj.isEmpty())
        return QString();
    QString fp = obj.value(QLatin1String("file_path")).toString();
    if (fp.isEmpty())
        fp = obj.value(QLatin1String("path")).toString();
    if (fp.isEmpty())
        return QString();
    return QFileInfo(fp).fileName(); // 只取文件名
}

}

CDSHChatWindow::CDSHChatWindow(const QString &address, const QString &tokenUrl, bool autoConnect,
                               QWidget *parent)
    : QMainWindow(parent)
{
    m_commander = new CDSHCommander(this);
    m_tokenUrl = tokenUrl.trimmed();

    buildUi();
    applyStyle();

    setWindowTitle(QStringLiteral("客户端: 127.0.0.1:8888")); // MDI 标题:客户端: 网址(默认地址)
    resize(1180, 760);

    connect(m_connectBtn, &QPushButton::clicked, this, &CDSHChatWindow::onConnectClicked);
    // 右键菜单:归档会话等
    m_sessionTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_sessionTree, &QTreeWidget::customContextMenuRequested,
            this, &CDSHChatWindow::onTreeContextMenu);
    connect(m_sessionTree, &QTreeWidget::currentItemChanged,
            this, &CDSHChatWindow::onTreeItemChanged);
    connect(m_sendBtn, &QPushButton::clicked, this, &CDSHChatWindow::onSendClicked);
    connect(m_inputEdit, &QPlainTextEdit::textChanged, this, &CDSHChatWindow::onInputTextChanged);
    // 提问交互面板:提交 / 跳过
    connect(m_qSubmit, &QPushButton::clicked, this, [this]() {
        submitAnswer(QString(), m_qEdit->text().trimmed());
    });
    connect(m_qSkip, &QPushButton::clicked, this, [this]() {
        submitAnswer(QString(), QString()); // 跳过本题(回答留空)
    });
    connect(m_qEdit, &QLineEdit::returnPressed, m_qSubmit, &QPushButton::click);
    connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CDSHChatWindow::onModelChanged);

    // Enter 发送(Shift+Enter 仍为换行):用事件过滤器可靠拦截回车
    m_inputEdit->installEventFilter(this);
    // 问题记录表:双击跳到显示框对应提问处
    connect(m_questionList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (item)
            onQuestionJump(m_questionList->row(item));
    });

    // 消息气泡选择复制即可(QLabel 富文本支持选择 + Ctrl+C),无需自定义右键菜单

    connect(m_commander, &CDSHCommander::connectionChanged,
            this, &CDSHChatWindow::onConnectionChanged);
    connect(m_commander, &CDSHCommander::commandFinished,
            this, &CDSHChatWindow::onCommandFinished);
    connect(m_commander, &CDSHCommander::muxEventReceived,
            this, &CDSHChatWindow::onMuxEvent);
    connect(m_commander, &CDSHCommander::hostEventReceived,
            this, &CDSHChatWindow::onHostEvent);
    connect(m_commander, &CDSHCommander::sessionEventReceived,
            this, &CDSHChatWindow::onSessionEvent);
    connect(m_commander, &CDSHCommander::questionRequested,
            this, &CDSHChatWindow::onQuestionRequested);
    connect(m_commander, &CDSHCommander::approvalRequested,
            this, &CDSHChatWindow::onApprovalRequested);
    connect(m_commander, &CDSHCommander::errorOccurred,
            this, &CDSHChatWindow::onError);
    connect(m_commander, &CDSHCommander::workspaceFrame,
            this, &CDSHChatWindow::onWorkspaceFrame); // 工作区流 → 维护左侧树
    connect(m_commander, &CDSHCommander::sessionSnapshot,
            this, &CDSHChatWindow::onSessionSnapshot); // session.follow 快照 → 渲染历史

    // 启动后自动连接(DSH 服务默认运行在 127.0.0.1:8888)
    loadTitleCache(); // 先加载本地标题缓存,冷启动也能显示会话名称
    loadArchivedCache(); // 再加载本地归档集合
    setStatus(QStringLiteral("未连接"), QStringLiteral("#F87171"));

    // 指定地址(host:port)时:更新输入框、标题并连接该地址
    if (!address.isEmpty()) {
        QString host = QStringLiteral("127.0.0.1");
        quint16 port = 8888;
        const QStringList parts = address.trimmed().split(QLatin1Char(':'));
        if (!parts.isEmpty() && !parts[0].isEmpty())
            host = parts[0].trimmed();
        if (parts.size() > 1) {
            bool okNum = false;
            const int p = parts[1].trimmed().toInt(&okNum);
            if (okNum && p > 0 && p <= 65535)
                port = quint16(p);
        }
        m_commander->setServer(host, port);

        // 带 token 登录:把登录网址交给 commander, 地址栏/标题先显示带 token 网址(登录成功后切换为正常地址)。
        if (!m_tokenUrl.isEmpty()) {
            m_commander->setLoginUrl(m_tokenUrl);
            m_serverEdit->setText(m_tokenUrl);
            setWindowTitle(QStringLiteral("客户端: %1").arg(m_tokenUrl));
        } else {
            m_serverEdit->setText(address);
            setWindowTitle(QStringLiteral("客户端: %1").arg(address));
        }
    }

    updateUiEnabled(); // 初始未连接:输入框/发送/显示框等禁用
    // 仅“源码管理右键打开”自动连接(带 token); 手工打开由用户点“连接”。
    if (autoConnect)
        m_commander->connectToServer();
}

CDSHChatWindow::~CDSHChatWindow() = default;

void CDSHChatWindow::closeEvent(QCloseEvent *event)
{
    // 作为 MDI 子窗口:允许关闭,由 WA_DeleteOnClose 销毁
    event->accept();
}

// 拦截输入框的回车:无 Shift 时发送(吞掉事件),Shift+回车放行(换行)
bool CDSHChatWindow::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_inputEdit && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && !(ke->modifiers() & Qt::ShiftModifier)) {
            onSendClicked();
            return true; // 已处理,阻止默认换行
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void CDSHChatWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(12, 10, 12, 10); // 窗口四周留白
    root->setSpacing(10);

    auto *splitter = new QSplitter(Qt::Horizontal, central);
    root->addWidget(splitter);

    // ---- 左侧:工作区 / 会话 两级树 ----
    auto *leftPanel = new QWidget(splitter);
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(10, 0, 10, 0); // 左区额外左右内边距, 与中部拉开
    leftLayout->setSpacing(6);

    auto *header1 = new QLabel(QStringLiteral("工作区 / 会话"), leftPanel);
    header1->setObjectName(QStringLiteral("headerLabel"));
    leftLayout->addWidget(header1);

    m_sessionTree = new QTreeWidget(leftPanel);
    m_sessionTree->setColumnCount(2); // 列0=名称,列1=相对时间
    m_sessionTree->setHeaderHidden(true);
    m_sessionTree->setMinimumWidth(340); // 左侧工作区/会话树更宽, 保证名称等内容完整显示
    m_sessionTree->setIndentation(14);
    m_sessionTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_sessionTree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_sessionTree->setColumnWidth(1, 76);
    leftLayout->addWidget(m_sessionTree, 1);

    auto *newBtn = new QPushButton(QStringLiteral("＋ 新增工作区"), leftPanel);
    newBtn->setObjectName(QStringLiteral("plainBtn"));
    connect(newBtn, &QPushButton::clicked, this, &CDSHChatWindow::onAddWorkspaceClicked);
    leftLayout->addWidget(newBtn);

    auto *refreshBtn = new QPushButton(QStringLiteral("↻ 刷新"), leftPanel);
    refreshBtn->setObjectName(QStringLiteral("plainBtn"));
    connect(refreshBtn, &QPushButton::clicked, this, &CDSHChatWindow::onRefreshSessionsClicked);
    leftLayout->addWidget(refreshBtn);

    splitter->addWidget(leftPanel);

    // ---- 右侧:顶部 + 消息流 + 输入区 ----
    auto *rightPanel = new QWidget(splitter);
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(6);

    // 顶部行:服务器地址 + 连接按钮 + 状态 + 下载 PDF
    auto *topBar = new QHBoxLayout;
    m_serverEdit = new QLineEdit(QStringLiteral("127.0.0.1:8888"), rightPanel);
    // 地址栏加宽(占据较大伸缩比例), 便于完整显示带 token 的长网址
    topBar->addWidget(m_serverEdit, 4);
    m_connectBtn = new QPushButton(QStringLiteral("连接"), rightPanel);
    topBar->addWidget(m_connectBtn);
    m_statusLabel = new QLabel(rightPanel);
    topBar->addWidget(m_statusLabel, 1);
    m_exportBtn = new QPushButton(QStringLiteral("下载 PDF"), rightPanel);
    m_exportBtn->setFixedWidth(96);
    connect(m_exportBtn, &QPushButton::clicked, this, &CDSHChatWindow::onExportPdf);
    topBar->addWidget(m_exportBtn);
    rightLayout->addLayout(topBar);

    // 思考栏: 放到地址栏下方一行(不再与地址同排), 固定高度、文字超长省略
    auto *thinkRow = new QHBoxLayout;
    m_thinkLabel = new QLabel(rightPanel);
    m_thinkLabel->setWordWrap(false); // 不换行
    m_thinkLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed); // 横向撑满, 竖向固定
    m_thinkLabel->setFixedHeight(26);
    m_thinkLabel->setMinimumWidth(200);
    m_thinkLabel->setStyleSheet(QStringLiteral(
        "color:#cfd3d6;font-size:12px;padding:2px 6px;"
        "background-color:#1B1E24;border-radius:4px;"));
    m_thinkLabel->setText(QStringLiteral("思考:—")); // 常驻占位, 保证该行始终能看到思考栏
    thinkRow->addWidget(m_thinkLabel, 1);
    rightLayout->addLayout(thinkRow);

    // 第二行:会话标题 + Agent 预设(只读)
    auto *titleBar = new QHBoxLayout;
    m_titleLabel = new QLabel(QStringLiteral("未选择会话"), rightPanel);
    m_titleLabel->setStyleSheet(QStringLiteral("font-size:15px;font-weight:bold;"));
    titleBar->addWidget(m_titleLabel);
    m_presetLabel = new QLabel(rightPanel);
    m_presetLabel->setStyleSheet(QStringLiteral("color:#9AA0A8;font-size:13px;"));
    titleBar->addWidget(m_presetLabel);
    titleBar->addStretch(1);
    rightLayout->addLayout(titleBar);

    // 消息区:左侧为消息列(消息显示框 + 提问面板 + 配置 + 附件条 + 输入区),
    // 右侧为问题记录表。思考行已上移到顶部“网址行”; 附件条与消息显示框置于同一列。
    auto *chatSplit = new QSplitter(Qt::Horizontal, rightPanel);
    auto *msgCol = new QWidget(chatSplit);
    auto *msgColLayout = new QVBoxLayout(msgCol);
    msgColLayout->setContentsMargins(10, 0, 10, 0); // 中部消息列额外左右内边距
    msgColLayout->setSpacing(6);


    // 消息显示框(可放控件的滚动区)
    m_messageScroll = new QScrollArea(msgCol);
    m_messageScroll->setWidgetResizable(true);
    m_messageScroll->setFrameShape(QFrame::NoFrame);
    m_messageHost = new QWidget;
    m_messageHost->setStyleSheet(QStringLiteral("background-color:#000000;"));
    m_messageScroll->setStyleSheet(QStringLiteral("QScrollArea { background-color:#000000; }"
                                                  "QScrollArea > QWidget > QWidget { background-color:#000000; }"));
    m_messageLayout = new QVBoxLayout(m_messageHost);
    m_messageLayout->setContentsMargins(8, 8, 8, 8);
    m_messageLayout->setSpacing(8);
    m_messageScroll->setWidget(m_messageHost);
    msgColLayout->addWidget(m_messageScroll, 1);

    buildTodoDock(); // 消息显示框下方的可折叠任务进度面板(默认折叠, 空表时隐藏)

    // 提问交互面板(默认隐藏,收到提问时显示)
    m_qPanel = new QFrame(msgCol);
    m_qPanel->setObjectName(QStringLiteral("qPanel"));
    auto *qLayout = new QVBoxLayout(m_qPanel);
    qLayout->setContentsMargins(12, 10, 12, 10);
    qLayout->setSpacing(8);
    m_qHeader = new QLabel(m_qPanel);
    m_qHeader->setStyleSheet(QStringLiteral("color:#9AA0A8;font-size:12px;"));
    qLayout->addWidget(m_qHeader);
    m_qText = new QLabel(m_qPanel);
    m_qText->setWordWrap(true);
    m_qText->setStyleSheet(QStringLiteral("color:#E6E8EB;font-size:14px;font-weight:bold;"));
    qLayout->addWidget(m_qText);
    m_qOptions = new QVBoxLayout;
    qLayout->addLayout(m_qOptions);
    auto *qRow = new QHBoxLayout;
    m_qEdit = new QLineEdit(m_qPanel);
    m_qEdit->setPlaceholderText(QStringLiteral("输入你的答案..."));
    qRow->addWidget(m_qEdit, 1);
    m_qSubmit = new QPushButton(QStringLiteral("提交"), m_qPanel);
    m_qSubmit->setFixedWidth(80);
    qRow->addWidget(m_qSubmit);
    m_qSkip = new QPushButton(QStringLiteral("跳过本题"), m_qPanel);
    m_qSkip->setFixedWidth(88);
    m_qSkip->setObjectName(QStringLiteral("plainBtn"));
    qRow->addWidget(m_qSkip);
    qLayout->addLayout(qRow);
    m_qPanel->hide();
    msgColLayout->addWidget(m_qPanel);

    // 配置行:权限 + 模型(置于输入区之上,使发送按钮落在最底部)
    auto *cfgBar = new QHBoxLayout;
    cfgBar->addWidget(new QLabel(QStringLiteral("权限:"), msgCol));
    m_permissionCombo = new QComboBox(msgCol);
    m_permissionCombo->setMinimumWidth(140);
    cfgBar->addWidget(m_permissionCombo);
    cfgBar->addStretch(1);
    cfgBar->addWidget(new QLabel(QStringLiteral("模型:"), msgCol));
    m_modelCombo = new QComboBox(msgCol);
    m_modelCombo->setMinimumWidth(260);
    cfgBar->addWidget(m_modelCombo);
    msgColLayout->addLayout(cfgBar);

    // 附件条(拖入文件后显示附件名称,宽度与消息显示框一致)
    m_attachBar = new QWidget(msgCol);
    m_attachBar->setObjectName(QStringLiteral("attachBar"));
    m_attachBar->setStyleSheet(QStringLiteral(
        "QWidget#attachBar { background-color:#000000; border:1px solid #2C2F35; border-radius:6px; }"));
    m_attachBar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_attachBar->setMinimumHeight(24);
    m_attachLayout = new QHBoxLayout(m_attachBar);
    m_attachLayout->setContentsMargins(4, 4, 4, 4);
    m_attachLayout->setSpacing(6);
    m_attachBar->hide();
    msgColLayout->addWidget(m_attachBar);

    // 输入区:输入框 + 发送按钮(发送按钮保持在最底部)
    auto *inputRow = new QHBoxLayout;
    m_inputEdit = new CDSDropTextEdit(msgCol); // 支持把文件拖入(交给 DSH 附件协议处理)
    m_inputEdit->setObjectName(QStringLiteral("inputEdit")); // 便于单独设置选区高亮样式
    m_inputEdit->setPlaceholderText(QStringLiteral("输入消息,Enter 发送,Shift+Enter 换行(支持拖入图片附件)"));
    m_inputEdit->setFixedHeight(88); // 默认高度;发送后回归、随内容变化
    // 确保输入框可编辑, 且已输入文字可用鼠标/键盘选中复制。
    // 注意: 用 setTextInteractionFlags 会整体覆盖默认值, 必须包含 Qt::TextEditable,
    // 否则输入框变成只读、无法输入。
    m_inputEdit->setTextInteractionFlags(Qt::TextEditable | Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    connect(m_inputEdit, &CDSDropTextEdit::fileDropped, this, &CDSHChatWindow::onDropFile);
    inputRow->addWidget(m_inputEdit, 1);
    m_sendBtn = new QPushButton(QStringLiteral("发送"), msgCol);
    m_sendBtn->setFixedWidth(88);
    m_sendBtn->setFixedHeight(30);
    m_sendBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    inputRow->addWidget(m_sendBtn);
    msgColLayout->addLayout(inputRow);

    chatSplit->addWidget(msgCol);
    m_questionList = new QListWidget(chatSplit);
    m_questionList->setObjectName(QStringLiteral("questionList"));
    m_questionList->setMinimumWidth(150);
    m_questionList->setMaximumWidth(300);
    m_questionList->setToolTip(QStringLiteral("我的提问(双击跳转到显示框对应位置)"));
    chatSplit->addWidget(m_questionList);
    chatSplit->setHandleWidth(14); // 中部消息列 与 右侧问题表 之间的分隔带加宽(更多间距)
    chatSplit->setStretchFactor(0, 1);
    chatSplit->setStretchFactor(1, 0);

    rightLayout->addWidget(chatSplit, 1);

    updateSendButton(); // 主按钮初始为“发送”

    splitter->addWidget(rightPanel);
    splitter->setHandleWidth(14); // 左侧会话树 与 右侧 之间的分隔带加宽(更多间距)
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({560, 900}); // 左侧工作区/会话树起始更宽

    setCentralWidget(central);
}

// ------------------------------------------------------------------
// 可折叠任务进度面板(todo/write 事件驱动, 官方 TodoDock 的等价实现)
// 放在消息显示框下方、输入区上方; 默认折叠, 列表空时整体隐藏。
// ------------------------------------------------------------------
void CDSHChatWindow::buildTodoDock()
{
    if (!m_messageLayout || !m_messageScroll)
        return; // 需在消息区构建之后调用
    // 需要在 msgCol 里、m_messageScroll 之后插入; 通过父容器拿 layout
    QWidget *parent = m_messageScroll->parentWidget();
    if (!parent)
        return;
    QVBoxLayout *col = qobject_cast<QVBoxLayout *>(parent->layout());
    if (!col)
        return;

    m_todoDock = new QWidget(parent);
    m_todoDock->setObjectName(QStringLiteral("todoDock"));
    m_todoDock->setStyleSheet(QStringLiteral(
        "QWidget#todoDock { background-color:#1B1E24; border:1px solid #2C2F35; border-radius:6px; }"));

    auto *vl = new QVBoxLayout(m_todoDock);
    vl->setContentsMargins(6, 4, 6, 6);
    vl->setSpacing(4);

    // 可折叠头: 标题 + 进度汇总 + 折叠箭头
    m_todoToggle = new QToolButton(m_todoDock);
    m_todoToggle->setObjectName(QStringLiteral("todoToggle"));
    m_todoToggle->setCheckable(false);
    m_todoToggle->setAutoRaise(false);
    m_todoToggle->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_todoToggle->setText(QStringLiteral("任务进度"));
    m_todoToggle->setStyleSheet(QStringLiteral(
        "QToolButton#todoToggle { background-color:transparent; border:none;"
        "  color:#cfd3d6; font-size:12px; text-align:left; padding:2px 4px; }"
        "QToolButton#todoToggle:hover { color:#f9fafb; }"));
    connect(m_todoToggle, &QToolButton::clicked, this, &CDSHChatWindow::toggleTodoDock);
    vl->addWidget(m_todoToggle);

    // 展开后的进度列表
    m_todoList = new QListWidget(m_todoDock);
    m_todoList->setObjectName(QStringLiteral("todoList"));
    m_todoList->setFrameShape(QFrame::NoFrame);
    m_todoList->setStyleSheet(QStringLiteral(
        "QListWidget#todoList { background-color:transparent; border:none; color:#cfd3d6;"
        "  font-size:12px; }"
        "QListWidget#todoList::item { padding:2px 6px; }"));
    m_todoList->setSelectionMode(QAbstractItemView::NoSelection);
    m_todoList->setFocusPolicy(Qt::NoFocus);
    vl->addWidget(m_todoList);

    // 默认折叠 & 空表时隐藏
    m_todoCollapsed = true;
    m_todoList->setVisible(false);
    m_todoDock->hide();

    // 插入到消息显示框之后、提问面板之前
    int idx = col->indexOf(m_messageScroll);
    if (idx >= 0)
        col->insertWidget(idx + 1, m_todoDock);
}

// todo/write → 整表替换(每次携带完整列表), 保存并刷新面板
void CDSHChatWindow::onTodoWrite(const QJsonArray &todos)
{
    m_todos.clear();
    for (const QJsonValue &v : todos) {
        const QJsonObject o = v.toObject();
        QString content = o.value(QLatin1String("content")).toString();
        QString st = o.value(QLatin1String("status")).toString();
        int status = 0; // pending
        if (st == QLatin1String("in_progress")) status = 1;
        else if (st == QLatin1String("completed")) status = 2;
        m_todos.append(qMakePair(content, status));
    }
    updateTodoDock();
}

// 刷新折叠标题汇总 + 展开列表内容
void CDSHChatWindow::updateTodoDock()
{
    if (!m_todoDock)
        return;
    if (m_todos.isEmpty()) {
        clearTodoDock(); // 空表隐藏
        return;
    }
    int done = 0, active = 0, pending = 0;
    for (const auto &t : qAsConst(m_todos)) {
        if (t.second == 2) ++done;
        else if (t.second == 1) ++active;
        else ++pending;
    }
    QStringList parts;
    if (done > 0)    parts << QStringLiteral("完成 %1").arg(done);
    if (active > 0)  parts << QStringLiteral("进行中 %1").arg(active);
    if (pending > 0) parts << QStringLiteral("待办 %1").arg(pending);
    const QString arrow = m_todoCollapsed ? QStringLiteral("▸") : QStringLiteral("▾");
    m_todoToggle->setText(QStringLiteral("%1  任务进度  %2")
        .arg(arrow, parts.join(QStringLiteral(" · "))));
    m_todoList->clear();
    for (const auto &t : qAsConst(m_todos)) {
        QString glyph;
        if (t.second == 2)      glyph = QStringLiteral("✔");
        else if (t.second == 1) glyph = QStringLiteral("◌");
        else                    glyph = QStringLiteral("○");
        auto *item = new QListWidgetItem(QStringLiteral("%1 %2").arg(glyph, t.first), m_todoList);
        if (t.second == 2)
            item->setForeground(QColor(QStringLiteral("#8A8F96"))); // 完成: 灰
        else if (t.second == 1)
            item->setForeground(QColor(QStringLiteral("#2563EB"))); // 进行中: 蓝
    }
    m_todoList->setVisible(!m_todoCollapsed);
    m_todoDock->show();
}

// 折叠/展开切换
void CDSHChatWindow::toggleTodoDock()
{
    if (!m_todoDock || m_todos.isEmpty())
        return;
    m_todoCollapsed = !m_todoCollapsed;
    updateTodoDock();
}

// 清空进度(turn/start 每轮重置 / 清空消息区时)
void CDSHChatWindow::clearTodoDock()
{
    m_todos.clear();
    if (m_todoList)
        m_todoList->clear();
    if (m_todoDock)
        m_todoDock->hide();
}

void CDSHChatWindow::applyStyle()
{
    // 深色主题(对齐官方 DSH:背景 #151517,主文字 #f9fafb,次级 #cfd3d6/#adb2b8,边框 rgba(255,255,255,12%))
    setStyleSheet(QStringLiteral(
        "QMainWindow { background-color: #151517; }"
        "QWidget { color: #f9fafb; font-size: 13px; font-family: 'Segoe UI','Microsoft YaHei'; }"
        "QLabel#headerLabel { color: #cfd3d6; font-size: 12px; }"
        "QLineEdit, QComboBox, QPlainTextEdit, QListWidget, QTreeWidget, QTextBrowser {"
        "  background-color: #1B1E24; color: #f9fafb;"
        "  border: 1px solid #2C2F35; border-radius: 6px; padding: 4px;"
        "  selection-background-color: #2C2F35; selection-color: #f9fafb;"
        "}"
        "QComboBox QAbstractItemView {"
        "  background-color: #1B1E24; color: #f9fafb;"
        "  selection-background-color: #2C2F35; selection-color: #f9fafb;"
        "}"
        "QPushButton { background-color: #2A2D33; color: #f9fafb;"
        "  border: 1px solid #2C2F35; border-radius: 6px; padding: 6px 14px; }"
        "QPushButton:hover { background-color: #34373E; }"
        "QPushButton:disabled { background-color: #22252B; color: #6B6F76; }"
        "QPushButton#plainBtn { background-color: #1B1E24; color: #f9fafb; }"
        "QPushButton#plainBtn:hover { background-color: #2A2D33; }"
        "QTreeWidget::item { padding: 4px 6px; }"
        "QTreeWidget::item:selected { background-color: #2C2F35; color: #f9fafb; }"
        "QListWidget::item { padding: 4px 6px; }"
        "QListWidget::item:selected { background-color: #2C2F35; color: #f9fafb; }"
        "QListWidget#questionList { border: 1px solid #3A3F47; border-radius: 6px; }"
        "QFrame#qPanel {"
        "  background-color: #1B1E24; border: 1px solid #2C2F35; border-radius: 6px;"
        "}"
        "QMenu { background-color: #1B1E24; color: #f9fafb; border: 1px solid #2C2F35; }"
        "QMenu::item { padding: 6px 24px 6px 12px; }"
        "QMenu::item:selected { background-color: #2C2F35; color: #f9fafb; }"
        "QSplitter::handle { background-color: #23262B; width: 1px; }"
        "QScrollBar:vertical { background: #151517; width: 10px; }"
        "QScrollBar::handle:vertical { background: #2C2F35; border-radius: 5px; min-height: 24px; }"
        "QScrollBar::add-line, QScrollBar::sub-line { height: 0; }"
        "QMessageBox {"
        "  background-color: #1B1E24; color: #f9fafb;"
        "  border: 1px solid #2C2F35;"
        "}"
        "QMessageBox QLabel { color: #f9fafb; }"
        "QMessageBox QPushButton {"
        "  background-color: #2A2D33; color: #f9fafb;"
        "  border: 1px solid #2C2F35; border-radius: 6px; padding: 6px 16px;"
        "}"
        "QMessageBox QPushButton:hover { background-color: #34373E; }"
        // 消息输入框: 选中文本时用醒目的蓝色高亮, 反色显示白色文字, 便于辨认
        "QPlainTextEdit#inputEdit {"
        "  selection-background-color: #2563EB;"
        "  selection-color: #FFFFFF;"
        "}"
    ));
}

void CDSHChatWindow::setStatus(const QString &text, const QString &color)
{
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(QStringLiteral("color:%1;").arg(color));
}

void CDSHChatWindow::updateUiEnabled()
{
    const bool canSend = m_connected && !m_currentSessionId.isEmpty();
    m_inputEdit->setEnabled(canSend);
    m_sendBtn->setEnabled(canSend);
    m_permissionCombo->setEnabled(canSend);
    m_modelCombo->setEnabled(canSend);
    updateSendButton(); // 会话切换/连接变化时同步发送/停止按钮文本
    // 断开时消息显示框也禁用
    if (m_messageScroll)
        m_messageScroll->setEnabled(m_connected);
    m_questionList->setEnabled(m_connected);
    // 断开或未选会话时,清空消息显示区与问题索引
    if (!m_connected || m_currentSessionId.isEmpty())
        clearMessageArea();
}

// 清空消息显示框与问题索引(断开/未选会话时调用)
void CDSHChatWindow::clearMessageArea()
{
    // 清空消息容器
    if (m_messageLayout) {
        while (QLayoutItem *item = m_messageLayout->takeAt(0)) {
            if (QWidget *w = item->widget())
                w->deleteLater();
            delete item;
        }
    }
    m_userBubbles.clear();
    if (m_questionList)
        m_questionList->clear();
    m_inputEdit->clear(); // 清空输入框
    clearAttachments();   // 清空待发送的图片附件
    m_thinking.clear();
    if (m_thinkLabel)
        m_thinkLabel->setText(QStringLiteral("思考:—")); // 回到占位(常驻)
    clearTodoDock();      // 清空任务进度面板(换会话/断开时)
}

// 新消息加入后,把消息显示框滚动到底部:
// 先让 host 布局立即重算内容长度, 再把垂直滚动条拉到最大值(即最底)。
// 内容高度更新后 QScrollArea 的滚动范围才反映新长度, 此时 setValue(maximum()) 生效。
void CDSHChatWindow::scrollMessageToBottom()
{
    if (!m_messageScroll)
        return;
    m_messageHost->layout()->activate();
    m_messageScroll->verticalScrollBar()->setValue(m_messageScroll->verticalScrollBar()->maximum());
}

// 添加一条气泡:用户消息 50% 宽靠右,助手消息 5/6 宽靠左
void CDSHChatWindow::addBubble(const QString &html, bool right, bool trackUser)
{
    auto *bubble = new QLabel;
    bubble->setTextFormat(Qt::RichText);
    bubble->setWordWrap(true);
    bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
    bubble->setAlignment(Qt::AlignLeft); // 气泡内文本左对齐
    bubble->setStyleSheet(right
        ? QStringLiteral("background-color:#1F6F43;border-radius:8px;padding:8px 10px;color:#f9fafb;")
        : QStringLiteral("background-color:#1B1E24;border-radius:8px;padding:8px 10px;color:#f9fafb;"));
    bubble->setText(html);

    auto *wrap = new QWidget;
    auto *hl = new QHBoxLayout(wrap);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);
    if (right) {
        // 用户:5/6 宽,靠右
        hl->addStretch(1);
        hl->addWidget(bubble, 5);
    } else {
        // 助手:5/6 宽,靠左
        hl->addWidget(bubble, 5);
        hl->addStretch(1);
    }
    m_messageLayout->addWidget(wrap);
    // 新消息加入后,把消息显示框滚动到底部(用户最新消息可见)
    scrollMessageToBottom();
    if (right && trackUser)
        m_userBubbles.append(wrap);
}

// 添加一条系统灰字
void CDSHChatWindow::addSystemLine(const QString &text)
{
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    l->setStyleSheet(QStringLiteral("color:#adb2b8;font-size:12px;padding:2px 6px;"));
    m_messageLayout->addWidget(l);
    scrollMessageToBottom();
}

// 添加上下文注入卡片
void CDSHChatWindow::addContextCard(const QString &text)
{
    // 上下文注入:仅显示一行标题,点击展开详细内容(像思考块那样折叠)
    auto *container = new QWidget;
    auto *vl = new QVBoxLayout(container);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(4);

    auto *btn = new QToolButton;
    btn->setCheckable(true);
    btn->setAutoRaise(false);
    btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btn->setText(QStringLiteral("上下文注入 · @deepseek-ai/dsh-system-prompt   ▸"));
    btn->setStyleSheet(QStringLiteral(
        "QToolButton { background-color:#20242B; border:1px solid #2C2F35;"
        "  border-radius:8px; padding:6px 10px; color:#adb2b8; text-align:left; }"
        "QToolButton:checked { border-color:#3A3F47; background-color:#26292F; color:#f9fafb; }"
        "QToolButton:hover { background-color:#26292F; }"));

    auto *area = new QLabel(text);
    area->setWordWrap(true);
    area->setTextInteractionFlags(Qt::TextSelectableByMouse);
    area->setTextFormat(Qt::PlainText);
    area->setStyleSheet(QStringLiteral(
        "background-color:#16181C; border:1px solid #26292F; border-radius:8px;"
        " padding:6px 8px; color:#cfd3d6;"));
    area->hide();

    connect(btn, &QToolButton::toggled, this, [btn, area](bool on) {
        btn->setText(on ? QStringLiteral("上下文注入 · @deepseek-ai/dsh-system-prompt   ▾")
                        : QStringLiteral("上下文注入 · @deepseek-ai/dsh-system-prompt   ▸"));
        area->setVisible(on);
    });

    vl->addWidget(btn);
    vl->addWidget(area);
    m_messageLayout->addWidget(container);
    scrollMessageToBottom();
}

// 可展开的工具参数卡:点击按钮展开/折叠完整参数
void CDSHChatWindow::addToolCallWidget(const QString &name, const QString &file, const QString &args)
{
    auto *tool = new QWidget;
    auto *vl = new QVBoxLayout(tool);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(4);

    auto *btn = new QToolButton;
    btn->setCheckable(true);
    btn->setAutoRaise(false);
    btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btn->setText(QStringLiteral("🔧 %1 %2   ▸").arg(name, file));
    btn->setStyleSheet(QStringLiteral(
        "QToolButton { background-color:#1B1E24; border:1px solid #2C2F35;"
        "  border-radius:6px; padding:4px 10px; color:#f9fafb; text-align:left; }"
        "QToolButton:checked { border-color:#3A3F47; background-color:#20242B; }"
        "QToolButton:hover { background-color:#20242B; }"));

    auto *argLabel = new QLabel;
    argLabel->setTextFormat(Qt::PlainText);
    argLabel->setWordWrap(true);
    argLabel->setStyleSheet(QStringLiteral(
        "background-color:#16181C; border:1px solid #26292F; border-radius:6px;"
        " padding:6px 8px; color:#cfd3d6; font-family:Consolas,'Microsoft YaHei'; font-size:12px;"));
    if (!args.isEmpty())
        argLabel->setText(args);
    argLabel->hide();

    connect(btn, &QToolButton::toggled, this, [btn, argLabel](bool on) {
        btn->setText(QStringLiteral("🔧 %1 %2   %3")
            .arg(btn->property("toolName").toString(),
                 btn->property("toolFile").toString(),
                 on ? QStringLiteral("▾") : QStringLiteral("▸")));
        argLabel->setVisible(on);
    });
    btn->setProperty("toolName", name);
    btn->setProperty("toolFile", file);

    vl->addWidget(btn);
    vl->addWidget(argLabel);
    m_messageLayout->addWidget(tool);
    scrollMessageToBottom();
}

// 可展开的思考块:点击展开/折叠思考全文
void CDSHChatWindow::addThinkBlock(const QString &text)
{
    if (text.trimmed().isEmpty())
        return;
    auto *container = new QWidget;
    auto *vl = new QVBoxLayout(container);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(4);

    auto *btn = new QToolButton;
    btn->setCheckable(true);
    btn->setAutoRaise(false);
    btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    btn->setText(QStringLiteral("思考   ▸"));
    btn->setStyleSheet(QStringLiteral(
        "QToolButton { background-color:#1B1E24; border:1px solid #2C2F35;"
        "  border-radius:6px; padding:4px 10px; color:#adb2b8; text-align:left; }"
        "QToolButton:checked { border-color:#3A3F47; background-color:#20242B; color:#f9fafb; }"
        "QToolButton:hover { background-color:#20242B; }"));

    auto *area = new QLabel(text);
    area->setWordWrap(true);
    area->setTextInteractionFlags(Qt::TextSelectableByMouse);
    area->setTextFormat(Qt::PlainText);
    area->setStyleSheet(QStringLiteral(
        "background-color:#16181C; border:1px solid #26292F; border-radius:6px;"
        " padding:6px 8px; color:#adb2b8; font-size:12px;"));
    area->hide();

    connect(btn, &QToolButton::toggled, this, [btn, area](bool on) {
        btn->setText(on ? QStringLiteral("思考   ▾") : QStringLiteral("思考   ▸"));
        area->setVisible(on);
    });

    vl->addWidget(btn);
    vl->addWidget(area);
    m_messageLayout->addWidget(container);
    scrollMessageToBottom();
}

// ---------------------------------------------------------------- 连接 / 树

void CDSHChatWindow::onConnectClicked()
{
    if (m_commander->isConnected()) {
        m_commander->disconnectFromServer();
        return;
    }
    QString host = QStringLiteral("127.0.0.1");
    quint16 port = 8888;
    QString tokenUrl;
    const QString text = m_serverEdit->text().trimmed();
    // 地址栏可能直接录入带 token 的完整网址(含 ://): 解析 host/port 并存下登录网址。
    if (text.contains(QLatin1String("://"))) {
        const QUrl u(text);
        host = u.host();
        port = u.port(8888);
        if (u.hasQuery() && u.query().contains(QLatin1String("token=")))
            tokenUrl = text; // 带 token → 登录时先交换 cookie
    } else {
        const QStringList parts = text.split(QLatin1Char(':'));
        if (!parts.isEmpty() && !parts[0].isEmpty())
            host = parts[0].trimmed();
        if (parts.size() > 1) {
            bool okNum = false;
            const int p = parts[1].trimmed().toInt(&okNum);
            if (okNum && p > 0 && p <= 65535)
                port = quint16(p);
        }
    }
    m_commander->setServer(host, port);
    m_tokenUrl = tokenUrl;
    if (!tokenUrl.isEmpty())
        m_commander->setLoginUrl(tokenUrl);
    // 连接时按网址更新窗口标题(客户端: 网址)
    setWindowTitle(QStringLiteral("客户端: %1").arg(tokenUrl.isEmpty()
        ? QStringLiteral("%1:%2").arg(host).arg(port) : tokenUrl));
    m_commander->connectToServer();
}

void CDSHChatWindow::onConnectionChanged(bool connected, const QString &detail)
{
    m_connected = connected;
    setStatus(detail, connected ? QStringLiteral("#4ADE80") : QStringLiteral("#F87171"));
    m_connectBtn->setText(connected ? QStringLiteral("断开") : QStringLiteral("连接"));
    updateUiEnabled();
    // token 登录成功后, 把地址栏/标题从带 token 网址切回正常 host:port
    if (connected && !m_tokenUrl.isEmpty()) {
        QString clean = m_commander->serverUrl();
        if (clean.startsWith(QLatin1String("http://")))
            clean = clean.mid(7);
        m_serverEdit->setText(clean);
        setWindowTitle(QStringLiteral("客户端: %1").arg(clean));
    }
    if (connected) {
        appendSystemLine(QStringLiteral("已连接 DSH 服务"));
        loadPresets(); // 拉取 Agent 预设列表(新建会话下拉/名称旁显示)
        loadTree();
    }
}

void CDSHChatWindow::onNewSessionClicked()
{
    // 若当前选中工作区(或选中其下会话),则把新会话建到该工作区
    QString workspaceId;
    QTreeWidgetItem *cur = m_sessionTree->currentItem();
    if (cur) {
        const QVariantMap data = cur->data(0, Qt::UserRole).toMap();
        if (data.value(QLatin1String(kNodeKind)).toString() == QStringLiteral("workspace")) {
            workspaceId = data.value(QLatin1String(kNodeId)).toString();
        } else if (cur->parent()) {
            workspaceId = cur->parent()->data(0, Qt::UserRole).toMap()
                              .value(QLatin1String(kNodeId)).toString();
        }
    }
    m_createRpcId = m_commander->createSession(workspaceId);
    setStatus(QStringLiteral("新建会话..."), QStringLiteral("#9AA0A8"));
}

void CDSHChatWindow::onAddWorkspaceClicked()
{
    // 弹窗:工作区名称 + 目录,均必填
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("新增工作区"));
    dlg.setModal(true);
    applyDarkStyle(&dlg); // 新增工作区对话框背景改为黑色
    auto *form = new QFormLayout(&dlg);

    auto *nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText(QStringLiteral("工作区名称(必填)"));
    auto *dirEdit = new QLineEdit(&dlg);
    dirEdit->setPlaceholderText(QStringLiteral("工作区目录(必填,需已存在)"));

    // 目录行:输入框 + 浏览按钮
    auto *dirRow = new QHBoxLayout;
    dirRow->addWidget(dirEdit, 1);
    auto *browseBtn = new QPushButton(QStringLiteral("..."), &dlg);
    QObject::connect(browseBtn, &QPushButton::clicked, &dlg, [&dlg, dirEdit]() {
        const QString path = QFileDialog::getExistingDirectory(&dlg, QStringLiteral("选择工作区目录"));
        if (!path.isEmpty())
            dirEdit->setText(path);
    });
    dirRow->addWidget(browseBtn);

    form->addRow(QStringLiteral("名称:"), nameEdit);
    form->addRow(QStringLiteral("目录:"), dirRow);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(btnBox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btnBox);

    dlg.setMinimumWidth(360);

    if (dlg.exec() != QDialog::Accepted)
        return; // 取消:不执行任何操作

    const QString name = nameEdit->text().trimmed();
    const QString dir = dirEdit->text().trimmed();
    if (name.isEmpty() || dir.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("新增工作区"),
                             QStringLiteral("工作区名称和目录都不能为空"));
        return;
    }

    // 按 DSH 规则采纳目录;标题默认取目录名,若与用户填的名称不同再 rename
    m_pendingWsName = name;
    m_pendingWsId.clear();
    m_createWsRpcId = m_commander->createWorkspace(dir);
    setStatus(QStringLiteral("新增工作区..."), QStringLiteral("#9AA0A8"));
}

void CDSHChatWindow::onRefreshSessionsClicked()
{
    loadTree();
}

void CDSHChatWindow::onTreeContextMenu(const QPoint &pos)
{
    QTreeWidgetItem *item = m_sessionTree->itemAt(pos);
    if (!item)
        return;
    const QVariantMap data = item->data(0, Qt::UserRole).toMap();
    const QString kind = data.value(QLatin1String(kNodeKind)).toString();
    const QString id = data.value(QLatin1String(kNodeId)).toString();
    if (kind.isEmpty() || id.isEmpty())
        return;

    // 弹出菜单背景改为黑色
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background-color: #1F2229; color: #E6E8EB; border: 1px solid #3A3F47; }"
        "QMenu::item { padding: 6px 24px 6px 12px; }"
        "QMenu::item:selected { background-color: #2563EB; color: #FFFFFF; }"));

    QAction *chosen = nullptr;

    if (kind == QStringLiteral("workspace")) {
        // 工作区:修改名称 / 删除(确认) / 新增会话
        QAction *renameAct = menu.addAction(QStringLiteral("修改名称"));
        QAction *delAct = menu.addAction(QStringLiteral("删除"));
        QAction *newSessionAct = menu.addAction(QStringLiteral("新增会话"));
        chosen = menu.exec(m_sessionTree->viewport()->mapToGlobal(pos));
        if (chosen == renameAct) {
            const QString name = promptForName(QStringLiteral("修改工作区名称"), QString());
            if (!name.isEmpty()) {
                m_pendingWsId = id;
                m_renameWsRpcId = m_commander->renameWorkspace(id, name);
                setStatus(QStringLiteral("修改名称..."), QStringLiteral("#9AA0A8"));
            }
        } else if (chosen == delAct) {
            const auto btn = QMessageBox::question(this, QStringLiteral("删除工作区"),
                QStringLiteral("确定删除该工作区吗?其下的会话不会被删除。"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (btn == QMessageBox::Yes) {
                m_deleteWsRpcId = m_commander->deleteWorkspace(id);
                setStatus(QStringLiteral("删除工作区..."), QStringLiteral("#9AA0A8"));
            }
        } else if (chosen == newSessionAct) {
            createSessionInWorkspace(id); // 在选中的工作区下新建会话(弹窗输名称)
        }
    } else if (kind == QStringLiteral("session")) {
        // 会话:修改名称 / 新增当前工作区(上级目录)下的会话 / 归档会话
        QAction *renameAct = menu.addAction(QStringLiteral("修改名称"));
        QAction *newSessionAct = menu.addAction(QStringLiteral("新增当前工作区下的会话"));
        QAction *archiveAct = menu.addAction(QStringLiteral("归档会话"));
        chosen = menu.exec(m_sessionTree->viewport()->mapToGlobal(pos));
        if (chosen == renameAct) {
            const QString name = promptForName(QStringLiteral("修改会话名称"), m_sessionTitles.value(id));
            if (!name.isEmpty()) {
                m_renameSessionRpcId = m_commander->renameSession(id, name);
                setStatus(QStringLiteral("修改名称..."), QStringLiteral("#9AA0A8"));
            }
        } else if (chosen == newSessionAct) {
            QString wsId;
            if (item->parent())
                wsId = item->parent()->data(0, Qt::UserRole).toMap()
                           .value(QLatin1String(kNodeId)).toString();
            if (!wsId.isEmpty())
                createSessionInWorkspace(wsId); // 在会话的上级工作区下新建会话(弹窗输名称)
        } else if (chosen == archiveAct) {
            m_archiveSession = id; // 记录当前归档的会话,便于命令返回后立即本地过滤
            m_archiveRpcId = m_commander->archiveSession(id);
            setStatus(QStringLiteral("归档中..."), QStringLiteral("#9AA0A8"));
        }
    }
}

void CDSHChatWindow::createSessionInWorkspace(const QString &workspaceId)
{
    // 弹窗:会话名称(必输)+ Agent 预设(下拉),背景黑色
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("新建会话"));
    dlg.setModal(true);
    applyDarkStyle(&dlg);
    auto *form = new QFormLayout(&dlg);

    auto *nameEdit = new QLineEdit(&dlg);
    nameEdit->setPlaceholderText(QStringLiteral("会话名称(必填)"));
    auto *presetCombo = new QComboBox(&dlg);
    if (m_presets.isEmpty()) {
        // 无预设时兜底
        presetCombo->addItem(QStringLiteral("默认"), m_defaultPresetId);
    } else {
        // 直接列出所有预设(如 标准模式/PTC 模式/极简模式/创造模式),
        // 默认选中 DSH 默认预设(如"标准模式"),不再出现"默认"字面项
        int defaultIdx = 0;
        for (const auto &p : m_presets) {
            presetCombo->addItem(p.second.isEmpty() ? p.first : p.second, p.first);
            if (!m_defaultPresetId.isEmpty() && p.first == m_defaultPresetId)
                defaultIdx = presetCombo->count() - 1;
        }
        presetCombo->setCurrentIndex(defaultIdx);
    }

    form->addRow(QStringLiteral("会话名称:"), nameEdit);
    form->addRow(QStringLiteral("Agent预设:"), presetCombo);

    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(btnBox, &QDialogButtonBox::accepted, &dlg, [&dlg, nameEdit]() {
        if (nameEdit->text().trimmed().isEmpty())
            QMessageBox::warning(&dlg, QStringLiteral("提示"), QStringLiteral("会话名称不能为空"));
        else
            dlg.accept();
    });
    QObject::connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btnBox);
    dlg.setMinimumWidth(360);

    if (dlg.exec() != QDialog::Accepted)
        return; // 取消:不做任何操作

    const QString name = nameEdit->text().trimmed();
    const QString preset = presetCombo->currentData().toString();
    m_pendingSessionName = name;
    m_pendingOpenSession.clear();
    m_createRpcId = m_commander->createSession(workspaceId, QString(), preset); // workspaceId + agentPreset
    setStatus(QStringLiteral("新建会话..."), QStringLiteral("#9AA0A8"));
}

void CDSHChatWindow::loadTree()
{
    // 工作区改由 workspace.follow 流下发(连接后持续推送), 这里只刷新会话列表;
    // 两者就绪后由 maybeFinishTreeLoad() 重建树。
    m_sessionsLoaded = false;
    m_sessRpcId = m_commander->listSessions();
    setStatus(QStringLiteral("刷新会话..."), QStringLiteral("#9AA0A8"));
}

// 工作区基线(session.list)与元数据均已就绪时才重建树(避免半成品)
void CDSHChatWindow::maybeFinishTreeLoad()
{
    if (!m_wsLoaded || !m_sessionsLoaded)
        return;
    finishTreeLoad();
}

void CDSHChatWindow::finishTreeLoad()
{
    // 串行两阶段加载全部完成后的重建(不再用计数)
    rebuildTree();
    queueTitleFetch(); // 树建好后,为无名称的会话批量补名称
    // 会话列表就绪后,若当前会话是当前打开的会话,同步其子会话运行状态与标题
    if (!m_currentSessionId.isEmpty()) {
        refreshSubagentState();
        updateSessionTitle();
    }
    QSet<QString> merged = m_archivedSessionIds;
    merged.unite(m_archivedLocal);
    setStatus(QStringLiteral("%1 个工作区 · 已归档 %2")
                  .arg(m_wsItems.size()).arg(merged.size()),
              QStringLiteral("#9AA0A8"));

    // 新建会话成功后,树刷新完再打开新会话
    if (!m_pendingOpenSession.isEmpty()) {
        const QString sid = m_pendingOpenSession;
        m_pendingOpenSession.clear();
        selectSessionInTree(sid);
    }
}

// workspace.follow 流帧:基线/增量, 维护 m_wsItems、m_archivedSessionIds 并刷新树。
void CDSHChatWindow::onWorkspaceFrame(const QJsonObject &frame)
{
    const QString type = frame.value(QLatin1String("type")).toString();
    if (type == QLatin1String("baseline")) {
        const QJsonObject value = frame.value(QLatin1String("value")).toObject();
        m_wsItems = value.value(QLatin1String("items")).toArray();
        m_archivedSessionIds.clear();
        const QJsonArray archived = value.value(QLatin1String("archivedSessionIds")).toArray();
        bool changed = false;
        for (const QJsonValue &v : archived) {
            m_archivedSessionIds.insert(v.toString());
            if (!m_archivedLocal.contains(v.toString())) {
                m_archivedLocal.insert(v.toString());
                changed = true;
            }
        }
        if (changed)
            saveArchivedCache();
        m_wsLoaded = true;
        maybeFinishTreeLoad();
        return;
    }
    if (type == QLatin1String("upsert")) {
        const QJsonObject ws = frame.value(QLatin1String("workspace")).toObject();
        const QString wsId = ws.value(QLatin1String("workspaceId")).toString();
        int found = -1;
        for (int i = 0; i < m_wsItems.size(); ++i) {
            if (m_wsItems.at(i).toObject().value(QLatin1String("workspaceId")).toString() == wsId) {
                found = i;
                break;
            }
        }
        if (found >= 0)
            m_wsItems.replace(found, ws);
        else
            m_wsItems.append(ws);
    } else if (type == QLatin1String("remove")) {
        const QString wsId = frame.value(QLatin1String("workspaceId")).toString();
        for (int i = 0; i < m_wsItems.size(); ++i) {
            if (m_wsItems.at(i).toObject().value(QLatin1String("workspaceId")).toString() == wsId) {
                m_wsItems.removeAt(i);
                break;
            }
        }
    } else if (type == QLatin1String("order")) {
        // 按 workspaceIds 顺序重排 m_wsItems
        const QJsonArray ids = frame.value(QLatin1String("workspaceIds")).toArray();
        QJsonArray ordered;
        for (const QJsonValue &idv : ids) {
            for (const QJsonValue &wv : m_wsItems) {
                if (wv.toObject().value(QLatin1String("workspaceId")).toString() == idv.toString()) {
                    ordered.append(wv);
                    break;
                }
            }
        }
        m_wsItems = ordered;
    } else if (type == QLatin1String("archived")) {
        m_archivedSessionIds.clear();
        const QJsonArray archived = frame.value(QLatin1String("archivedSessionIds")).toArray();
        for (const QJsonValue &v : archived)
            m_archivedSessionIds.insert(v.toString());
    } else {
        return;
    }
    // 数据已加载时,增量只刷新视图
    if (m_wsLoaded && m_sessionsLoaded)
        rebuildTree();
}

void CDSHChatWindow::rebuildTree()
{
    m_sessionTree->setUpdatesEnabled(false);
    m_sessionTree->clear();

    for (const QJsonValue &wv : m_wsItems) {
        const QJsonObject ws = wv.toObject();
        const QString wsId = ws.value(QLatin1String("workspaceId")).toString();
        const QString title = ws.value(QLatin1String("title")).toString();
        const QString path = ws.value(QLatin1String("path")).toString();

        auto *wsItem = new QTreeWidgetItem(m_sessionTree);
        wsItem->setText(0, QStringLiteral("📁 %1").arg(title.isEmpty() ? wsId.left(8) : title));
        wsItem->setToolTip(0, QStringLiteral("目录:%1").arg(path));
        QVariantMap wsData;
        wsData.insert(QLatin1String(kNodeKind), QStringLiteral("workspace"));
        wsData.insert(QLatin1String(kNodeId), wsId);
        wsItem->setData(0, Qt::UserRole, wsData);

        // 会话按最新 updatedAt 降序排列(与 DSH 一致:最新会话在上)
        QList<std::pair<QString, qint64>> sessionList;
        const QJsonArray sessionIds = ws.value(QLatin1String("sessionIds")).toArray();
        for (const QJsonValue &sv : sessionIds) {
            const QString sid = sv.toString();
            if (isArchived(sid))
                continue; // 归档会话不显示(服务端 archivedSessionIds + 本地兜底)
            const QJsonObject summary = m_sessionMap.value(sid).toObject();
            // 与 DSH 前端 sessionVisible 一致:
            //   子会话(origin=subagent)、空白会话(blank=true,除非当前或待打开)不显示
            if (summary.value(QLatin1String("origin")).toString() == QStringLiteral("subagent"))
                continue;
            if (summary.value(QLatin1String("blank")).toBool()
                && sid != m_currentSessionId && sid != m_pendingOpenSession)
                continue;
            const qint64 updated = qint64(summary.value(QLatin1String("updatedAt")).toDouble());
            sessionList.append(std::make_pair(sid, updated));
        }
        std::sort(sessionList.begin(), sessionList.end(),
                  [](const std::pair<QString, qint64> &a, const std::pair<QString, qint64> &b) {
                      return a.second > b.second;
                  });

        for (const auto &entry : sessionList) {
            const QString sid = entry.first;
            const QJsonObject summary = m_sessionMap.value(sid).toObject();
            const bool running = summary.value(QLatin1String("running")).toBool();
            const QString cwd = summary.value(QLatin1String("cwd")).toString();
            // 显示会话名称(标题);无标题时用项目目录 basename(与 DSH displayTitleOf 一致), 再短 id
            QString text = m_sessionTitles.value(sid);
            if (text.isEmpty()) {
                if (!cwd.isEmpty()) {
                    const QString base = QFileInfo(cwd).fileName();
                    if (!base.isEmpty()) text = base;
                }
                if (text.isEmpty())
                    text = shortSessionId(sid);
            }

            auto *sItem = new QTreeWidgetItem(wsItem);
            sItem->setText(0, QStringLiteral("%1 %2")
                .arg(running ? QStringLiteral("●") : QStringLiteral("○")).arg(text));
            sItem->setText(1, relativeTime(entry.second)); // 相对时间列
            if (!cwd.isEmpty())
                sItem->setToolTip(0, QStringLiteral("目录:%1").arg(cwd));
            QVariantMap sData;
            sData.insert(QLatin1String(kNodeKind), QStringLiteral("session"));
            sData.insert(QLatin1String(kNodeId), sid);
            sItem->setData(0, Qt::UserRole, sData);
        }
        wsItem->setExpanded(true);
    }

    // 重建后恢复当前会话的选中状态(不重复加载:onTreeItemChanged 有短路)
    if (!m_currentSessionId.isEmpty())
        selectSessionInTree(m_currentSessionId);

    m_sessionTree->setUpdatesEnabled(true);
}

void CDSHChatWindow::selectSessionInTree(const QString &sessionId)
{
    for (int w = 0; w < m_sessionTree->topLevelItemCount(); ++w) {
        QTreeWidgetItem *wsItem = m_sessionTree->topLevelItem(w);
        for (int s = 0; s < wsItem->childCount(); ++s) {
            QTreeWidgetItem *sItem = wsItem->child(s);
            if (sItem->data(0, Qt::UserRole).toMap().value(QLatin1String(kNodeId)).toString() == sessionId) {
                wsItem->setExpanded(true);
                m_sessionTree->setCurrentItem(sItem);
                return;
            }
        }
    }
}

void CDSHChatWindow::onTreeItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous)
{
    Q_UNUSED(previous);
    if (!current)
        return;
    const QVariantMap data = current->data(0, Qt::UserRole).toMap();
    if (data.value(QLatin1String(kNodeKind)).toString() != QStringLiteral("session"))
        return; // 工作区节点仅用于分组/展开
    const QString sid = data.value(QLatin1String(kNodeId)).toString();
    if (sid.isEmpty())
        return;
    openSession(sid); // 即使单击同一个会话,也重新加载/更新消息
}

void CDSHChatWindow::openSession(const QString &sessionId)
{
    if (sessionId.isEmpty())
        return;
    m_currentSessionId = sessionId;
    clearMessageArea();
    if (m_questionList)
        m_questionList->clear(); // 重新加载历史前清空索引(基于消息框内容重建)
    m_modelCombo->clear(); // 等 models 返回后重新填充

    const QString title = m_sessionTitles.value(sessionId,
        QStringLiteral("会话 ") + sessionId.left(8));
    m_baseSessionTitle = title;      // 保存纯标题(不含“/子会话”后缀)
    m_subagentRunning = false;       // 换会话先复位子会话标记
    m_titleLabel->setText(title);
    updatePresetLabel(); // 显示该会话的 Agent 预设(只读)
    updatePermissionCombo(); // 显示该会话的权限(默认值)
    updateUiEnabled();
    refreshSubagentState(); // 根据 summary 判断是否有运行中的子会话, 可能有则标题加后缀
    updateSessionTitle();   // 用最终结果刷新顶部标题

    appendSystemLine(QStringLiteral("── 会话 %1 ──").arg(sessionId.left(8)));
    m_modelsRpcId = m_commander->sessionModels(sessionId);
    // 现代 DSH: 历史 + 实时事件走 session.follow 流(快照=历史)
    m_commander->followSession(sessionId);
}

// ---------------------------------------------------------------- 发送

void CDSHChatWindow::onSendClicked()
{
    // 会话运行中:按钮为“停止”,点击即取消(与官方客户端一致)
    if (m_sessionRunning.value(m_currentSessionId, false)) {
        m_cancelRpcId = m_commander->cancelSession(m_currentSessionId);
        // 本地先置为非运行并回落按钮为“发送”; 服务端最终状态由 session-status 事件驱动,
        // 若仍在运行会再次把按钮切回“停止”。
        m_sessionRunning.insert(m_currentSessionId, false);
        updateSendButton();
        return;
    }

    if (!m_connected || m_currentSessionId.isEmpty())
        return;
    const QString text = m_inputEdit->toPlainText().trimmed();
    if (text.isEmpty() && m_attachments.isEmpty())
        return;
    m_inputEdit->clear();

    // 本地回显:文本 + 附件文件名标记
    QString echo = text;
    for (const DSHImageAttachment &a : qAsConst(m_attachments))
        echo += (echo.isEmpty() ? QString() : QStringLiteral("\n"))
                + QStringLiteral("[图片附件: %1]").arg(a.name.isEmpty() ? a.mediaType : a.name);
    appendUserMessage(echo);

    if (m_attachments.isEmpty()) {
        m_promptRpcId = m_commander->prompt(m_currentSessionId, text);
    } else {
        m_promptRpcId = m_commander->promptWithImages(m_currentSessionId, text, m_attachments);
        clearAttachments();
    }
    // 发送后进入运行态,发送按钮切换为“停止”(真正复位由 session-status 事件驱动)
    m_sessionRunning.insert(m_currentSessionId, true);
    updateSendButton();
}

// 依据会话运行状态切换主按钮: 运行中显示“停止”可取消, 否则显示“发送”
void CDSHChatWindow::updateSendButton()
{
    if (!m_sendBtn)
        return;
    const bool running = m_sessionRunning.value(m_currentSessionId, false);
    m_sendBtn->setText(running ? QStringLiteral("停止") : QStringLiteral("发送"));
    m_sendBtn->setToolTip(running ? QStringLiteral("停止当前会话") : QStringLiteral("发送消息"));
}

// 顶部会话标题: 当前会话有运行中的子会话时显示 “会话名称 / 子会话”, 否则显示纯会话名称
void CDSHChatWindow::updateSessionTitle()
{
    if (!m_titleLabel)
        return;
    const QString base = m_baseSessionTitle.isEmpty()
        ? m_sessionTitles.value(m_currentSessionId,
            QStringLiteral("会话 ") + m_currentSessionId.left(8))
        : m_baseSessionTitle;
    m_titleLabel->setText(m_subagentRunning
        ? QStringLiteral("%1 / 子会话").arg(base)
        : base);
}

// 根据 session.list 的 summary 判断当前会话是否有“正在运行”的子会话:
//   子会话 = origin==subagent 且 parentId(或 parentSessionId)==当前会话 且 running==true
// running 优先取实时状态 m_sessionRunning(host/session-status 会推送), 否则用 summary 里的值。
// 结果刷新 m_subagentRunning 并更新顶部标题。
void CDSHChatWindow::refreshSubagentState()
{
    bool running = false;
    // 遍历所有会话 summary, 找以当前会话为父且 origin==subagent 的子会话
    for (auto it = m_sessionMap.constBegin(); it != m_sessionMap.constEnd(); ++it) {
        const QString childId = it.key();
        const QJsonObject summary = it.value().toObject();
        if (summary.value(QLatin1String("origin")).toString() != QStringLiteral("subagent"))
            continue;
        const QString parentId = summary.value(QLatin1String("parentId")).toString();
        const QString parentSessionId = summary.value(QLatin1String("parentSessionId")).toString();
        if (parentId != m_currentSessionId && parentSessionId != m_currentSessionId)
            continue;
        // 优先用实时 running(事件推送), 否则回退 summary 里的运行标志
        const bool childRunning = m_sessionRunning.value(childId,
            summary.value(QLatin1String("running")).toBool());
        if (childRunning) {
            running = true;
            break;
        }
    }
    if (m_subagentRunning != running) {
        m_subagentRunning = running;
        updateSessionTitle();
    }
}

// 拖入文件: 仅支持图片(png/jpeg/webp/gif), 按 DSH 附件协议读出 base64 加入待发送列表
void CDSHChatWindow::onDropFile(const QString &localPath)
{
    QString mediaType;
    const QString lower = localPath.toLower();
    if (lower.endsWith(QLatin1String(".png")))       mediaType = QStringLiteral("image/png");
    else if (lower.endsWith(QLatin1String(".jpg")) ||
             lower.endsWith(QLatin1String(".jpeg"))) mediaType = QStringLiteral("image/jpeg");
    else if (lower.endsWith(QLatin1String(".webp"))) mediaType = QStringLiteral("image/webp");
    else if (lower.endsWith(QLatin1String(".gif")))  mediaType = QStringLiteral("image/gif");
    else {
        // 非图片类型: 提示拖入的文件类型错误
        showDropWarning(QStringLiteral("拖入的文件错误:\n仅支持图片附件(png/jpeg/webp/gif)。"));
        return;
    }

    QFile f(localPath);
    if (!f.open(QIODevice::ReadOnly)) {
        showDropWarning(QStringLiteral("无法读取文件:%1").arg(localPath));
        return;
    }
    const QByteArray data = f.readAll();
    f.close();
    if (data.isEmpty()) {
        showDropWarning(QStringLiteral("文件为空:%1").arg(localPath));
        return;
    }
    // 基础上限保护(具体限制以 DSH 部署的 imageLimits 为准, 宿主会二次校验)
    if (data.size() > 20 * 1024 * 1024) {
        showDropWarning(QStringLiteral("图片过大(>20MiB),无法作为附件:%1").arg(localPath));
        return;
    }

    DSHImageAttachment att;
    att.mediaType = mediaType;
    att.dataBase64 = QString::fromLatin1(data.toBase64()); // 标准 base64(含 '=')
    att.name = QFileInfo(localPath).fileName();
    m_attachments.append(att);
    addAttachmentChip(att.name, m_attachments.size() - 1);
}

void CDSHChatWindow::addAttachmentChip(const QString &name, int index)
{
    if (!m_attachBar || !m_attachLayout)
        return;

    // chip 容器: 文件名 + 删除按钮
    auto *chip = new QWidget(m_attachBar);
    chip->setStyleSheet(QStringLiteral(
        "QWidget { background-color:#1B1E24; border:1px solid #2C2F35; border-radius:6px; }"));
    auto *chipLayout = new QHBoxLayout(chip);
    chipLayout->setContentsMargins(6, 2, 4, 2);
    chipLayout->setSpacing(4);

    auto *iconName = new QLabel(QStringLiteral("📎 %1").arg(name), chip);
    iconName->setStyleSheet(QStringLiteral(
        "QLabel { background:transparent; border:none; color:#cfd3d6; font-size:12px; }"));
    chipLayout->addWidget(iconName);

    auto *delBtn = new QToolButton(chip);
    delBtn->setText(QStringLiteral("✕"));
    delBtn->setCursor(Qt::PointingHandCursor);
    delBtn->setToolTip(QStringLiteral("移除附件"));
    delBtn->setFixedSize(18, 18);
    delBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background:transparent; border:none; color:#9AA0A8; font-size:12px; font-weight:bold; }"
        "QToolButton:hover { color:#F87171; }"));
    delBtn->setAutoRaise(true);
    connect(delBtn, &QToolButton::clicked, this, [this, index]() { removeAttachmentAt(index); });
    chipLayout->addWidget(delBtn);

    m_attachLayout->addWidget(chip);
    m_attachBar->show();
}

void CDSHChatWindow::rebuildAttachmentChips()
{
    if (!m_attachLayout)
        return;
    // 清空现有 chip 行
    while (QLayoutItem *item = m_attachLayout->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    if (m_attachments.isEmpty()) {
        if (m_attachBar)
            m_attachBar->hide();
        return;
    }
    for (int i = 0; i < m_attachments.size(); ++i)
        addAttachmentChip(m_attachments.at(i).name, i);
    if (m_attachBar)
        m_attachBar->show();
}

void CDSHChatWindow::removeAttachmentAt(int index)
{
    if (index < 0 || index >= m_attachments.size())
        return;
    m_attachments.removeAt(index);
    rebuildAttachmentChips(); // 重建后序号与数据保持一致, 不会错乱
}

void CDSHChatWindow::clearAttachments()
{
    m_attachments.clear();
    rebuildAttachmentChips();
}

// 把消息区内容导出为 PDF 文件
void CDSHChatWindow::onExportPdf()
{
    // 默认文件名:工作区名称_会话名称
    // 工作区名称 = 当前会话归属的 workspace.title(遍历 workspace.list 缓存)
    QString wsName;
    for (const QJsonValue &wv : m_wsItems) {
        const QJsonObject ws = wv.toObject();
        const QJsonArray sids = ws.value(QLatin1String("sessionIds")).toArray();
        for (const QJsonValue &sv : sids) {
            if (sv.toString() == m_currentSessionId) {
                wsName = ws.value(QLatin1String("title")).toString();
                break;
            }
        }
        if (!wsName.isEmpty())
            break;
    }
    // 会话名称:优先会话标题缓存,再标题栏
    QString sessionName = m_sessionTitles.value(m_currentSessionId);
    if (sessionName.isEmpty())
        sessionName = m_titleLabel->text().trimmed();

    QString name;
    if (!wsName.isEmpty() && !sessionName.isEmpty())
        name = wsName + QStringLiteral("_") + sessionName;
    else if (!wsName.isEmpty())
        name = wsName;
    else if (!sessionName.isEmpty())
        name = sessionName;
    else
        name = QStringLiteral("dsh_chat");
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    const QString path = QFileDialog::getSaveFileName(this,
        QStringLiteral("导出为 PDF"), name + QStringLiteral(".pdf"),
        QStringLiteral("PDF 文件 (*.pdf)"));
    if (path.isEmpty())
        return;

    QString html = QStringLiteral(
        "<style>body{font-family:'Segoe UI','Microsoft YaHei';font-size:12px;color:#111;}"
        "div{margin:4px 0;line-height:1.6;}</style>"
        "<h2>DSH 对话记录</h2>");
    for (int i = 0; i < m_messageLayout->count(); ++i) {
        QLayoutItem *item = m_messageLayout->itemAt(i);
        if (!item || !item->widget())
            continue;
        QWidget *w = item->widget();
        const bool isUser = m_userBubbles.contains(w); // 用户消息气泡(绿色)
        const QString bubbleBg = isUser
            ? QStringLiteral("background-color:#1F6F43;color:#fff;")
            : QStringLiteral("background-color:#F2F3F5;color:#111;");
        const QList<QLabel *> labs = w->findChildren<QLabel *>();
        for (QLabel *l : labs) {
            if (!l->text().isEmpty())
                html += QStringLiteral("<div style=\"%1padding:6px 10px;border-radius:6px;\">%2</div>")
                            .arg(bubbleBg, l->text());
        }
        const QList<QToolButton *> btns = w->findChildren<QToolButton *>();
        for (QToolButton *b : btns)
            html += QStringLiteral("<div><b>%1</b></div>").arg(b->text().toHtmlEscaped());
    }

    QTextDocument doc;
    doc.setHtml(html);
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(96);
    doc.setPageSize(QSizeF(QPageSize(QPageSize::A4).size(QPageSize::Point)));
    doc.setDocumentMargin(20);
    doc.print(&writer);
    appendSystemLine(QStringLiteral("已导出 PDF:%1").arg(path));
}

// 输入框高度自适应:随内容增长,最大为窗口一半;清空后回归默认
void CDSHChatWindow::onInputTextChanged()
{
    const int defaultH = 88;
    int maxH = height() / 2;
    if (maxH < defaultH)
        maxH = defaultH;
    const QFontMetrics fm(m_inputEdit->font());
    // 行数 = 段落数(换行)+ 长行按内容宽度的折行估算
    const int lineSpacing = fm.lineSpacing();
    const int contentW = m_inputEdit->viewport()->width() - 8;
    int lineN = 0;
    const QStringList blocks = m_inputEdit->toPlainText().split(QLatin1Char('\n'));
    for (const QString &line : blocks) {
        const int w = fm.horizontalAdvance(line);
        int n = 1;
        if (contentW > 10)
            n += w / contentW; // 超过一行的折行数
        lineN += n;
    }
    int h = lineSpacing * lineN + 16;
    if (h < defaultH)
        h = defaultH;
    if (h > maxH)
        h = maxH;
    m_inputEdit->setFixedHeight(h);
}

void CDSHChatWindow::onModelChanged(int index)
{
    if (index < 0 || m_currentSessionId.isEmpty())
        return;
    const QVariantMap data = m_modelCombo->itemData(index).toMap();
    if (data.isEmpty())
        return;
    m_selectRpcId = m_commander->selectModel(m_currentSessionId,
        data.value(QStringLiteral("provider")).toString(),
        data.value(QStringLiteral("model")).toString());
}

// ---------------------------------------------------------------- 命令结果

void CDSHChatWindow::onCommandFinished(const QString &rpcId, bool ok, const QJsonObject &result)
{
    // ---- Agent 预设列表 ----
    if (rpcId == m_presetsRpcId) {
        m_presetsRpcId.clear();
        m_presets.clear();
        if (ok) {
            m_defaultPresetId.clear();
            const QJsonArray presets = result.value(QLatin1String("presets")).toArray();
            for (const QJsonValue &v : presets) {
                const QJsonObject p = v.toObject();
                const QString id = p.value(QLatin1String("id")).toString();
                const QString name = p.value(QLatin1String("name")).toString();
                if (!id.isEmpty()) {
                    m_presets.append(qMakePair(id, name));
                    if (p.value(QLatin1String("isDefault")).toBool())
                        m_defaultPresetId = id; // 记录 DSH 默认预设
                }
            }
        }
        return;
    }

    // ---- 树加载:session.list ----(工作区由 workspace.follow 流单独下发)
    if (rpcId == m_sessRpcId) {
        m_sessRpcId.clear();
        m_sessionMap = QJsonObject();
        if (ok) {
            const QJsonArray items = result.value(QLatin1String("items")).toArray();
            for (const QJsonValue &v : items) {
                const QJsonObject item = v.toObject();
                const QString sid = item.value(QLatin1String("sessionId")).toString();
                m_sessionMap.insert(sid, item);
                // 会话名称来自 session.list 的 projections.values["title"]
                // (由 dsh-session-title 的 title 投影对 session/title 事件折叠而来,
                //  与 DSH 前端一致,冷启动即可显示名称)
                const QJsonObject values = item.value(QLatin1String("projections")).toObject()
                                               .value(QLatin1String("values")).toObject();
                const QString title = values.value(QLatin1String("title")).toString();
                if (!title.isEmpty() && !m_sessionTitles.contains(sid)) {
                    m_sessionTitles.insert(sid, title);
                    m_sessionTitlesChanged = true;
                }
            }
            if (m_sessionTitlesChanged) {
                m_sessionTitlesChanged = false;
                saveTitleCache(); // 记下这些名称,之后冷启动也能显示
            }
        } else {
            appendSystemLine(QStringLiteral("获取会话失败:%1").arg(result.value(QLatin1String("message")).toString()));
        }
        m_sessionsLoaded = true;
        maybeFinishTreeLoad();
        return;
    }

    // ---- 新建会话 ----
    if (rpcId == m_createRpcId) {
        m_createRpcId.clear();
        if (!ok) {
            appendSystemLine(QStringLiteral("新建会话失败:%1").arg(result.value(QLatin1String("message")).toString()));
            m_pendingSessionName.clear();
            return;
        }
        const QString sid = result.value(QLatin1String("sessionId")).toString();
        m_sessionRunning.insert(sid, false);
        m_pendingOpenSession = sid; // 刷新树后打开
        if (!m_pendingSessionName.isEmpty()) {
            // 新建会话后立即设置用户填写的名称
            m_renameSessionRpcId = m_commander->renameSession(sid, m_pendingSessionName);
            setStatus(QStringLiteral("设置会话名称..."), QStringLiteral("#9AA0A8"));
        } else {
            m_pendingSessionName.clear();
            loadTree();
        }
        return;
    }

    // ---- 模型列表 ----
    if (rpcId == m_modelsRpcId) {
        m_modelsRpcId.clear();
        if (!ok) {
            appendSystemLine(QStringLiteral("获取模型列表失败:%1").arg(result.value(QLatin1String("message")).toString()));
            return;
        }
        // 现代 DSH: 模型目录字段为 default(而非 current), groups 为 {id, models:[{id}]}
        const QJsonObject current = result.value(QLatin1String("default")).toObject();
        const QString curProvider = current.value(QLatin1String("provider")).toString();
        const QString curModel = current.value(QLatin1String("model")).toString();
        m_modelCombo->blockSignals(true);
        m_modelCombo->clear();
        const QJsonArray groups = result.value(QLatin1String("groups")).toArray();
        for (const QJsonValue &gv : groups) {
            const QJsonObject group = gv.toObject();
            const QString gid = group.value(QLatin1String("id")).toString();
            const QJsonArray models = group.value(QLatin1String("models")).toArray();
            for (const QJsonValue &mv : models) {
                const QJsonObject model = mv.toObject();
                const QString mid = model.value(QLatin1String("id")).toString();
                QVariantMap data;
                data.insert(QStringLiteral("provider"), gid);
                data.insert(QStringLiteral("model"), mid);
                m_modelCombo->addItem(QStringLiteral("%1 / %2").arg(gid).arg(mid), data);
                if (gid == curProvider && mid == curModel)
                    m_modelCombo->setCurrentIndex(m_modelCombo->count() - 1);
            }
        }
        if (m_modelCombo->count() > 0 && m_modelCombo->currentIndex() < 0)
            m_modelCombo->setCurrentIndex(0);
        m_modelCombo->blockSignals(false);
        return;
    }

    // ---- 历史消息 ----
    if (rpcId == m_historyRpcId) {
        m_historyRpcId.clear();
        if (!ok) {
            appendSystemLine(QStringLiteral("加载历史失败:%1").arg(result.value(QLatin1String("message")).toString()));
            return;
        }
        renderHistoryEvents(result.value(QLatin1String("events")).toArray());
        scrollMessageToBottom(); // 历史渲染完成后, 确保消息显示框停在底部
        return;
    }

    // ---- 取消 ----
    if (rpcId == m_cancelRpcId) {
        m_cancelRpcId.clear();
        if (!ok)
            appendSystemLine(QStringLiteral("取消失败:%1").arg(result.value(QLatin1String("message")).toString()));
        // 取消请求已返回, 会话停止由 session-status 事件最终复位; 此处先兜底复位按钮
        m_sessionRunning.insert(m_currentSessionId, false);
        updateSendButton();
        return;
    }

    // ---- 发送 ----
    if (rpcId == m_promptRpcId) {
        m_promptRpcId.clear();
        if (!ok)
            appendSystemLine(QStringLiteral("发送失败:%1").arg(result.value(QLatin1String("message")).toString()));
        return;
    }

    // ---- 切换模型 ----
    if (rpcId == m_selectRpcId) {
        m_selectRpcId.clear();
        if (!ok)
            appendSystemLine(QStringLiteral("切换模型失败:%1").arg(result.value(QLatin1String("message")).toString()));
        return;
    }

    // ---- 归档会话 ----
    if (rpcId == m_archiveRpcId) {
        m_archiveRpcId.clear();
        if (!ok) {
            appendSystemLine(QStringLiteral("归档失败:%1").arg(result.value(QLatin1String("message")).toString()));
            return;
        }
        const QString sid = m_archiveSession;
        m_archiveSession.clear();
        if (!sid.isEmpty()) {
            m_archivedLocal.insert(sid); // 本地兜底记录,立即过滤
            saveArchivedCache();
        }
        // 归档后清空消息/索引/输入框,并禁用发送(当前会话已不存在)
        m_currentSessionId.clear();
        m_subagentRunning = false; // 归档后复位子会话标记
        m_baseSessionTitle.clear();
        m_titleLabel->setText(QStringLiteral("未选择会话"));
        if (m_presetLabel)
            m_presetLabel->clear();
        clearMessageArea();
        updateUiEnabled(); // 发送/输入/权限/模型等变灰
        loadTree(); // 立即刷新树,不依赖 host/archived-sessions-changed 事件
        return;
    }

    // ---- 新增工作区 ----
    if (rpcId == m_createWsRpcId) {
        m_createWsRpcId.clear();
        if (!ok) {
            appendSystemLine(QStringLiteral("新增工作区失败:%1").arg(result.value(QLatin1String("message")).toString()));
            m_pendingWsName.clear();
            return;
        }
        const QJsonObject ws = result.value(QLatin1String("workspace")).toObject();
        const QString wsId = ws.value(QLatin1String("workspaceId")).toString();
        const QString title = ws.value(QLatin1String("title")).toString();
        // DSH 的 create 标题默认取目录名;若用户填的名称不同,再 rename
        if (!m_pendingWsName.isEmpty() && m_pendingWsName != title) {
            m_pendingWsId = wsId;
            m_renameWsRpcId = m_commander->renameWorkspace(wsId, m_pendingWsName);
            setStatus(QStringLiteral("设置工作区名称..."), QStringLiteral("#9AA0A8"));
            return;
        }
        m_pendingWsName.clear();
        m_pendingWsId.clear();
        loadTree();
        return;
    }

    // ---- 重命名工作区(新增工作区设置名称 / 右键修改名称) ----
    if (rpcId == m_renameWsRpcId) {
        m_renameWsRpcId.clear();
        m_pendingWsName.clear();
        m_pendingWsId.clear();
        if (!ok) {
            appendSystemLine(QStringLiteral("工作区名称设置失败:%1").arg(result.value(QLatin1String("message")).toString()));
        } else {
            appendSystemLine(QStringLiteral("工作区名称已更新"));
        }
        loadTree();
        return;
    }

    // ---- 删除工作区 ----
    if (rpcId == m_deleteWsRpcId) {
        m_deleteWsRpcId.clear();
        if (!ok) {
            appendSystemLine(QStringLiteral("删除工作区失败:%1").arg(result.value(QLatin1String("message")).toString()));
            return;
        }
        appendSystemLine(QStringLiteral("已删除工作区"));
        loadTree();
        return;
    }

    // ---- 修改会话名称(右键改名 / 新建会话设置名称) ----
    if (rpcId == m_renameSessionRpcId) {
        m_renameSessionRpcId.clear();
        m_pendingSessionName.clear();
        if (!ok) {
            appendSystemLine(QStringLiteral("会话名称设置失败:%1").arg(result.value(QLatin1String("message")).toString()));
        } else {
            appendSystemLine(QStringLiteral("会话名称已更新"));
        }
        loadTree(); // 重新拉取,projections.title 会带最新名称;若为新建会话会顺带打开(pendingOpenSession)
        return;
    }

    // ---- 批量补会话名称(从 session.history 取最后一条 session/title)----
    if (rpcId == m_titleHistoryRpcId) {
        m_titleHistoryRpcId.clear();
        const QString sid = m_titleFetchSession;
        m_titleFetchSession.clear();
        if (ok) {
            // 事件按时间正序,取最后一条 session/title 的 title(latest wins)
            QString title;
            const QJsonArray events = result.value(QLatin1String("events")).toArray();
            for (const QJsonValue &v : events) {
                const QJsonObject e = v.toObject();
                if (e.value(QLatin1String("type")).toString() == QStringLiteral("session/title")) {
                    const QString t = e.value(QLatin1String("data")).toObject()
                                          .value(QLatin1String("title")).toString();
                    if (!t.isEmpty())
                        title = t;
                }
            }
            if (!title.isEmpty()) {
                m_sessionTitles.insert(sid, title);
                saveTitleCache();
                updateSessionNode(sid);
            }
        }
        // 处理下一个(串行,避免并发风暴)
        startNextTitleFetch();
        return;
    }
}

// ---------------------------------------------------------------- 事件渲染

void CDSHChatWindow::onMuxEvent(const QString &type, const QJsonObject &payload)
{
    const QString sessionId = payload.value(QLatin1String("sessionId")).toString();
    // approval/requested 由专门的 onApprovalRequested 槽处理(带 rpcId 才能应答), 这里不再渲染
    if (type == QStringLiteral("approval/resolved")) {
        // 审批结果已出: 更新对应按钮条为“已处理”
        settleApproval(payload.value(QLatin1String("approvalId")).toString(),
                       payload.value(QLatin1String("outcome")).toString());
        return;
    }
    if (type == QStringLiteral("question/requested") && sessionId == m_currentSessionId) {
        const QJsonArray questions = payload.value(QLatin1String("questions")).toArray();
        if (!questions.isEmpty()) {
            appendSystemLine(QStringLiteral("❓ 向你提问:%1")
                .arg(questions.first().toObject().value(QLatin1String("question")).toString()));
        }
    }
    // 其余帧(session/subscribed、session/queue 等)暂不展示
}

// 收到 approval/requested → 在消息流底部插入带【允许一次/拒绝】按钮的行内条
void CDSHChatWindow::onApprovalRequested(const QString &rpcId, const QJsonObject &payload)
{
    if (payload.value(QLatin1String("sessionId")).toString() != m_currentSessionId)
        return;
    const QString approvalId = payload.value(QLatin1String("approvalId")).toString();
    const QString toolName = payload.value(QLatin1String("toolName")).toString();
    const QString reason = payload.value(QLatin1String("reason")).toString();
    addApprovalInline(rpcId, m_currentSessionId, approvalId, toolName, reason);
}

// 构造行内审批按钮条
void CDSHChatWindow::addApprovalInline(const QString &rpcId, const QString &sessionId,
                                       const QString &approvalId, const QString &toolName,
                                       const QString &reason)
{
    auto *row = new QWidget;
    auto *vl = new QVBoxLayout(row);
    vl->setContentsMargins(0, 0, 0, 0);
    vl->setSpacing(4);

    // 标题行: 审批提示
    auto *head = new QLabel(row);
    head->setWordWrap(true);
    head->setStyleSheet(QStringLiteral(
        "QLabel { color:#cfd3d6; font-size:12px; padding:2px 0; }"));
    head->setText(QStringLiteral("⚠ 需要审批:%1").arg(toolName));
    vl->addWidget(head);
    if (!reason.isEmpty()) {
        auto *reasonLabel = new QLabel(row);
        reasonLabel->setWordWrap(true);
        reasonLabel->setStyleSheet(QStringLiteral(
            "QLabel { color:#9AA0A8; font-size:12px; padding:2px 0; }"));
        reasonLabel->setText(reason);
        vl->addWidget(reasonLabel);
    }

    // 按钮行: 允许一次 / 拒绝
    auto *btnRow = new QHBoxLayout;
    btnRow->setContentsMargins(0, 4, 0, 0);
    btnRow->setSpacing(8);
    auto *allowBtn = new QPushButton(QStringLiteral("允许一次"), row);
    allowBtn->setObjectName(QStringLiteral("allowBtn"));
    allowBtn->setStyleSheet(QStringLiteral(
        "QPushButton#allowBtn { background-color:#1F6F43; color:#f9fafb;"
        "  border:1px solid #2C2F35; border-radius:6px; padding:4px 14px; }"
        "QPushButton#allowBtn:hover { background-color:#268A52; }"));
    auto *rejectBtn = new QPushButton(QStringLiteral("拒绝"), row);
    rejectBtn->setObjectName(QStringLiteral("rejectBtn"));
    rejectBtn->setStyleSheet(QStringLiteral(
        "QPushButton#rejectBtn { background-color:#7A2E2E; color:#f9fafb;"
        "  border:1px solid #2C2F35; border-radius:6px; padding:4px 14px; }"
        "QPushButton#rejectBtn:hover { background-color:#993A3A; }"));
    btnRow->addWidget(allowBtn);
    btnRow->addWidget(rejectBtn);
    btnRow->addStretch(1);
    vl->addLayout(btnRow);

    // 记录 approvalId → row(用于 approval/resolved 时更新)
    m_pendingApprovals.append(qMakePair(approvalId, row));
    m_messageLayout->addWidget(row);
    scrollMessageToBottom();

    // 点击“允许一次”
    connect(allowBtn, &QPushButton::clicked, this, [this, rpcId, sessionId, approvalId]() {
        m_commander->approve(rpcId, sessionId, approvalId, QStringLiteral("allowed-once"));
        settleApproval(approvalId, QStringLiteral("allowed-once"));
    });
    // 点击“拒绝”
    connect(rejectBtn, &QPushButton::clicked, this, [this, rpcId, sessionId, approvalId]() {
        m_commander->approve(rpcId, sessionId, approvalId, QStringLiteral("rejected"));
        settleApproval(approvalId, QStringLiteral("rejected"));
    });
}

// 审批已处理: 把对应按钮条置为“已处理”并禁用按钮(避免重复应答)
void CDSHChatWindow::settleApproval(const QString &approvalId, const QString &outcome)
{
    if (approvalId.isEmpty())
        return;
    for (int i = 0; i < m_pendingApprovals.size(); ++i) {
        if (m_pendingApprovals.at(i).first != approvalId)
            continue;
        QWidget *row = m_pendingApprovals.at(i).second;
        m_pendingApprovals.removeAt(i);

        // 禁用该行所有按钮
        const QList<QPushButton *> btns = row ? row->findChildren<QPushButton *>()
                                              : QList<QPushButton *>();
        for (QPushButton *b : btns)
            b->setEnabled(false);

        // 在行内追加一条“已处理”标记
        if (row) {
            QString label;
            QColor color(QStringLiteral("#8A8F96"));
            if (outcome == QLatin1String("allowed-once")) {
                label = QStringLiteral("已允许");
                color = QColor(QStringLiteral("#4ADE80"));
            } else if (outcome == QLatin1String("rejected")) {
                label = QStringLiteral("已拒绝");
                color = QColor(QStringLiteral("#F87171"));
            } else {
                label = QStringLiteral("已处理");
            }
            auto *done = new QLabel(QStringLiteral("✔ %1").arg(label), row);
            done->setStyleSheet(QStringLiteral("color:%1; font-size:11px;").arg(color.name()));
            if (auto *vl = qobject_cast<QVBoxLayout *>(row->layout()))
                vl->addWidget(done);
        }
        return;
    }
}

void CDSHChatWindow::onHostEvent(const QString &type, const QJsonObject &payload)
{
    if (type == QStringLiteral("host/archived-sessions-changed")) {
        // host 权威推送归档集合:直接更新并记忆到本地,确保冷启动也能过滤
        const QJsonArray archived = payload.value(QLatin1String("archivedSessionIds")).toArray();
        m_archivedSessionIds.clear();
        bool changed = false;
        for (const QJsonValue &v : archived) {
            const QString sid = v.toString();
            m_archivedSessionIds.insert(sid);
            if (!m_archivedLocal.contains(sid)) {
                m_archivedLocal.insert(sid);
                changed = true;
            }
        }
        if (changed)
            saveArchivedCache();
        loadTree(); // 归档集发生变化,刷新树
        return;
    }
    if (type == QStringLiteral("host/workspace-changed")
        || type == QStringLiteral("host/workspace-removed")
        || type == QStringLiteral("host/workspace-order-changed")
        || type == QStringLiteral("host/session-added")
        || type == QStringLiteral("host/session-removed")) {
        // 结构变化:刷新整树(频率低,可接受)
        loadTree();
    } else if (type == QStringLiteral("host/session-status")) {
        // 运行状态变化:只更新节点文本,不整树刷新
        const QString sid = payload.value(QLatin1String("sessionId")).toString();
        m_sessionRunning.insert(sid, payload.value(QLatin1String("running")).toBool());
        updateSessionNode(sid);
        if (sid == m_currentSessionId)
            updateSendButton(); // 当前会话运行状态变化 → 同步“发送/停止”按钮
        // 子会话(或其他会话)运行状态变化也可能影响“当前会话是否有运行中的子会话”,
        // 故无论哪个会话变化都刷新一次标题(成本低)。
        if (!m_currentSessionId.isEmpty())
            refreshSubagentState();
    }
}

void CDSHChatWindow::onSessionSnapshot(const QString &sessionId, const QJsonArray &events)
{
    // 打开会话的历史快照 → 批量渲染(仅当前会话)
    if (sessionId.isEmpty() || sessionId != m_currentSessionId)
        return;
    renderHistoryEvents(events);
    scrollMessageToBottom();
}

void CDSHChatWindow::onSessionEvent(const QString &sessionId, const QJsonObject &event)
{
    // 只渲染当前会话的事件(其他会话的历史通过 session.history 单独加载)
    if (sessionId != m_currentSessionId)
        return;
    const QString type = event.value(QLatin1String("type")).toString();
    // 用户消息已在发送时本地回显,避免实时事件重复显示
    if (type == QStringLiteral("user/message")) {
        const QString text = extractText(event.value(QLatin1String("data")).toObject());
        if (isSystemInjection(text))
            addContextCard(text); // 实时也显示上下文注入卡片
        return;
    }
    if (type == QStringLiteral("assistant/chunk")) {
        // 流式思考增量:累积并实时更新“思考最后一行”
        const QJsonObject chunk = event.value(QLatin1String("data")).toObject()
                                      .value(QLatin1String("chunk")).toObject();
        if (chunk.value(QLatin1String("type")).toString() == QStringLiteral("reasoning-delta")) {
            m_thinking += chunk.value(QLatin1String("text")).toString();
            QString last = m_thinking.split(QLatin1Char('\n')).last();
            // 固定高度单行展示, 超长时用省略号截断(只显示思考文字变动)
            last = QStringLiteral("思考:%1").arg(last);
            const QFontMetrics fm(m_thinkLabel->font());
            const int availW = m_thinkLabel->width() > 0 ? m_thinkLabel->width() : 420;
            m_thinkLabel->setText(fm.elidedText(last, Qt::ElideRight, availW));
            m_thinkLabel->show();
        }
        return; // chunk 不进入消息流(避免卡顿)
    }
    if (type == QStringLiteral("assistant/message")) {
        // 思考完成: 顶部思考栏统一显示“思考完成”, 另加可展开思考块
        const QJsonObject d = event.value(QLatin1String("data")).toObject();
        const QJsonArray blocks = d.value(QLatin1String("message")).toObject()
                                      .value(QLatin1String("content")).toArray();
        QString thinking;
        for (const QJsonValue &bv : blocks) {
            if (bv.toObject().value(QLatin1String("type")).toString() == QStringLiteral("reasoning"))
                thinking += bv.toObject().value(QLatin1String("text")).toString();
        }
        // 思考完成标记(绿色小勾)
        m_thinkLabel->setText(QStringLiteral("✔ 思考完成"));
        m_thinking.clear();
        const QString textHtml = renderMessageBlocks(blocks);
        if (!textHtml.isEmpty())
            addBubble(textHtml, false);
        if (!thinking.trimmed().isEmpty())
            addThinkBlock(thinking); // 可展开思考
        return;
    }
    if (type == QStringLiteral("tool/call")) {
        const QJsonObject data = event.value(QLatin1String("data")).toObject();
        const QString name = data.value(QLatin1String("name")).toString();
        if (name == QStringLiteral("ask_user_question"))
            addBubble(askQuestionHtml(data.value(QLatin1String("arguments"))), false);
        else
            addToolCallWidget(name, toolFileName(data.value(QLatin1String("arguments"))),
                              prettyJson(data.value(QLatin1String("arguments"))));
        return;
    }
    // 任务进度(整表快照): 每次携带完整列表, 直接替换
    if (type == QStringLiteral("todo/write")) {
        onTodoWrite(event.value(QLatin1String("data")).toObject()
                          .value(QLatin1String("todos")).toArray());
        return;
    }
    // 新一轮开始时清空上一轮的进度清单(与官方一致, 每轮重置)
    if (type == QStringLiteral("turn/start")) {
        clearTodoDock();
        return;
    }
    // 任务完成:当前会话一轮问询结束(回合收尾),右下角气泡提示
    if (type == QStringLiteral("turn/end")) {
        notifyTaskComplete(QStringLiteral("任务完成"), QStringLiteral("本轮对话已完成"));
        return;
    }
    const QString html = eventHtml(event);
    if (!html.isEmpty())
        addBubble(html, false); // 过滤后为空的事件不产生空气泡
}

void CDSHChatWindow::renderHistoryEvents(const QJsonArray &events)
{
    for (const QJsonValue &v : events) {
        const QJsonObject entry = v.toObject();
        const QJsonObject evt = entry.value(QLatin1String("event")).toObject();
        const QString type = evt.value(QLatin1String("type")).toString();
        if (type == QStringLiteral("user/message")) {
            const QString text = extractText(evt.value(QLatin1String("data")).toObject());
            if (text.isEmpty())
                continue;
            if (isSystemInjection(text)) {
                addContextCard(text); // 上下文注入卡片
                continue;
            }
            // 我的提问:右对齐气泡 + 索引(仅含中文才建索引;全英文多为编译报错不建)
            const bool cjk = containsCjk(text);
            if (cjk && m_questionList)
                new QListWidgetItem(text.section(QLatin1Char('\n'), 0, 0), m_questionList);
            addBubble(userMessageHtml(text), true, cjk);
            continue;
        }
        if (type == QStringLiteral("tool/call")) {
            const QJsonObject data = evt.value(QLatin1String("data")).toObject();
            const QString name = data.value(QLatin1String("name")).toString();
            if (name == QStringLiteral("ask_user_question")) {
                addBubble(askQuestionHtml(data.value(QLatin1String("arguments"))), false);
            } else {
                addToolCallWidget(name, toolFileName(data.value(QLatin1String("arguments"))),
                                  prettyJson(data.value(QLatin1String("arguments"))));
            }
            continue;
        }
        if (type == QStringLiteral("assistant/message")) {
            const QJsonArray blocks = evt.value(QLatin1String("data")).toObject()
                                          .value(QLatin1String("message")).toObject()
                                          .value(QLatin1String("content")).toArray();
            QString thinking;
            for (const QJsonValue &bv : blocks) {
                if (bv.toObject().value(QLatin1String("type")).toString() == QStringLiteral("reasoning"))
                    thinking += bv.toObject().value(QLatin1String("text")).toString();
            }
            const QString textHtml = renderMessageBlocks(blocks);
            if (!textHtml.isEmpty())
                addBubble(textHtml, false);
            if (!thinking.trimmed().isEmpty())
                addThinkBlock(thinking); // 可展开思考(历史回放也一致)
            continue;
        }
        // 任务进度整表快照: 历史回放直接应用(不生成气泡)
        if (type == QStringLiteral("todo/write")) {
            onTodoWrite(evt.value(QLatin1String("data")).toObject()
                              .value(QLatin1String("todos")).toArray());
            continue;
        }
        // 其他事件(系统)左对齐
        const QString html = eventHtml(evt);
        if (!html.isEmpty())
            addBubble(html, false);
    }
}

QString CDSHChatWindow::eventHtml(const QJsonObject &event)
{
    const QString type = event.value(QLatin1String("type")).toString();
    const QJsonObject data = event.value(QLatin1String("data")).toObject();

    if (type == QStringLiteral("user/message")) {
        const QString text = extractText(data); // user/message 的 data 即消息本体
        if (isSystemInjection(text))
            return QString(); // 系统上下文注入(role system)不显示
        return userMessageHtml(text);
    }
    if (type == QStringLiteral("assistant/message")) {
        const QJsonArray blocks = data.value(QLatin1String("message")).toObject()
                                      .value(QLatin1String("content")).toArray();
        return assistantMessageHtml(renderMessageBlocks(blocks));
    }
    if (type == QStringLiteral("tool/call")) {
        const QString name = data.value(QLatin1String("name")).toString();
        if (name == QStringLiteral("ask_user_question")) {
            // 提问工具:渲染成问题卡(不显示原始 JSON)
            return askQuestionHtml(data.value(QLatin1String("arguments")));
        }
        // 只显示“工具名 + 文件名”,不展开 JSON 参数(实时与历史一致)
        const QString file = toolFileName(data.value(QLatin1String("arguments")));
        return QStringLiteral("<b>🔧 %1</b> <span style=\"color:#C9CDD4;\">%2</span>")
            .arg(escapeHtml(name), escapeHtml(file));
    }
    if (type == QStringLiteral("tool/result")) {
        const QJsonArray blocks = data.value(QLatin1String("message")).toObject()
                                      .value(QLatin1String("content")).toArray();
        QString toolName;
        QString errMsg;
        for (const QJsonValue &v : blocks) {
            const QJsonObject b = v.toObject();
            if (b.value(QLatin1String("type")).toString() != QStringLiteral("tool-result"))
                continue;
            if (toolName.isEmpty())
                toolName = b.value(QLatin1String("name")).toString();
            const QJsonObject result = b.value(QLatin1String("result")).toObject();
            if (result.value(QLatin1String("error")).isObject())
                errMsg = result.value(QLatin1String("error")).toObject()
                              .value(QLatin1String("message")).toString();
            else if (result.contains(QLatin1String("error")))
                errMsg = result.value(QLatin1String("error")).toString();
        }
        if (errMsg.isEmpty())
            return QString(); // 正常结果不重复显示(tool/call 已显示“工具 文件名”)
        // 有错误:工具名 + 换行 + 红色错误信息
        return QStringLiteral("<div style=\"color:#F87171;margin:4px 0;\">🔧 %1<br>错误:%2</div>")
            .arg(escapeHtml(toolName), escapeHtml(errMsg));
    }
    if (type == QStringLiteral("session/title")) {
        const QString title = data.value(QLatin1String("title")).toString();
        if (!title.isEmpty()) {
            m_sessionTitles.insert(m_currentSessionId, title);
            m_titleLabel->setText(title);
            updateSessionNode(m_currentSessionId);
            saveTitleCache(); // 标题变化时写回本地缓存
        }
        return QString();
    }
    if (type == QStringLiteral("turn/start"))
        return QString(); // 系统固定提示:不再显示
    if (type == QStringLiteral("turn/end"))
        return QString(); // 系统固定提示:不再显示
    // 其余事件全部过滤(assistant/chunk 等流式增量、step 边界、compaction、
    // hook、team、approval 会话事件等),避免大量内部事件逐个触发排版导致卡顿
    return QString();
}

// ---------------------------------------------------------------- 渲染工具

void CDSHChatWindow::appendUserMessage(const QString &text)
{
    if (text.isEmpty())
        return;
    // 用户消息:右对齐气泡 + 索引(仅含中文的提问才建索引;全英文多为编译报错不建)
    const bool cjk = containsCjk(text);
    if (cjk && m_questionList)
        new QListWidgetItem(text.section(QLatin1Char('\n'), 0, 0), m_questionList);
    addBubble(userMessageHtml(text), true, cjk);
}

// 双击问题记录表:滚动到对应提问的气泡 widget
void CDSHChatWindow::onQuestionJump(int row)
{
    if (row < 0 || row >= m_userBubbles.size())
        return;
    QWidget *w = m_userBubbles[row];
    if (w && m_messageScroll)
        m_messageScroll->ensureWidgetVisible(w);
}

void CDSHChatWindow::appendAssistantMessage(const QJsonObject &message)
{
    addBubble(assistantMessageHtml(renderMessageBlocks(
        message.value(QLatin1String("content")).toArray())), false);
}

void CDSHChatWindow::appendToolCard(const QString &toolName, const QString &argsText, const QString &resultText)
{
    addBubble(toolCardHtml(toolName, argsText, resultText), false);
}

void CDSHChatWindow::appendSystemLine(const QString &text)
{
    addSystemLine(text);
}

QString CDSHChatWindow::userMessageHtml(const QString &text) const
{
    if (text.isEmpty())
        return QString();
    // 纯内容(气泡背景与对齐由 addBubble 提供)
    return escapeHtml(text).replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
}

QString CDSHChatWindow::assistantMessageHtml(const QString &innerHtml) const
{
    // 纯内容(气泡背景与对齐由 addBubble 提供)
    return innerHtml;
}

QString CDSHChatWindow::toolCardHtml(const QString &toolName, const QString &argsText, const QString &resultText) const
{
    QString html = QStringLiteral(
        "<div style=\"background-color:#232832;border:1px solid #3A3F47;border-radius:8px;padding:8px 10px;margin:6px 0;\">");
    html += QStringLiteral("<b>🔧 %1</b>").arg(escapeHtml(toolName));
    if (!argsText.isEmpty()) {
        html += QStringLiteral("<pre style=\"color:#C9CDD4;margin:4px 0 0 0;white-space:pre-wrap;\">%1</pre>")
                    .arg(escapeHtml(argsText));
    }
    if (!resultText.isEmpty()) {
        html += QStringLiteral("<div style=\"color:#9AA0A8;margin-top:4px;\">结果:%1</div>")
                    .arg(escapeHtml(resultText).left(400));
    }
    html += QStringLiteral("</div>");
    return html;
}

QString CDSHChatWindow::systemLineHtml(const QString &text) const
{
    return QStringLiteral(
        "<div style=\"color:#8A9099;font-size:12px;margin:2px 0;\">%1</div>").arg(escapeHtml(text));
}

QString CDSHChatWindow::renderMessageBlocks(const QJsonArray &blocks) const
{
    QString html;
    for (const QJsonValue &v : blocks) {
        const QJsonObject b = v.toObject();
        const QString type = b.value(QLatin1String("type")).toString();
        if (type == QStringLiteral("text")) {
            // 文本块按 Markdown 渲染
            const QString md = b.value(QLatin1String("text")).toString();
            if (isSystemInjection(md))
                continue; // 跳过系统上下文注入(dsh-system-prompt 的 runtime/policy 信息)
            const QString rendered = markdownToHtml(md);
            html += rendered.isEmpty() ? escapeHtml(md).replace(QLatin1Char('\n'), QStringLiteral("<br/>"))
                                       : rendered;
        } else if (type == QStringLiteral("reasoning")) {
            // 思考信息不需要显示(完全隐藏)
        } else if (type == QStringLiteral("image")) {
            html += QStringLiteral("<div style=\"color:#8A9099;\">[图片]</div>");
        }
        // tool-call / tool-result 块由独立的 tool/call、tool/result 事件渲染,此处跳过以防重复
    }
    return html;
}

QString CDSHChatWindow::extractText(const QJsonObject &message)
{
    QString text;
    const QJsonArray blocks = message.value(QLatin1String("content")).toArray();
    for (const QJsonValue &v : blocks) {
        const QJsonObject b = v.toObject();
        if (b.value(QLatin1String("type")).toString() == QStringLiteral("text"))
            text += b.value(QLatin1String("text")).toString();
    }
    return text.trimmed();
}

QString CDSHChatWindow::prettyJson(const QJsonValue &value)
{
    if (value.isUndefined() || value.isNull())
        return QString();
    // 工具参数等字段是 JSON 编码的字符串,先解析成对象/数组再美化
    if (value.isString()) {
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(value.toString().toUtf8(), &perr);
        if (perr.error == QJsonParseError::NoError && (doc.isObject() || doc.isArray()))
            return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
        return escapeHtml(value.toString()); // 不是 JSON 字符串则按原文
    }
    QJsonDocument doc;
    if (value.isObject())
        doc = QJsonDocument(value.toObject());
    else if (value.isArray())
        doc = QJsonDocument(value.toArray());
    else
        doc = QJsonDocument(QJsonObject{{QStringLiteral("v"), value}});
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

QString CDSHChatWindow::escapeHtml(QString text)
{
    text.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    text.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    text.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    text.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    return text;
}

void CDSHChatWindow::updateSessionNode(const QString &sessionId)
{
    for (int w = 0; w < m_sessionTree->topLevelItemCount(); ++w) {
        QTreeWidgetItem *wsItem = m_sessionTree->topLevelItem(w);
        for (int s = 0; s < wsItem->childCount(); ++s) {
            QTreeWidgetItem *sItem = wsItem->child(s);
            if (sItem->data(0, Qt::UserRole).toMap().value(QLatin1String(kNodeId)).toString() == sessionId) {
                const QString title = m_sessionTitles.value(sessionId, shortSessionId(sessionId));
                const bool running = m_sessionRunning.value(sessionId, false);
                sItem->setText(0, QStringLiteral("%1 %2")
                    .arg(running ? QStringLiteral("●") : QStringLiteral("○")).arg(title));
                return;
            }
        }
    }
}

void CDSHChatWindow::onError(const QString &message)
{
    appendSystemLine(QStringLiteral("❌ %1").arg(message));
}

// 启动时从本地缓存文件加载会话标题(跨运行保留,冷启动也能显示名称)
void CDSHChatWindow::loadTitleCache()
{
    m_titleCachePath = QCoreApplication::applicationDirPath()
                       + QStringLiteral("/Resource/session_titles.json");
    QFile file(m_titleCachePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject())
        return;
    const QJsonObject obj = doc.object();
    for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
        if (it.value().isString() && !it.value().toString().isEmpty())
            m_sessionTitles.insert(it.key(), it.value().toString());
    }
}

// 标题变化时写回本地缓存文件
void CDSHChatWindow::saveTitleCache()
{
    if (m_titleCachePath.isEmpty())
        return;
    QDir dir = QFileInfo(m_titleCachePath).absoluteDir();
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));
    QJsonObject obj;
    for (auto it = m_sessionTitles.constBegin(); it != m_sessionTitles.constEnd(); ++it)
        obj.insert(it.key(), it.value());
    QFile file(m_titleCachePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.close();
}

// 树加载后,为还没有名称(本地缓存也没有)的会话排队,批量拉取名称
void CDSHChatWindow::queueTitleFetch()
{
    m_titleFetchQueue.clear();
    for (const QJsonValue &wv : m_wsItems) {
        const QJsonObject ws = wv.toObject();
        const QJsonArray sessionIds = ws.value(QLatin1String("sessionIds")).toArray();
        for (const QJsonValue &sv : sessionIds) {
            const QString sid = sv.toString();
            if (sid.isEmpty() || isArchived(sid))
                continue; // 归档会话不拉
            const QJsonObject summary = m_sessionMap.value(sid).toObject();
            // 子会话 / 空白会话不拉名称(与树过滤一致)
            if (summary.value(QLatin1String("origin")).toString() == QStringLiteral("subagent")
                || summary.value(QLatin1String("blank")).toBool())
                continue;
            if (m_sessionTitles.contains(sid))
                continue; // 已有名称,跳过
            m_titleFetchQueue.append(sid);
        }
    }
    startNextTitleFetch();
}

// 串行逐个拉取:每个会话取一次 session.history,避免并发请求风暴卡顿
void CDSHChatWindow::startNextTitleFetch()
{
    if (m_titleFetchBusy)
        return;
    while (!m_titleFetchQueue.isEmpty()) {
        const QString sid = m_titleFetchQueue.takeFirst();
        if (m_sessionTitles.contains(sid) || isArchived(sid))
            continue; // 已处理或已归档
        m_titleFetchBusy = true;
        m_titleFetchSession = sid;
        m_titleHistoryRpcId = m_commander->sessionHistory(sid, -1, 100); // 从尾部取 100 条找标题
        return;
    }
}

// 是否归档:服务端返回的 + 本地兜底记录
bool CDSHChatWindow::isArchived(const QString &sessionId) const
{
    return m_archivedSessionIds.contains(sessionId) || m_archivedLocal.contains(sessionId);
}

// 启动时加载本地归档集合(兜底:即使服务端 archivedSessionIds 为空也能过滤)
void CDSHChatWindow::loadArchivedCache()
{
    m_archivedCachePath = QCoreApplication::applicationDirPath()
                          + QStringLiteral("/Resource/archived_sessions.json");
    QFile file(m_archivedCachePath);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isArray())
        return;
    m_archivedLocal.clear();
    for (const QJsonValue &v : doc.array())
        m_archivedLocal.insert(v.toString());
}

// 归档变动时写回本地文件
void CDSHChatWindow::saveArchivedCache()
{
    if (m_archivedCachePath.isEmpty())
        return;
    QDir dir = QFileInfo(m_archivedCachePath).absoluteDir();
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));
    QJsonArray arr;
    for (const QString &sid : m_archivedLocal)
        arr.append(sid);
    QFile file(m_archivedCachePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    file.close();
}

// 为对话框应用深色(黑色)主题
void CDSHChatWindow::applyDarkStyle(QWidget *w)
{
    if (!w)
        return;
    w->setStyleSheet(QStringLiteral(
        "QDialog { background-color: #151517; }"
        "QWidget { color: #f9fafb; background-color: #151517; font-family: 'Segoe UI','Microsoft YaHei'; }"
        "QLabel { background: transparent; color: #f9fafb; }"
        "QLineEdit { background-color: #1B1E24; color: #f9fafb;"
        "  border: 1px solid #2C2F35; border-radius: 4px; padding: 4px; }"
        "QComboBox { background-color: #1B1E24; color: #f9fafb;"
        "  border: 1px solid #2C2F35; border-radius: 4px; padding: 4px; }"
        "QComboBox QAbstractItemView { background-color: #1B1E24; color: #f9fafb;"
        "  selection-background-color: #2C2F35; selection-color: #f9fafb; }"
        "QPushButton { background-color: #2A2D33; color: #f9fafb;"
        "  border: 1px solid #2C2F35; border-radius: 4px; padding: 6px 14px; }"
        "QPushButton:hover { background-color: #34373E; }"
        "QDialogButtonBox QPushButton { background-color: #1B1E24; color: #f9fafb; }"
        "QDialogButtonBox QPushButton:hover { background-color: #2A2D33; }"
    ));
}

// 深色提示框: 全局 QWidget{color:#f9fafb} 会让 QMessageBox 文字变白但背景仍是浅色,
// 导致白字看不清。这里在 exec 前给消息框本身设置深色样式, 保证可读。
void CDSHChatWindow::showDropWarning(const QString &text)
{
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("提示"));
    box.setIcon(QMessageBox::Warning);
    box.setText(text);
    box.setStyleSheet(QStringLiteral(
        "QMessageBox { background-color: #1B1E24; color: #f9fafb; }"
        "QMessageBox QLabel { color: #f9fafb; background: transparent; }"
        "QPushButton { background-color: #2A2D33; color: #f9fafb;"
        "  border: 1px solid #2C2F35; border-radius: 4px; padding: 6px 16px; }"
        "QPushButton:hover { background-color: #34373E; }"
    ));
    box.exec();
}

// 弹窗输入名称;取消或为空返回空字符串
QString CDSHChatWindow::promptForName(const QString &caption, const QString &initial)
{
    QDialog dlg(this);
    dlg.setWindowTitle(caption);
    dlg.setModal(true);
    applyDarkStyle(&dlg);
    auto *form = new QFormLayout(&dlg);
    auto *edit = new QLineEdit(initial, &dlg);
    form->addRow(QStringLiteral("名称:"), edit);
    auto *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(btnBox, &QDialogButtonBox::accepted, &dlg, [&dlg, edit]() {
        if (edit->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, QStringLiteral("提示"), QStringLiteral("名称不能为空"));
        } else {
            dlg.accept();
        }
    });
    QObject::connect(btnBox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(btnBox);
    dlg.setMinimumWidth(320);
    if (dlg.exec() != QDialog::Accepted)
        return QString();
    return edit->text().trimmed();
}

// 拉取 Agent 预设列表(供新建会话下拉/会话名称旁显示)
void CDSHChatWindow::loadPresets()
{
    m_presetsRpcId = m_commander->listAgentPresets();
}

// 预设 id → 显示名(无 name 时用 id)
QString CDSHChatWindow::presetName(const QString &id) const
{
    for (const auto &p : m_presets) {
        if (p.first == id)
            return p.second.isEmpty() ? p.first : p.second;
    }
    return id;
}

// 更新会话名称旁的 Agent 预设标签(只读,不可修改)
void CDSHChatWindow::updatePresetLabel()
{
    const QJsonObject summary = m_sessionMap.value(m_currentSessionId).toObject();
    const QString presetId = summary.value(QLatin1String("agentPreset")).toString();
    m_presetLabel->setText(presetId.isEmpty()
        ? QString()
        : QStringLiteral("· %1").arg(presetName(presetId)));
}

// 收到提问帧:显示可交互问答面板
void CDSHChatWindow::onQuestionRequested(const QString &rpcId, const QString &sessionId, const QJsonArray &questions)
{
    showQuestionPanel(rpcId, sessionId, questions);
}

void CDSHChatWindow::showQuestionPanel(const QString &rpcId, const QString &sessionId, const QJsonArray &questions)
{
    m_qRpcId = rpcId;
    m_qSessionId = sessionId;
    m_qQuestions = questions;
    m_qIndex = 0;
    m_qAnswers = QJsonArray();
    displayCurrentQuestion();
    m_qPanel->show();
}

// 显示 m_qQuestions[m_qIndex] 这一题
void CDSHChatWindow::displayCurrentQuestion()
{
    if (m_qIndex < 0 || m_qIndex >= m_qQuestions.size())
        return;
    const QJsonObject q = m_qQuestions.at(m_qIndex).toObject();
    m_qHeader->setText(q.value(QLatin1String("header")).toString());
    // 多题时在标题上标注进度,如 "3/3"
    if (m_qQuestions.size() > 1)
        m_qHeader->setText(QStringLiteral("%1/%2 · %3")
            .arg(m_qIndex + 1).arg(m_qQuestions.size())
            .arg(q.value(QLatin1String("header")).toString()));
    m_qText->setText(q.value(QLatin1String("question")).toString());
    m_qEdit->clear();

    // 清空旧选项
    while (QLayoutItem *item = m_qOptions->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    // 逐项生成选项按钮(编号 + 标签 + 说明),点击即提交该选项
    const QJsonArray opts = q.value(QLatin1String("options")).toArray();
    for (int i = 0; i < opts.size(); ++i) {
        const QJsonObject o = opts.at(i).toObject();
        const QString label = o.value(QLatin1String("label")).toString();
        const QString desc = o.value(QLatin1String("description")).toString();
        QString text = QStringLiteral("%1  %2").arg(i + 1).arg(label);
        if (!desc.isEmpty())
            text += QStringLiteral("  ·  %1").arg(desc);
        auto *btn = new QPushButton(text, m_qPanel);
        btn->setObjectName(QStringLiteral("plainBtn"));
        connect(btn, &QPushButton::clicked, this, [this, label]() { submitAnswer(label, QString()); });
        m_qOptions->addWidget(btn);
    }
}

void CDSHChatWindow::submitAnswer(const QString &selectedLabel, const QString &customText)
{
    if (m_qRpcId.isEmpty() || m_qSessionId.isEmpty())
        return;
    if (m_qIndex < 0 || m_qIndex >= m_qQuestions.size())
        return;
    const QJsonObject q = m_qQuestions.at(m_qIndex).toObject();
    QJsonObject answerItem;
    answerItem.insert(QLatin1String("id"), q.value(QLatin1String("id")).toString());
    if (!customText.isEmpty()) {
        answerItem.insert(QLatin1String("custom"), customText);
        answerItem.insert(QLatin1String("selected"), QJsonArray());
    } else if (!selectedLabel.isEmpty()) {
        QJsonArray sel;
        sel.append(selectedLabel);
        answerItem.insert(QLatin1String("selected"), sel);
    } else {
        // 跳过本题
        answerItem.insert(QLatin1String("selected"), QJsonArray());
    }
    m_qAnswers.append(answerItem);
    appendSystemLine(QStringLiteral("第 %1 题已作答:%2")
        .arg(m_qIndex + 1).arg(customText.isEmpty() ? selectedLabel : customText));

    ++m_qIndex;
    if (m_qIndex < m_qQuestions.size()) {
        // 还有题,显示下一题
        displayCurrentQuestion();
        return;
    }
    // 全部答完,一次性回传所有答案
    QJsonObject answer;
    answer.insert(QLatin1String("answers"), m_qAnswers);
    QJsonObject value;
    value.insert(QLatin1String("sessionId"), m_qSessionId);
    value.insert(QLatin1String("answer"), answer);
    m_commander->respond(m_qRpcId, value);
    clearQuestionPanel();
}

void CDSHChatWindow::clearQuestionPanel()
{
    if (m_qPanel)
        m_qPanel->hide();
    m_qRpcId.clear();
    m_qSessionId.clear();
    m_qQuestions = QJsonArray();
    m_qIndex = 0;
    m_qAnswers = QJsonArray();
    m_qEdit->clear();
    while (QLayoutItem *item = m_qOptions->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
}

// 系统托盘：任务完成后在操作系统右下角弹出气泡提示（不阻塞）
// 复用主窗口(MainWindow)已有的托盘图标，避免每个对话窗口再新增一个托盘图标：
//   - MDI 子窗口被 addSubWindow 托管后不再是顶层窗口；
//   - MainWindow 是顶层窗口（parent 为 null，不属于 QApplication 对象树，findChild 找不到），
//     因此遍历所有顶层窗口，找到唯一已加入系统托盘的图标来弹气泡。
void CDSHChatWindow::notifyTaskComplete(const QString &title, const QString &message)
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return; // 系统无托盘时静默
    QSystemTrayIcon *tray = nullptr;
    const QWidgetList tops = QApplication::topLevelWidgets();
    for (QWidget *w : tops) {
        tray = w->findChild<QSystemTrayIcon *>();
        if (tray)
            break;
    }
    if (!tray)
        return; // 找不到(如主窗口已关闭)则不弹，避免自建出第二个托盘图标
    tray->showMessage(title, message, QSystemTrayIcon::Information, 5000);
}

// 从 permissions 投影填充会话权限下拉(选项 + 默认当前值)
void CDSHChatWindow::updatePermissionCombo()
{
    m_permissionCombo->blockSignals(true);
    m_permissionCombo->clear();
    const QJsonObject summary = m_sessionMap.value(m_currentSessionId).toObject();
    const QJsonObject permissions = summary.value(QLatin1String("projections")).toObject()
                                        .value(QLatin1String("values")).toObject()
                                        .value(QLatin1String("permissions")).toObject();
    const QJsonArray options = permissions.value(QLatin1String("options")).toArray();
    if (options.isEmpty()) {
        // 兜底:固定三项,默认 workspace-write
        m_permissionCombo->addItem(QStringLiteral("Read Only"), QStringLiteral("read-only"));
        m_permissionCombo->addItem(QStringLiteral("Workspace Write"), QStringLiteral("workspace-write"));
        m_permissionCombo->addItem(QStringLiteral("Full access"), QStringLiteral("danger-full-access"));
        m_permissionCombo->setCurrentIndex(1);
    } else {
        const QString currentValue = permissions.value(QLatin1String("currentValue")).toString();
        int defaultIdx = 0;
        for (int i = 0; i < options.size(); ++i) {
            const QJsonObject o = options.at(i).toObject();
            const QString val = o.value(QLatin1String("value")).toString();
            m_permissionCombo->addItem(permissionDisplayName(val), val);
            if (val == currentValue)
                defaultIdx = i;
        }
        if (m_permissionCombo->count() > 0)
            m_permissionCombo->setCurrentIndex(defaultIdx);
    }
    m_permissionCombo->blockSignals(false);
}

// 把 ask_user_question 的 arguments 渲染成问题卡(标题 + 问题 + 选项),不显示原始 JSON
QString CDSHChatWindow::askQuestionHtml(const QJsonValue &args) const
{
    QJsonObject obj;
    if (args.isString()) {
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(args.toString().toUtf8(), &perr);
        if (perr.error == QJsonParseError::NoError && doc.isObject())
            obj = doc.object();
    } else if (args.isObject()) {
        obj = args.toObject();
    }
    if (obj.isEmpty())
        return QString();
    const QJsonArray qs = obj.value(QLatin1String("questions")).toArray();
    if (qs.isEmpty())
        return QString();
    const QJsonObject q = qs.first().toObject();
    const QString header = q.value(QLatin1String("header")).toString();
    const QString question = q.value(QLatin1String("question")).toString();

    QString html = QStringLiteral(
        "<div style=\"background-color:#232832;border:1px solid #3A3F47;border-radius:8px;padding:8px 10px;margin:6px 0;\">");
    if (!header.isEmpty())
        html += QStringLiteral("<div style=\"color:#9AA0A8;font-size:12px;\">%1</div>").arg(escapeHtml(header));
    html += QStringLiteral("<div style=\"color:#E6E8EB;font-size:14px;\">%1</div>").arg(escapeHtml(question));
    const QJsonArray opts = q.value(QLatin1String("options")).toArray();
    for (int i = 0; i < opts.size(); ++i) {
        const QJsonObject o = opts.at(i).toObject();
        const QString label = o.value(QLatin1String("label")).toString();
        const QString desc = o.value(QLatin1String("description")).toString();
        html += QStringLiteral("<div style=\"margin-top:4px;color:#E6E8EB;\">%1  %2").arg(i + 1).arg(escapeHtml(label));
        if (!desc.isEmpty())
            html += QStringLiteral("  ·  %1").arg(escapeHtml(desc));
        html += QStringLiteral("</div>");
    }
    html += QStringLiteral("</div>");
    return html;
}
