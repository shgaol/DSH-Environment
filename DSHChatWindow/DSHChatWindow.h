#ifndef DSHCHATWINDOW_H
#define DSHCHATWINDOW_H

#include "DSHCommander.h" // 提供 DSHImageAttachment(图片附件)结构

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMainWindow>
#include <QPair>
#include <QPoint>
#include <QSet>
#include <QString>
#include <QStringList>

class CDSHCommander;
class CDSDropTextEdit;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QWidget;
class QVBoxLayout;

// CDSHChatWindow:仿 DSH(DeepSeek Harness)的对话窗口。
//   左侧:工作区/会话两级树(工作区对应一个目录,其下为会话);
//   右侧:顶部(服务器地址、连接状态、会话标题、模型选择)、消息流、输入区。
// 性能:
//   - workspace.list 与 session.list 并行加载后组装树;
//   - 只渲染用户可见事件(assistant/chunk 等流式增量事件一律过滤);
//   - 历史消息拼成一个 HTML 块批量插入,避免逐条触发排版。
class CDSHChatWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit CDSHChatWindow(const QString &address = QString(), const QString &tokenUrl = QString(),
                            bool autoConnect = false, QWidget *parent = nullptr);
    ~CDSHChatWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override; // 关闭 = 隐藏
    bool eventFilter(QObject *obj, QEvent *event) override; // 拦截输入框回车(发送)、放行 Shift+回车(换行)

private slots:
    void onConnectClicked();
    void onNewSessionClicked();
    void onAddWorkspaceClicked(); // 新增工作区(弹窗输入名称 + 目录)
    void onRefreshSessionsClicked();
    void onTreeContextMenu(const QPoint &pos);
    void onTreeItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous);
    void onSendClicked();
    void onDropFile(const QString &localPath); // 拖入文件 → 按 DSH 附件协议判断处理
    void onExportPdf();            // 把消息区导出为 PDF
    void onInputTextChanged(); // 输入框根据内容自适应高度(最大为窗口一半)
    void onQuestionJump(int row); // 双击问题记录表跳到显示框对应提问处
    void onModelChanged(int index);
    void onCommandFinished(const QString &rpcId, bool ok, const QJsonObject &result);
    void onMuxEvent(const QString &type, const QJsonObject &payload);
    void onHostEvent(const QString &type, const QJsonObject &payload);
    void onSessionEvent(const QString &sessionId, const QJsonObject &event);
    void onQuestionRequested(const QString &rpcId, const QString &sessionId, const QJsonArray &questions);
    void onApprovalRequested(const QString &rpcId, const QJsonObject &payload); // 审批请求 → 行内按钮条
    void onConnectionChanged(bool connected, const QString &detail);
    void onError(const QString &message);
    void onWorkspaceFrame(const QJsonObject &frame); // workspace.follow 流帧 → 维护工作区树
    void onSessionSnapshot(const QString &sessionId, const QJsonArray &events); // session.follow 快照 → 渲染历史

