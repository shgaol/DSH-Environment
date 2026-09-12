#include "DSHCommander.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUuid>
#include <QVariant>
#include <QtWebSockets/QWebSocket>

CDSHCommander::CDSHCommander(QObject *parent)
    : QObject(parent)
{
    // HTTP 命令通道:传输层超时 30 秒(与 DSH 客户端默认一致),不需要定时器
    m_http = new QNetworkAccessManager(this);
    m_http->setTransferTimeout(30000);
    // 本客户端只连接本机 127.0.0.1 的 DSH 服务, 绕过系统代理避免被拦截/转发挂起。
    m_http->setProxy(QNetworkProxy::NoProxy);

    // 事件流: 单条 /api/remote.mux 多路复用下行流(发起 $events 逻辑流后收事件帧)
    m_netSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);

    connect(m_netSocket, &QWebSocket::connected, this, &CDSHCommander::onNetConnected);
    connect(m_netSocket, &QWebSocket::disconnected, this, &CDSHCommander::onNetDisconnected);
    connect(m_netSocket, &QWebSocket::textMessageReceived, this, &CDSHCommander::onNetTextMessage);
}

CDSHCommander::~CDSHCommander() = default;

void CDSHCommander::setServer(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
}

void CDSHCommander::setLoginUrl(const QString &loginUrl)
{
    m_loginUrl = loginUrl.trimmed();
}

bool CDSHCommander::hasLoginUrl() const
{
    return !m_loginUrl.isEmpty();
}

QString CDSHCommander::serverUrl() const
{
    return QStringLiteral("http://%1:%2").arg(m_host).arg(m_port);
}

QString CDSHCommander::wsUrl(const QString &path) const
{
    return QStringLiteral("ws://%1:%2%3").arg(m_host).arg(m_port).arg(path);
}

bool CDSHCommander::isConnected() const
{
    return m_connected;
}

void CDSHCommander::connectToServer()
{
    // 已有连接或正在连接则忽略(不自动重连,避免重复发起)
    if (m_netSocket->state() != QAbstractSocket::UnconnectedState) {
        return;
    }
    emit connectionChanged(false, QStringLiteral("正在连接 %1:%2 ...").arg(m_host).arg(m_port));
    // 设置了带 token 的登录网址 → 先做 token→cookie 交换再开事件流; 否则直连。
    if (!m_loginUrl.isEmpty()) {
        performTokenLogin();
        return;
    }
    openWebSockets();
}

// 向带 token 的网址发一次 GET, 读取 Set-Cookie 完成登录; 成功后打开事件流。
void CDSHCommander::performTokenLogin()
{
    emit connectionChanged(false, QStringLiteral("登录中(交换 cookie)..."));
    const QUrl loginUrl(m_loginUrl);   // 先构造 QUrl, 避免 QNetworkRequest request(QUrl(member)) 被当成函数声明
    QNetworkRequest request(loginUrl);
    // 不自动跟随 303, 以便读取 Set-Cookie
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply *reply = m_http->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray rawCookie = reply->rawHeader(QByteArrayLiteral("set-cookie"));
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 || rawCookie.isEmpty()) {
            emit errorOccurred(QStringLiteral("登录失败: HTTP %1 未返回登录 cookie(可能是端口/服务不对)").arg(status));
            emit connectionChanged(false, QStringLiteral("登录失败(HTTP %1)").arg(status));
            return;
        }
        // 取第一个 Set-Cookie 的 "name=value" 部分(去掉属性), 作为后续 Cookie 头。
        const QList<QNetworkCookie> cookies = QNetworkCookie::parseCookies(rawCookie);
        m_cookieHeader.clear();
        if (!cookies.isEmpty()) {
            const QNetworkCookie &c = cookies.first();
            m_cookieHeader = QStringLiteral("%1=%2")
                .arg(QString::fromLatin1(c.name()), QString::fromLatin1(c.value()));
        }
        if (m_cookieHeader.isEmpty()) {
            emit errorOccurred(QStringLiteral("登录失败: 未解析到登录 cookie"));
            emit connectionChanged(false, QStringLiteral("登录失败(无 cookie)"));
            return;
        }
        // 登录成功 → 再打开两个事件流(带 cookie)
        emit connectionChanged(false, QStringLiteral("登录成功, 连接事件流..."));
        openWebSockets();
    });
}

