#ifndef DSHCOMMANDER_H
#define DSHCOMMANDER_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QWebSocket;

// 图片附件(DSH 附件协议): 仅支持 png/jpeg/webp/gif, data 为标准 base64(含 '=' 填充)
struct DSHImageAttachment
{
    QString mediaType;   // image/png | image/jpeg | image/webp | image/gif
    QString dataBase64;  // 标准 base64
    QString name;        // 可选显示名
};

// CDSHCommander:与 DSH(DeepSeek Harness)服务的通信封装。
// 协议(来自 dsh-client-connection / dsh-host-apiproxy 源码):
//   1) 命令通道:HTTP POST /api/<method>,JSON-RPC 信封
//        {type:"client-request", rpcId, method, payload}
//      响应  {type:"server-response", rpcId, result:{ok:true,value} | {ok:false,error}}
//   2) 事件通道:WebSocket 下行流(仅收不发,发送会被服务端以 1008 断开)
//        ws://host:port/api/events.mux   会话事件流
//        ws://host:port/api/events.host  主机事件流
//      帧格式 {type:"server-request", rpcId, method, payload}
// 说明:不自动重连、不使用定时器;断开后由界面手动调用 connectToServer() 重连。
class CDSHCommander : public QObject
{
    Q_OBJECT

public:
    explicit CDSHCommander(QObject *parent = nullptr);
    ~CDSHCommander() override;

    // 服务器地址(默认 127.0.0.1:8888)
    void setServer(const QString &host, quint16 port);
    QString serverUrl() const; // http://host:port
    QString wsUrl(const QString &path) const; // ws://host:port/path

    // 设置带 token 的登录网址(形如 http://host:port/?token=X)。
    // 连接时先向它发起一次 token→cookie 交换, 拿到登录 cookie 后再连 HTTP/WS;
    // 不设置则按原方式直连(不登录)。
    void setLoginUrl(const QString &loginUrl);
    bool hasLoginUrl() const; // 是否设置了 token 登录网址

    bool isConnected() const; // 两个事件流都处于 Open 状态

    // 连接 / 断开(不自动重连)
    void connectToServer();
    void disconnectFromServer();
    // 打开一个会话的 session.follow 流(快照=历史, 之后实时事件)。只维护当前跟随的一个。
    void followSession(const QString &sessionId);

    // 发送一条 RPC 命令(异步),返回本次 rpcId;结果通过 commandFinished 信号返回
    QString sendCommand(const QString &method, const QJsonObject &payload);

    // ---- 常用命令便捷封装(全部异步,结果走 commandFinished)----
    QString listSessions();                          // session.list
    QString createSession(const QString &workspaceId = QString(), const QString &cwd = QString(),
                          const QString &agentPreset = QString()); // session.create(workspaceId/cwd 二选一,可带 Agent 预设)
    QString promptWithImages(const QString &sessionId, const QString &text,
                             const QList<DSHImageAttachment> &images); // session.prompt(带图片附件)
    QString listAgentPresets();                          // agentPreset.list
    QString settingsDescribe();                          // settings.describe
    QString settingsUpdate(const QString &ns, const QJsonObject &patch); // settings.update
    QString settingsMutate(const QString &ns, const QJsonArray &ops); // settings.mutate
    // 回答一个提问(client-response 回传,结果走 /api/respond)
    void respond(const QString &rpcId, const QJsonObject &resultValue);
    // 应答一次审批(client-response 回传; outcome = allowed-once | rejected)
    void approve(const QString &rpcId, const QString &sessionId, const QString &approvalId,
                 const QString &outcome);
    QString prompt(const QString &sessionId, const QString &text); // session.prompt(queue 模式)
    QString cancelSession(const QString &sessionId); // session.cancel
    QString archiveSession(const QString &sessionId); // workspace.archiveSession(归档会话)
    QString sessionHistory(const QString &sessionId, int beforeSeq = -1, int maxMessages = -1);
    QString sessionModels(const QString &sessionId); // session.models
    QString selectModel(const QString &sessionId, const QString &provider, const QString &model);
    QString listWorkspaces();                        // workspace.list
    QString createWorkspace(const QString &path);    // workspace.create(采纳已存在的目录)
    QString renameWorkspace(const QString &workspaceId, const QString &title); // workspace.rename
    QString deleteWorkspace(const QString &workspaceId); // workspace.delete
    QString renameSession(const QString &sessionId, const QString &title); // session.rename
    QString hostDescribe();                          // host.describe

signals:
    // 连接状态变化(connected=false 时 detail 为原因)
    void connectionChanged(bool connected, const QString &detail);
    // 单条命令完成:ok=true 时 result 为业务 value;ok=false 时 result 为错误对象
    // {code,message,details}
    void commandFinished(const QString &rpcId, bool ok, const QJsonObject &result);
    // 事件流帧:type 为帧类型(session/event、host/session-added、approval/requested 等)
    void muxEventReceived(const QString &type, const QJsonObject &payload);
    void hostEventReceived(const QString &type, const QJsonObject &payload);
    // 会话事件:event 为 {type,seq,time,data,...}
    void sessionEventReceived(const QString &sessionId, const QJsonObject &event);
    // 提问请求(question/requested 帧):需要用户回答,答案通过 respond() 回传
    void questionRequested(const QString &rpcId, const QString &sessionId, const QJsonArray &questions);
    // 审批请求(approval/requested 帧):需要用户批准/拒绝, 结果通过 approve() 回传(回显该 rpcId)
    void approvalRequested(const QString &rpcId, const QJsonObject &payload);
    void errorOccurred(const QString &message);
    // 工作区状态流帧(workspace.follow 的 baseline/upsert/remove/order/archived), 由界面维护树。
    void workspaceFrame(const QJsonObject &frame);
    // session.follow 快照: 打开会话的历史事件(已拍平成事件数组)。
    void sessionSnapshot(const QString &sessionId, const QJsonArray &events);

private slots:
    void onNetConnected();
    void onNetDisconnected();
    void onNetTextMessage(const QString &message);

private:
    void handleStreamFrame(const QJsonObject &frame);
    void handleStreamItem(const QJsonObject &value);
    void handleFollowItem(const QJsonObject &value); // session.follow 帧(快照/实时事件)
    void updateConnectionState();
    QString mintRpcId() const;
    // 用带 token 的网址做登录交换, 成功后打开事件流(openWebSockets)。
    void performTokenLogin();
    // 打开 /api/remote.mux 多路复用事件流(带登录 cookie), 并打开 $events 逻辑流。
    void openWebSockets();

    QString m_host = QStringLiteral("127.0.0.1");
    quint16 m_port = 8888;
    bool m_connected = false;
    bool m_netOpen = false;
    QString m_loginUrl;      // 带 token 的登录网址(空 = 不登录直连)
    QString m_cookieHeader;  // 登录 cookie(如 dsh-auth-xxx=yyy), 用于 HTTP/WS 鉴权

    QNetworkAccessManager *m_http = nullptr;   // HTTP 命令通道
    QWebSocket *m_netSocket = nullptr;         // /api/remote.mux 多路复用事件流
    QString m_eventsStreamId = QStringLiteral("evt");     // $events 逻辑流 id
    QString m_workspaceStreamId = QStringLiteral("ws");   // workspace.follow 逻辑流 id
    QString m_followStreamId;   // 当前会话的 session.follow 逻辑流 id(空=未跟随)
    QString m_followSessionId;  // 当前跟随的会话 id
};

#endif // DSHCOMMANDER_H