private:
    void buildUi();
    void applyStyle();
    void loadTree();                 // 并行拉取工作区 + 会话
    void finishTreeLoad();
    void maybeFinishTreeLoad(); // 工作区基线 + session.list 都就绪才重建树
    void rebuildTree();
    void selectSessionInTree(const QString &sessionId);
    void openSession(const QString &sessionId);
    void renderHistoryEvents(const QJsonArray &events); // 历史消息批量渲染
    QString eventHtml(const QJsonObject &event); // 事件 → HTML(内部事件返回空串)
    void appendUserMessage(const QString &text);
    void appendAssistantMessage(const QJsonObject &message);
    void appendToolCard(const QString &toolName, const QString &argsText, const QString &resultText);
    void appendSystemLine(const QString &text);
    QString renderMessageBlocks(const QJsonArray &blocks) const;
    QString askQuestionHtml(const QJsonValue &args) const; // 把 ask_user_question 渲染成问题卡(不显示原始 JSON)
    QString userMessageHtml(const QString &text) const;
    QString assistantMessageHtml(const QString &innerHtml) const;
    QString toolCardHtml(const QString &toolName, const QString &argsText, const QString &resultText) const;
    QString systemLineHtml(const QString &text) const;
    static QString extractText(const QJsonObject &message);
    static QString prettyJson(const QJsonValue &value);
    static QString escapeHtml(QString text);
    void setStatus(const QString &text, const QString &color);
    void updateSessionNode(const QString &sessionId);
    void updateUiEnabled();  // 依据连接/会话选中状态启用或禁用输入框、发送、显示框等
    void updateSendButton(); // 依据会话运行状态切换“发送/停止”按钮(与官方客户端一致)
    void updateSessionTitle();  // 依据是否运行中 + 子会话状态, 刷新顶部会话标题(追加“/子会话”)
    void refreshSubagentState(); // 根据 session.list 维护“当前会话是否有运行中的子会话”
    void clearMessageArea(); // 清空消息显示框与问题索引
    void scrollMessageToBottom(); // 新消息加入后,把消息显示框滚动到底部(内容高更新后拉到最大值)
    void addBubble(const QString &html, bool right, bool trackUser = true); // 气泡;trackUser 是否记入跳转列表
    void addSystemLine(const QString &text);         // 添加一条系统灰字
    void addContextCard(const QString &text);        // 添加上下文注入卡片
    void addAttachmentChip(const QString &name, int index); // 添加一个图片附件 chip(i=对应 m_attachments 序号)
    void rebuildAttachmentChips();                    // 依据当前 m_attachments 重建整个 chip 行
    void removeAttachmentAt(int index);               // 删除第 index 个附件(移除数据并重建 chip 行)
    void clearAttachments();                          // 清空待发送附件与 chip
    void addToolCallWidget(const QString &name, const QString &file, const QString &args); // 可展开的工具参数卡
    void addThinkBlock(const QString &text);         // 可展开的思考块(点击展开/折叠)
    void loadTitleCache();   // 启动时加载会话标题本地缓存
    void saveTitleCache();   // 标题变化时写回本地缓存
    void loadArchivedCache(); // 启动时加载本地归档集合
    void saveArchivedCache(); // 归档变化时写回本地集合
    bool isArchived(const QString &sessionId) const; // 是否归档(服务端 + 本地兜底)
    void queueTitleFetch();  // 树加载后,为无标题会话排队拉取名称
    void startNextTitleFetch(); // 启动下一个名称拉取(串行,防卡顿)
    void applyDarkStyle(QWidget *w); // 为对话框应用深色主题
    void showDropWarning(const QString &text); // 深色提示框(拖入文件相关, 避免白字看不清)
    QString promptForName(const QString &caption, const QString &initial); // 弹窗输入名称(取消返回空)
    void createSessionInWorkspace(const QString &workspaceId); // 弹窗输入名称并新建会话到指定工作区
    void loadPresets();  // 拉取 Agent 预设列表(供新建会话下拉/名称旁显示)
    QString presetName(const QString &id) const; // 预设 id → 显示名
    void updatePresetLabel(); // 更新会话名称旁的 Agent 预设标签(只读)
    void updatePermissionCombo(); // 从 permissions 投影填充会话权限下拉(显示默认值)
    void showQuestionPanel(const QString &rpcId, const QString &sessionId, const QJsonArray &questions);
    void displayCurrentQuestion(); // 显示 m_qQuestions[m_qIndex] 一题
    void submitAnswer(const QString &selectedLabel, const QString &customText); // 提交当前题并前进/结束
    void clearQuestionPanel();
    // 系统托盘：任务完成后在操作系统右下角弹出提示（不阻塞）
    void notifyTaskComplete(const QString &title, const QString &message);
    // 可折叠的任务进度面板(todo/write 事件驱动的整表快照)
    void buildTodoDock();            // 构建进度面板 UI(默认折叠, 空表时隐藏)
    void onTodoWrite(const QJsonArray &todos); // 收到 todo/write → 保存并刷新面板
    void updateTodoDock();           // 刷新折叠标题汇总 + 展开列表内容
    void toggleTodoDock();           // 折叠/展开切换
    void clearTodoDock();            // 清空进度(turn/start 或清空消息区时)
    // 行内审批按钮条
    void addApprovalInline(const QString &rpcId, const QString &sessionId,
                           const QString &approvalId, const QString &toolName, const QString &reason);
    void settleApproval(const QString &approvalId, const QString &outcome); // 处理后更新按钮条为已处理

    CDSHCommander *m_commander = nullptr;
    QString m_currentSessionId;
    QString m_pendingOpenSession; // 新建会话成功后待打开的会话(等树加载完成)
    bool m_connected = false;
    QString m_tokenUrl;   // 本次连接使用的带 token 登录网址(空 = 未走 token 登录)

    // 树加载(并行):workspace.follow 流 + session.list, 两者就绪后重建
    QString m_wsRpcId;
    QString m_sessRpcId;
    bool m_wsLoaded = false;      // workspace.follow 基线是否已到
    bool m_sessionsLoaded = false; // session.list 是否已完成
    QJsonArray m_wsItems;      // workspace.list 结果(items)
    QJsonObject m_sessionMap;  // sessionId → sessionSummary
    QSet<QString> m_archivedSessionIds; // 归档会话(树中不显示)

    // 等待结果的 rpcId(用于区分是哪条命令返回)
    QString m_createRpcId;
    QString m_modelsRpcId;
    QString m_historyRpcId;
    QString m_promptRpcId;
    QString m_cancelRpcId;     // session.cancel 的 rpcId(用于追踪取消命令完成)
    QString m_selectRpcId;
    QString m_archiveRpcId;
    QString m_createWsRpcId;   // workspace.create
    QString m_renameWsRpcId;   // workspace.rename(设置用户填写的名称)
    QString m_pendingWsName;   // 待设置的工作区名称
    QString m_pendingWsId;     // 待重命名的工作区 id
    QString m_deleteWsRpcId;   // workspace.delete(删除工作区)
    QString m_renameSessionRpcId; // session.rename(修改会话名称)
    QString m_pendingSessionName; // 新建会话时待设置的会话名称
    QString m_presetsRpcId;       // agentPreset.list 的 rpcId
    QList<QPair<QString, QString>> m_presets; // Agent 预设 {id, 显示名}
    QString m_defaultPresetId;    // DSH 默认预设 id(isDefault=true)

    QLineEdit *m_serverEdit = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
    QTreeWidget *m_sessionTree = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_presetLabel = nullptr; // 会话名称旁的 Agent 预设标签(只读)
    QComboBox *m_permissionCombo = nullptr; // 会话权限(Read Only / Workspace Write / Full access)
    QComboBox *m_modelCombo = nullptr;
    QScrollArea *m_messageScroll = nullptr; // 消息区(可放控件的容器)
    QWidget *m_messageHost = nullptr;
    QVBoxLayout *m_messageLayout = nullptr;
    QList<QWidget *> m_userBubbles; // 每条用户消息的气泡 widget(索引跳转用)
    CDSDropTextEdit *m_inputEdit = nullptr; // 输入框(支持拖入文件 → 图片附件)
    QPushButton *m_sendBtn = nullptr;
    QPushButton *m_exportBtn = nullptr;  // 右上角“下载 PDF”按钮
    QListWidget *m_questionList = nullptr; // 消息区右侧的问题记录表(我的提问)
    QLabel *m_thinkLabel = nullptr;        // 思考提示行(进行中显示最后一行,完成后隐藏)
    QString m_thinking;                    // 累积的思考增量文本

    // 可折叠的任务进度面板(todo/write 事件驱动; 官方 TodoDock 的等价实现)
    QWidget *m_todoDock = nullptr;         // 面板容器(消息显示框下方, 输入区上方)
    QToolButton *m_todoToggle = nullptr;   // 可点击折叠头(标题 + 进度汇总 + 折叠箭头)
    QListWidget *m_todoList = nullptr;     // 展开后的进度列表
    bool m_todoCollapsed = true;           // 是否处于折叠态(默认折叠)
    QList<QPair<QString, int>> m_todos;    // 当前任务进度 {content, status(0待办/1进行中/2完成)}

    // 待处理审批: approvalId → 行内按钮条 QWidget(便于收到 approval/resolved 时更新)
    QList<QPair<QString, QWidget *>> m_pendingApprovals;

    // 图片附件(拖入输入框, DSH 附件协议)
    QWidget *m_attachBar = nullptr;        // 附件区(在输入框上方, 有附件时显示)
    QHBoxLayout *m_attachLayout = nullptr; // 附件 chip 行布局
    QList<DSHImageAttachment> m_attachments; // 待发送的图片附件

    // 提问交互面板(收到 question/requested 时显示,用户回答后 respond 回传)
    QWidget *m_qPanel = nullptr;
    QLabel *m_qHeader = nullptr;
    QLabel *m_qText = nullptr;
    QVBoxLayout *m_qOptions = nullptr;
    QLineEdit *m_qEdit = nullptr;
    QPushButton *m_qSubmit = nullptr;
    QPushButton *m_qSkip = nullptr;
    QString m_qRpcId;
    QString m_qSessionId;
    QJsonArray m_qQuestions;
    int m_qIndex = 0;
    QJsonArray m_qAnswers; // 多题时累积的答案

    QHash<QString, QString> m_sessionTitles; // sessionId → 标题
    QHash<QString, bool> m_sessionRunning;   // sessionId → 是否运行中
    // 子会话显示: 顶部会话标题在“当前会话有运行中的子会话”时显示为 “会话名称 / 子会话”,
    // 子会话结束后恢复为纯会话名称。
    bool m_subagentRunning = false;         // 当前会话是否有运行中的子会话
    QString m_baseSessionTitle;             // 当前会话的纯标题(不含“/子会话”后缀)
    QString m_titleCachePath; // 会话标题本地缓存文件路径(exe 目录/Resource/session_titles.json)
    QSet<QString> m_archivedLocal; // 本地记录的归档会话(兜底,即使服务端不返回也过滤)
    QString m_archivedCachePath;   // 本地归档集合文件路径
    QString m_archiveSession;      // 当前正在归档的会话(关联 rpcId)
    bool m_sessionTitlesChanged = false; // 本次 session.list 是否新增了名称缓存
    QStringList m_titleFetchQueue; // 待拉取名称的会话队列
    bool m_titleFetchBusy = false; // 是否正在拉取(串行)
    QString m_titleHistoryRpcId;   // 名称拉取对应的 session.history rpcId
    QString m_titleFetchSession;   // 当前正在拉取名称的会话
};

#endif // DSHCHATWINDOW_H