// 打开 /api/remote.mux 多路复用事件流(带登录 cookie), 连接后由 onNetConnected 打开 $events 逻辑流。
void CDSHCommander::openWebSockets()
{
    m_netOpen = false;
    QNetworkRequest req(QUrl(wsUrl(QStringLiteral("/api/remote.mux"))));
    if (!m_cookieHeader.isEmpty())
        req.setRawHeader(QByteArrayLiteral("Cookie"), m_cookieHeader.toUtf8());
    m_netSocket->open(req);
}

// 打开一个会话的 session.follow 流(只跟随当前)。先取消上一次跟随, 再打开新的。
void CDSHCommander::followSession(const QString &sessionId)
{
    if (!m_followStreamId.isEmpty() && m_netSocket->state() == QAbstractSocket::ConnectedState) {
        QJsonObject cancel;
        cancel.insert(QLatin1String("type"), QStringLiteral("cancel"));
        cancel.insert(QLatin1String("streamId"), m_followStreamId);
        m_netSocket->sendTextMessage(QString::fromUtf8(QJsonDocument(cancel).toJson(QJsonDocument::Compact)));
    }
    m_followSessionId = sessionId;
    m_followStreamId = QStringLiteral("fs-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject open;
    open.insert(QLatin1String("type"), QStringLiteral("open"));
    open.insert(QLatin1String("streamId"), m_followStreamId);
    open.insert(QLatin1String("endpoint"), QStringLiteral("session/follow"));
    QJsonObject args;
    args.insert(QLatin1String("address"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("session")},
                                                      {QStringLiteral("sessionId"), sessionId}});
    args.insert(QLatin1String("maxMessages"), 100);
    // session.follow 参数名为 request: args 需包裹为 {request: {...}}
    open.insert(QLatin1String("payload"), QJsonObject{{QStringLiteral("args"),
        QJsonObject{{QStringLiteral("request"), args}}}});
    if (m_netSocket->state() == QAbstractSocket::ConnectedState)
        m_netSocket->sendTextMessage(QString::fromUtf8(QJsonDocument(open).toJson(QJsonDocument::Compact)));
}

void CDSHCommander::disconnectFromServer()
{
    m_netSocket->close();
    m_netOpen = false;
    m_connected = false;
    emit connectionChanged(false, QStringLiteral("已断开"));
}

void CDSHCommander::onNetConnected()
{
    m_netOpen = true;
    // 打开转发事件流($events): 之后服务端会下发 ready/emit/waterfall/cancel 帧
    auto sendOpen = [this](const QString &streamId, const QString &endpoint) {
        QJsonObject open;
        open.insert(QLatin1String("type"), QStringLiteral("open"));
        open.insert(QLatin1String("streamId"), streamId);
        open.insert(QLatin1String("endpoint"), endpoint);
        open.insert(QLatin1String("payload"), QJsonObject{{QStringLiteral("args"), QJsonObject{}}});
        m_netSocket->sendTextMessage(QString::fromUtf8(QJsonDocument(open).toJson(QJsonDocument::Compact)));
    };
    sendOpen(m_eventsStreamId, QStringLiteral("$events"));
    sendOpen(m_workspaceStreamId, QStringLiteral("workspace/follow")); // 流端点用斜杠 namespace/method
    updateConnectionState();
}

void CDSHCommander::onNetDisconnected()
{
    m_netOpen = false;
    updateConnectionState();
}

void CDSHCommander::onNetTextMessage(const QString &message)
{
    // mux 帧: {type:'item'|'end'|'error', streamId, value/error}
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        emit errorOccurred(QStringLiteral("事件流:JSON 解析失败"));
        return;
    }
    handleStreamFrame(doc.object());
}

void CDSHCommander::handleStreamFrame(const QJsonObject &frame)
{
    const QString type = frame.value(QLatin1String("type")).toString();
    const QString streamId = frame.value(QLatin1String("streamId")).toString();
    if (type == QLatin1String("item")) {
        const QJsonObject value = frame.value(QLatin1String("value")).toObject();
        // workspace.follow 流 → 交给界面维护工作区树
        if (streamId == m_workspaceStreamId) {
            emit workspaceFrame(value);
        } else if (!m_followStreamId.isEmpty() && streamId == m_followStreamId) {
            handleFollowItem(value);   // 当前会话的 session.follow 流
        } else {
            handleStreamItem(value);   // $events 流
        }
    } else if (type == QLatin1String("end")) {
        // 事件流结束: 忽略
    } else if (type == QLatin1String("error")) {
        // 事件流错误: 忽略
    }
}

// 处理 $events 逻辑流里的一项(item.value)。
void CDSHCommander::handleStreamItem(const QJsonObject &value)
{
    const QString type = value.value(QLatin1String("type")).toString();
    if (type == QLatin1String("ready")) {
        return; // 已连接状态由 onNetConnected/updateConnectionState 维护
    }
    if (type == QLatin1String("emit")) {
        return;
    }
    // waterfall / cancel 及其它帧: 暂不处理
}

// session.follow 流帧: 快照(历史) + 实时事件/块运行。
void CDSHCommander::handleFollowItem(const QJsonObject &value)
{
    const QString type = value.value(QLatin1String("type")).toString();
    if (type == QLatin1String("snapshot")) {
        // 历史: 直接把 records(每条含 event 字段)交给界面渲染
        const QJsonArray records = value.value(QLatin1String("records")).toArray();
        emit sessionSnapshot(m_followSessionId, records);
        return;
    }
    // 实时事件帧: {type:'event'|'chunks', event:{type,seq,time,data}}
    if (value.value(QLatin1String("event")).isObject())
        emit sessionEventReceived(m_followSessionId, value.value(QLatin1String("event")).toObject());
}

void CDSHCommander::updateConnectionState()
{
    const bool now = m_netOpen;
    if (now == m_connected)
        return;
    m_connected = now;
    emit connectionChanged(now, now
        ? QStringLiteral("已连接 %1:%2").arg(m_host).arg(m_port)
        : QStringLiteral("事件流已断开"));
}

QString CDSHCommander::mintRpcId() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString CDSHCommander::sendCommand(const QString &method, const QJsonObject &payload)
{
    const QString rpcId = mintRpcId();

    QJsonObject envelope;
    envelope.insert(QLatin1String("type"), QStringLiteral("client-request"));
    envelope.insert(QLatin1String("rpcId"), rpcId);
    // 现代 DSH: 端点用斜杠 namespace/method; payload 需包裹为 {args: ...}。
    // 带参方法用单一 request 参数(如 follow(request)): 非空 payload 包裹成 {args:{request:...}}。
    QString wireMethod = method;
    wireMethod.replace(QLatin1Char('.'), QLatin1Char('/'));
    envelope.insert(QLatin1String("method"), wireMethod);
    QJsonObject wrapped;
    if (payload.isEmpty()) {
        wrapped.insert(QLatin1String("args"), QJsonObject{});
    } else {
        QJsonObject req;
        req.insert(QLatin1String("request"), payload);
        wrapped.insert(QLatin1String("args"), req);
    }
    envelope.insert(QLatin1String("payload"), wrapped);

    QNetworkRequest request(QUrl(serverUrl() + QStringLiteral("/api/") + wireMethod));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_cookieHeader.isEmpty())
        request.setRawHeader(QByteArrayLiteral("Cookie"), m_cookieHeader.toUtf8());

    QNetworkReply *reply = m_http->post(request, QJsonDocument(envelope).toJson(QJsonDocument::Compact));

    connect(reply, &QNetworkReply::finished, this, [this, reply, rpcId, method]() {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        auto makeError = [](const QString &code, const QString &message) {
            QJsonObject error;
            error.insert(QLatin1String("code"), code);
            error.insert(QLatin1String("message"), message);
            error.insert(QLatin1String("details"), QJsonObject());
            return error;
        };

        // 传输层失败(超时/断网/非 2xx)
        if (reply->error() != QNetworkReply::NoError || status < 200 || status >= 300) {
            const QString msg = QStringLiteral("%1:HTTP %2 %3")
                .arg(method).arg(status).arg(reply->errorString());
            emit errorOccurred(msg);
            emit commandFinished(rpcId, false, makeError(QStringLiteral("http-%1").arg(status), msg));
            return;
        }

        // 响应必须是合法 JSON
        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(body, &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            const QString msg = QStringLiteral("%1:响应不是合法 JSON").arg(method);
            emit errorOccurred(msg);
            emit commandFinished(rpcId, false, makeError(QStringLiteral("bad-response"), msg));
            return;
        }

        const QJsonObject full = doc.object();
        if (full.value(QLatin1String("type")).toString() != QStringLiteral("server-response")
            || full.value(QLatin1String("rpcId")).toString() != rpcId) {
            const QString msg = QStringLiteral("%1:rpcId 不匹配或响应类型错误").arg(method);
            emit errorOccurred(msg);
            emit commandFinished(rpcId, false, makeError(QStringLiteral("bad-response"), msg));
            return;
        }

        const QJsonObject result = full.value(QLatin1String("result")).toObject();
        const bool ok = result.value(QLatin1String("ok")).toBool();
        if (ok) {
            emit commandFinished(rpcId, true, result.value(QLatin1String("value")).toObject());
        } else {
            const QJsonObject error = result.value(QLatin1String("error")).toObject();
            emit errorOccurred(error.value(QLatin1String("message")).toString());
            emit commandFinished(rpcId, false, error);
        }
    });

    return rpcId;
}

QString CDSHCommander::listSessions()
{
    return sendCommand(QStringLiteral("session.list"), QJsonObject());
}

QString CDSHCommander::createSession(const QString &workspaceId, const QString &cwd, const QString &agentPreset)
{
    QJsonObject payload;
    if (!workspaceId.isEmpty())
        payload.insert(QLatin1String("workspaceId"), workspaceId);
    else if (!cwd.isEmpty())
        payload.insert(QLatin1String("cwd"), cwd);
    if (!agentPreset.isEmpty())
        payload.insert(QLatin1String("agentPreset"), agentPreset);
    return sendCommand(QStringLiteral("session.create"), payload);
}

QString CDSHCommander::listAgentPresets()
{
    // 现代 DSH: 端点命名空间为 agentPresets(复数)
    return sendCommand(QStringLiteral("agentPresets.list"), QJsonObject());
}

QString CDSHCommander::settingsDescribe()
{
    return sendCommand(QStringLiteral("settings.describe"), QJsonObject());
}

QString CDSHCommander::settingsUpdate(const QString &ns, const QJsonObject &patch)
{
    QJsonObject payload;
    payload.insert(QLatin1String("ns"), ns);
    payload.insert(QLatin1String("patch"), patch);
    return sendCommand(QStringLiteral("settings.update"), payload);
}

QString CDSHCommander::settingsMutate(const QString &ns, const QJsonArray &ops)
{
    QJsonObject payload;
    payload.insert(QLatin1String("ns"), ns);
    payload.insert(QLatin1String("ops"), ops);
    return sendCommand(QStringLiteral("settings.mutate"), payload);
}

void CDSHCommander::respond(const QString &rpcId, const QJsonObject &resultValue)
{
    QJsonObject envelope;
    envelope.insert(QLatin1String("type"), QStringLiteral("client-response"));
    envelope.insert(QLatin1String("rpcId"), rpcId);
    QJsonObject result;
    result.insert(QLatin1String("ok"), true);
    result.insert(QLatin1String("value"), resultValue);
    envelope.insert(QLatin1String("result"), result);

    QNetworkRequest request(QUrl(serverUrl() + QStringLiteral("/api/respond")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_cookieHeader.isEmpty())
        request.setRawHeader(QByteArrayLiteral("Cookie"), m_cookieHeader.toUtf8());
    QNetworkReply *reply = m_http->post(request, QJsonDocument(envelope).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            emit errorOccurred(QStringLiteral("提交回答失败:%1").arg(reply->errorString()));
    });
}

// 应答一次审批: 通过 /api/respond 回传 client-response(回显 rpcId)。
// payload 遵循 approval 应答契约 { sessionId, approvalId, outcome }。
void CDSHCommander::approve(const QString &rpcId, const QString &sessionId,
                            const QString &approvalId, const QString &outcome)
{
    QJsonObject value;
    value.insert(QLatin1String("sessionId"), sessionId);
    value.insert(QLatin1String("approvalId"), approvalId);
    value.insert(QLatin1String("outcome"), outcome); // allowed-once | rejected
    respond(rpcId, value);
}

QString CDSHCommander::prompt(const QString &sessionId, const QString &text)
{
    QJsonObject payload;
    // SessionPromptRequest 需要客户端生成的 requestId(用户消息持久化身份)
    payload.insert(QLatin1String("requestId"),
                   QUuid::createUuid().toString(QUuid::WithoutBraces));
    payload.insert(QLatin1String("sessionId"), sessionId);
    payload.insert(QLatin1String("mode"), QStringLiteral("queue"));
    QJsonObject part;
    part.insert(QLatin1String("type"), QStringLiteral("text"));
    part.insert(QLatin1String("text"), text);
    QJsonArray content;
    content.append(part);
    payload.insert(QLatin1String("content"), content);
    return sendCommand(QStringLiteral("session.prompt"), payload);
}

QString CDSHCommander::promptWithImages(const QString &sessionId, const QString &text,
                                        const QList<DSHImageAttachment> &images)
{
    QJsonObject payload;
    payload.insert(QLatin1String("requestId"),
                   QUuid::createUuid().toString(QUuid::WithoutBraces));
    payload.insert(QLatin1String("sessionId"), sessionId);
    payload.insert(QLatin1String("mode"), QStringLiteral("queue"));
    QJsonArray content;
    // 图片块在前(与 DSH 客户端一致), 文本块在后
    for (const DSHImageAttachment &img : images) {
        QJsonObject part;
        part.insert(QLatin1String("type"), QStringLiteral("image"));
        part.insert(QLatin1String("mediaType"), img.mediaType);
        part.insert(QLatin1String("data"), img.dataBase64);
        if (!img.name.isEmpty())
            part.insert(QLatin1String("name"), img.name);
        content.append(part);
    }
    if (!text.isEmpty()) {
        QJsonObject part;
        part.insert(QLatin1String("type"), QStringLiteral("text"));
        part.insert(QLatin1String("text"), text);
        content.append(part);
    }
    payload.insert(QLatin1String("content"), content);
    return sendCommand(QStringLiteral("session.prompt"), payload);
}

QString CDSHCommander::cancelSession(const QString &sessionId)
{
    QJsonObject payload;
    payload.insert(QLatin1String("sessionId"), sessionId);
    return sendCommand(QStringLiteral("session.cancel"), payload);
}

QString CDSHCommander::archiveSession(const QString &sessionId)
{
    QJsonObject payload;
    payload.insert(QLatin1String("sessionId"), sessionId);
    return sendCommand(QStringLiteral("workspace.archiveSession"), payload);
}

QString CDSHCommander::sessionHistory(const QString &sessionId, int beforeSeq, int maxMessages)
{
    QJsonObject payload;
    payload.insert(QLatin1String("sessionId"), sessionId);
    if (beforeSeq >= 0)
        payload.insert(QLatin1String("beforeSeq"), beforeSeq);
    if (maxMessages > 0)
        payload.insert(QLatin1String("maxMessages"), maxMessages);
    return sendCommand(QStringLiteral("session.history"), payload);
}

QString CDSHCommander::sessionModels(const QString &sessionId)
{
    Q_UNUSED(sessionId);
    // 现代 DSH: 模型目录是全局的 session.modelCatalog(非按会话的 session.models)
    return sendCommand(QStringLiteral("session.modelCatalog"), QJsonObject());
}

QString CDSHCommander::selectModel(const QString &sessionId, const QString &provider, const QString &model)
{
    QJsonObject payload;
    payload.insert(QLatin1String("sessionId"), sessionId);
    payload.insert(QLatin1String("provider"), provider);
    payload.insert(QLatin1String("model"), model);
    return sendCommand(QStringLiteral("session.selectModel"), payload);
}

QString CDSHCommander::listWorkspaces()
{
    return sendCommand(QStringLiteral("workspace.list"), QJsonObject());
}

QString CDSHCommander::createWorkspace(const QString &path)
{
    QJsonObject payload;
    payload.insert(QLatin1String("path"), path);
    return sendCommand(QStringLiteral("workspace.create"), payload);
}

QString CDSHCommander::renameWorkspace(const QString &workspaceId, const QString &title)
{
    QJsonObject payload;
    payload.insert(QLatin1String("workspaceId"), workspaceId);
    payload.insert(QLatin1String("title"), title);
    return sendCommand(QStringLiteral("workspace.rename"), payload);
}

QString CDSHCommander::deleteWorkspace(const QString &workspaceId)
{
    QJsonObject payload;
    payload.insert(QLatin1String("workspaceId"), workspaceId);
    return sendCommand(QStringLiteral("workspace.delete"), payload);
}

QString CDSHCommander::renameSession(const QString &sessionId, const QString &title)
{
    QJsonObject payload;
    payload.insert(QLatin1String("sessionId"), sessionId);
    payload.insert(QLatin1String("title"), title);
    return sendCommand(QStringLiteral("session.rename"), payload);
}

QString CDSHCommander::hostDescribe()
{
    return sendCommand(QStringLiteral("host.describe"), QJsonObject());
}
