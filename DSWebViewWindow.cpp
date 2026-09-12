#include "DSWebViewWindow.h"
#include "Application.h"     // CApplication::configDir()（配置目录：文档/DSH-Environment/configure）
#include "DSWebEnginePage.h" // 站外链接改由外部浏览器(Edge)打开（未启用 WebEngine 时为空）
#include "DSWebEngineView.h" // 右键菜单追加“使用默认浏览器打开链接”（未启用 WebEngine 时为空）

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QHash>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTextBrowser>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#ifdef DSH_HAVE_WEBENGINE
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVariant>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>
#include <QWebEngineView>
#endif
// Qt WebView 仅 Qt6 支持（Qt5 的 QWebView 是 QObject 控制器，无法嵌入 QWidget）
#if defined(DSH_HAVE_WEBVIEW) && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QWebView>
#endif

namespace {
// ---- 各类型窗口的站点信息（表驱动：新增一个外部网站只需加枚举值 + 这里一行）----
struct SiteInfo {
    CDSWebProfileKind kind;
    const char *title;        // 窗口 / MDI 子窗口标题（未指定标题时的兜底，同时作为去重标识）
    const char *profileName;  // WebEngine profile 存储名（同一进程内唯一）
    const char *dataDirName;  // 配置目录（文档/DSH-Environment/configure）下的数据目录名
    const char *internalHost; // 视为“站内”的域名后缀；站外链接交给 Edge（空串表示不适用）
};

// 站点信息表（表驱动）：
//   - DshService 是本机 DSH 服务窗口（共享 profile + token→cookie 交换）；
//   - DeepSeek / Toutiao / GitHub 是**内置站点 profile 预设**：原来各有导航栏按钮，
//     按钮已移除，改为在“网页小程序”里登记快捷方式打开；保留预设是为了让指向这些站点的
//     网页小程序沿用它们原有的 profile 与数据目录（登录状态不丢）：kindForUrl() 用这里的
//     internalHost 后缀把网址匹配回对应类型；
//   - WebApplet 是网页小程序自己的类型（其它网址共用它的 profile），
//     「站内域名后缀」由调用方按小程序网址给出（构造函数 internalHostSuffix），这里留空串。
const SiteInfo kSiteInfos[] = {
    {CDSWebProfileKind::DshService, "DSH 服务", "dsh-web", "", ""},
    {CDSWebProfileKind::DeepSeek, "DeepSeek", "dsh-deepseek", "deepseek-web", "deepseek.com"},
    {CDSWebProfileKind::Toutiao, "今日头条", "dsh-toutiao", "toutiao-web", "toutiao.com"},
    {CDSWebProfileKind::GitHub, "GitHub/shgaol", "dsh-github", "github-shgaol-web", "github.com"},
    {CDSWebProfileKind::WebApplet, "网页小程序", "dsh-webapplet", "webapplet-web", ""},
};

// 取某类型窗口的站点信息（未知类型兜底为 DSH 服务）
const SiteInfo &siteInfo(CDSWebProfileKind kind)
{
    for (const SiteInfo &info : kSiteInfos) {
        if (info.kind == kind) {
            return info;
        }
    }
    return kSiteInfos[0];
}

// 配置目录：文档/DSH-Environment/configure
// 与 CApplication::configDir() 使用同一规则；app 实例不可用时按同样规则兜底。
QString dshConfigureDir()
{
    if (CApplication *app = CApplication::instance()) {
        const QString dir = app->configDir();
        if (!dir.isEmpty()) {
            return dir;
        }
    }
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
           + QStringLiteral("/DSH-Environment/configure");
}

// 外部网站的专属数据目录：配置目录/<dataDirName>（如 configure/deepseek-web、configure/toutiao-web）
//   storage/ → cookie、localStorage、IndexedDB（登录状态就在这里）、站点权限
//   cache/   → HTTP 磁盘缓存
// 放在 configure 目录下的意义：与 Edge 等系统浏览器彼此独立，
// 清理 Edge 缓存/Cookie 不会影响这些窗口的登录状态；备份该目录即可迁移登录信息。
QString siteDataDir(CDSWebProfileKind kind)
{
    const QString name = QString::fromUtf8(siteInfo(kind).dataDirName);
    if (name.isEmpty()) {
        return QString(); // DshService 没有专属数据目录
    }
    return dshConfigureDir() + QLatin1Char('/') + name;
}
} // namespace

#ifdef DSH_HAVE_WEBENGINE
namespace {
// 应用级共享的持久化 profile(cookie store 从这里拿)，仅用于本机 DSH 服务网页
QWebEngineProfile *webProfile()
{
    static QWebEngineProfile *p = nullptr;
    if (!p) {
        p = new QWebEngineProfile(QStringLiteral("dsh-web"), QCoreApplication::instance());
        p->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    }
    return p;
}

// 外部网站专属 profile：所有持久化数据显式指向 文档/DSH-Environment/configure/<站点目录>，
// 不写入系统浏览器（Edge）的目录，因此清理 Edge 缓存不会导致登录信息丢失。
// 每个类型一个静态实例（同一类型的所有窗口共用同一个 profile / 登录态）。
QWebEngineProfile *siteProfile(CDSWebProfileKind kind)
{
    // 按枚举值作 key 缓存：同一类型的所有窗口共用同一个 profile / 登录态，
    // 新增 profile 类型时这里不需要改动。
    static QHash<int, QWebEngineProfile *> profiles;
    const int index = static_cast<int>(kind);
    if (QWebEngineProfile *existing = profiles.value(index, nullptr)) {
        return existing;
    }

    const SiteInfo &info = siteInfo(kind);
    const QString dir = siteDataDir(kind);
    if (dir.isEmpty()) {
        return nullptr; // DshService 没有专属目录（用共享 dsh-web profile），不会走到这里
    }
    // 目录须在使用前建好，否则 WebEngine 可能回退到默认数据目录
    QDir().mkpath(dir + QStringLiteral("/storage"));
    QDir().mkpath(dir + QStringLiteral("/cache"));

    auto *p = new QWebEngineProfile(QString::fromUtf8(info.profileName),
                                    QCoreApplication::instance());
    p->setPersistentStoragePath(dir + QStringLiteral("/storage")); // cookie/localStorage 等
    p->setCachePath(dir + QStringLiteral("/cache"));               // HTTP 磁盘缓存
    // 强制把会话 cookie 也落盘（登录态通常在会话 cookie 里），并允许磁盘缓存
    p->setPersistentCookiesPolicy(QWebEngineProfile::ForcePersistentCookies);
    p->setHttpCacheType(QWebEngineProfile::DiskHttpCache);
    // 站点权限（摄像头/麦克风/通知等）也持久化到 configure 目录
    p->setPersistentPermissionsPolicy(QWebEngineProfile::PersistentPermissionsPolicy::StoreOnDisk);

    // 去掉 UA 里的 "QtWebEngine/x.y.z" 标记：站点把它当作非标准浏览器时可能给降级页面
    // 或风控拦截。只删标记、保留真实 Chromium 版本，其余与默认 UA 一致。
    const QString ua = p->httpUserAgent();
    if (!ua.isEmpty()) {
        QString cleaned = ua;
        cleaned.remove(QRegularExpression(QStringLiteral("\\s*QtWebEngine/\\S+")));
        if (!cleaned.trimmed().isEmpty() && cleaned != ua) {
            p->setHttpUserAgent(cleaned);
        }
    }
    profiles.insert(index, p);
    return p;
}

// 用于完成 token→cookie 交换的本地 HTTP 客户端
QNetworkAccessManager *nam()
{
    static QNetworkAccessManager *m = nullptr;
    if (!m) {
        m = new QNetworkAccessManager(QCoreApplication::instance());
        // 本交换只访问本机 127.0.0.1 的 DSH 服务, 绕过系统代理避免被拦截/转发失败。
        m->setProxy(QNetworkProxy::NoProxy);
    }
    return m;
}
} // namespace
#endif

QString CDSWebViewWindow::defaultTitle(CDSWebProfileKind kind)
{
    return QString::fromUtf8(siteInfo(kind).title);
}

bool CDSWebViewWindow::isExternalSite(CDSWebProfileKind kind)
{
    // 除本机 DSH 服务窗口外的都是“外部网站窗口”：专属 profile（数据落在 configure 目录）、
    // 站外链接交给 Edge 打开、工具栏带“登录数据”按钮。
    // 网页小程序（WebApplet）与 DeepSeek / 今日头条 / GitHub 走的是同一套行为，
    // 区别只在“站内域名后缀”按小程序网址动态给出。
    return kind != CDSWebProfileKind::DshService;
}

CDSWebProfileKind CDSWebViewWindow::kindForUrl(const QString &url)
{
    const QString host = QUrl(url).host().toLower();
    if (!host.isEmpty()) {
        // 用站点信息表里的“站内域名后缀”做匹配（与 CDSWebEnginePage 判定站内/站外同一规则）：
        // 同一个后缀下的窗口共用同一个 profile，因此登录信息与缓存也是同一份。
        for (const SiteInfo &info : kSiteInfos) {
            if (info.kind == CDSWebProfileKind::DshService) {
                continue; // 本机 DSH 服务窗口不走网址匹配
            }
            const QString suffix = QString::fromUtf8(info.internalHost).toLower();
            if (suffix.isEmpty()) {
                continue; // 网页小程序本身没有固定后缀
            }
            if (host == suffix || host.endsWith(QLatin1Char('.') + suffix)) {
                return info.kind;
            }
        }
    }
    return CDSWebProfileKind::WebApplet; // 其它网址：用网页小程序自己的 profile
}

#ifdef DSH_HAVE_WEBENGINE
QWebEngineProfile *CDSWebViewWindow::profileFor(CDSWebProfileKind kind)
{
    return isExternalSite(kind) ? siteProfile(kind) : webProfile();
}
#endif

CDSWebViewWindow::CDSWebViewWindow(CDSWebProfileKind kind, QWidget *parent,
                                   const QString &internalHostSuffix)
    : QMainWindow(parent)
    , m_kind(kind)
{
    const bool externalSite = isExternalSite(m_kind);
    // “站内”域名后缀：调用方给了就用它（网页小程序按小程序网址给出），
    // 否则用该类型在站点信息表里的配置（DeepSeek = deepseek.com 等）。
    m_internalHostSuffix = internalHostSuffix.isEmpty()
        ? QString::fromUtf8(siteInfo(m_kind).internalHost)
        : internalHostSuffix;
    setWindowTitle(defaultTitle(m_kind));

    // 中央内容：顶部地址栏(像 Edge) + 网页视图
    auto *central = new QWidget(this);
    auto *v = new QVBoxLayout(central);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto *addrRow = new QHBoxLayout;
    addrRow->setContentsMargins(4, 4, 4, 4);
    addrRow->setSpacing(4);
    m_addrEdit = new QLineEdit(central);
    m_addrEdit->setPlaceholderText(QStringLiteral("输入网址后回车"));
    m_addrEdit->setClearButtonEnabled(true);
    addrRow->addWidget(m_addrEdit, 1);
    auto *refreshBtn = new QToolButton(central);
    refreshBtn->setText(QStringLiteral("刷新"));
    refreshBtn->setAutoRaise(true);
    addrRow->addWidget(refreshBtn);
    // 外部网站窗口：提供“登录数据”按钮，直接打开 configure 下的数据目录，
    // 便于查看/备份登录信息（备份该目录即可迁移登录状态）。
    if (externalSite) {
        m_dataDirBtn = new QToolButton(central);
        m_dataDirBtn->setText(QStringLiteral("登录数据"));
        m_dataDirBtn->setAutoRaise(true);
        m_dataDirBtn->setToolTip(
            QStringLiteral("登录信息（cookie 等）保存在：\n%1\n\n"
                           "该目录独立于 Edge：清理 Edge 缓存不会影响这里的登录状态；\n"
                           "备份此目录即可迁移/恢复登录信息。")
                .arg(siteDataDir(m_kind)));
        addrRow->addWidget(m_dataDirBtn);
    }
    v->addLayout(addrRow);

#ifdef DSH_HAVE_WEBENGINE
    // 网页显示用 QWebEngineView（标准 Qt5/Qt6 方式）
    if (externalSite) {
        // 外部网站（含网页小程序）：专属持久化 profile（数据落在 configure 目录）
        // + CDSWebEnginePage（站外链接交给外部浏览器 Edge 打开，站内链接仍在内嵌窗口内导航）
        // + CDSWebEngineView（右键菜单追加“使用默认浏览器打开链接”）。
        // 视图先建、页对象以视图为父对象（setPage 不接管所有权，
        // 靠 QObject 父子关系随视图一起销毁）。
        auto *view = new CDSWebEngineView(central);
        view->setPage(new CDSWebEnginePage(
            QStringList{m_internalHostSuffix},
            siteProfile(m_kind), view));
        m_view = view;
    } else {
        m_view = new QWebEngineView(webProfile(), central);
    }
#elif defined(DSH_HAVE_WEBVIEW) && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Qt WebView（Windows 上基于 Edge WebView2）：QWebView 是 QWindow 子类，
    // 用 createWindowContainer 包装成 QWidget 后嵌入主窗口
    auto *webView = new QWebView;
    m_view = QWidget::createWindowContainer(webView, central);
    m_webView = webView;
#else
    // 降级：无可用网页后端时的占位显示
    auto *tb = new QTextBrowser(central);
    tb->setPlainText(externalSite
                         ? QStringLiteral("无可用网页后端，无法显示 %1。\n（登录数据目录仍为：%2）")
                               .arg(defaultTitle(m_kind), siteDataDir(m_kind))
                         : QStringLiteral("无可用网页后端，网页无法显示。"));
    m_view = tb;
#endif
    v->addWidget(m_view, 1);

    setCentralWidget(central);

    // 地址栏回车 → 跳转(内部走 token→cookie 交换)
    connect(m_addrEdit, &QLineEdit::returnPressed, this, [this]() {
        openUrl(m_addrEdit->text());
    });
    connect(refreshBtn, &QToolButton::clicked, this, [this]() { reload(); });
    if (m_dataDirBtn) {
        connect(m_dataDirBtn, &QToolButton::clicked, this, &CDSWebViewWindow::openDataDir);
    }
#ifdef DSH_HAVE_WEBENGINE
    if (auto *eng = qobject_cast<QWebEngineView *>(m_view)) {
        connect(eng, &QWebEngineView::urlChanged, this, [this](const QUrl &u) {
            if (m_addrEdit)
                m_addrEdit->setText(u.toString());
            emit urlChanged(u.toString());
        });
        // 网页图标(favicon)：WebEngine 解出图标后转发给外层（外层把它设为 MDI 子窗口图标，
        // 于是“应用”页 tab 上显示的图标与网页自身的图标一致；同窗口内跳转时也会自动跟随）。
        connect(eng, &QWebEngineView::iconChanged, this, [this](const QIcon &icon) {
            if (icon.isNull()) {
                return; // 还没解出图标：不发信号，外层继续用兜底图标
            }
            emit faviconChanged(icon);
        });
    }
#endif
}

CDSWebViewWindow::~CDSWebViewWindow() = default;

QString CDSWebViewWindow::dataDir() const
{
    // 只有外部网站窗口使用 configure 目录下的专属数据目录；
    // DSH 服务窗口的登录靠 token→cookie 交换，不需要独立数据目录。
    return siteDataDir(m_kind);
}

void CDSWebViewWindow::openDataDir()
{
    const QString dir = dataDir();
    if (dir.isEmpty()) {
        return;
    }
    QDir().mkpath(dir); // 还没产生数据时目录可能不存在，先建出来便于查看
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void CDSWebViewWindow::openUrl(const QString &url)
{
    if (m_addrEdit)
        m_addrEdit->setText(url); // 先回显用户/传入的网址
    emit urlChanged(url);         // 外层 MDI 标题同步为当前网址

#ifdef DSH_HAVE_WEBENGINE
    QWebEngineView *eng = static_cast<QWebEngineView *>(m_view);
    const QUrl u(url);

    // DSH web 启动网址形如 http://127.0.0.1:端口/?token=xxx。
    // QWebEngine 不能可靠地把服务端那套 SameSite=Strict cookie 在 303 跳转到 / 时带过去,
    // 导致 401。这里由应用自己完成 token→cookie 交换: 用本地 HTTP 请求读取 Set-Cookie,
    // 注入 WebView 的 cookie store, 再加载干净的 /。
    // （仅 DSH 服务窗口；DeepSeek 官网等外部网站直接加载，登录由站点自己的 cookie 维持。）
    if (m_kind == CDSWebProfileKind::DshService
        && u.hasQuery() && u.query().contains(QLatin1String("token="))) {
        const QUrl clean = [&]() {
            QUrl c = u;
            c.setQuery(QString());
            c.setFragment(QString());
            c.setPath(QLatin1String("/"));
            return c;
        }();
        tryTokenExchange(u, clean, 0);
        return;
    }

    eng->load(u);
#elif defined(DSH_HAVE_WEBVIEW) && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (m_webView) {
        static_cast<QWebView *>(m_webView)->setUrl(QUrl(url));
    }
#else
    static_cast<QTextBrowser *>(m_view)->setPlainText(
        QStringLiteral("无可用网页后端，无法显示网页：\n%1").arg(url));
#endif
}

#ifdef DSH_HAVE_WEBENGINE
// 用本地 HTTP 请求向 DSH 服务交换登录 cookie, 成功则注入 WebView 并加载干净的 /。
// 服务刚启动时接口可能尚未就绪导致首次请求取不到 Set-Cookie, 这里自动重试几次。
void CDSWebViewWindow::tryTokenExchange(const QUrl &tokenUrl, const QUrl &clean, int attempt)
{
    constexpr int kMaxAttempts = 4;
    constexpr int kRetryDelayMs = 300;

    QNetworkRequest req(tokenUrl);
    // 不自动跟随 303, 以便读取 Set-Cookie
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    // 强制走 HTTP/1.1(禁用 HTTP/2), 确保发出标准的 Host 头, 服务端才能校验 authority;
    // 并补上浏览器常用头, 让请求尽量与浏览器一致。
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setRawHeader(QByteArrayLiteral("User-Agent"),
                     QByteArrayLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"));
    req.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("text/html,application/xhtml+xml,*/*;q=0.8"));
    QNetworkReply *reply = nam()->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, tokenUrl, clean, attempt]() {
        const QByteArray rawCookie = reply->rawHeader(QByteArrayLiteral("set-cookie"));
        const QVariant statusAttr = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const int status = statusAttr.isValid() ? statusAttr.toInt() : -1;
        const bool h2 = reply->attribute(QNetworkRequest::Http2WasUsedAttribute).toBool();
        const QString err = reply->errorString();
        Q_UNUSED(status);
        Q_UNUSED(h2);
        Q_UNUSED(err);
        reply->deleteLater();
        // 只要服务端返回了 Set-Cookie(通常为 303), 就视为交换成功;
        // 不要依赖 reply->error()(某些 Qt 版本对未自动跟随的重定向会报非 NoError)。
        if (!rawCookie.isEmpty()) {
            const QList<QNetworkCookie> cookies = QNetworkCookie::parseCookies(rawCookie);
            QWebEngineCookieStore *store = webProfile()->cookieStore();
            const QUrl origin(QStringLiteral("http://%1:%2/")
                                  .arg(clean.host()).arg(clean.port()));
            for (const QNetworkCookie &parsed : cookies) {
                QNetworkCookie c = parsed;
                if (c.path().isEmpty())
                    c.setPath(QLatin1String("/"));
                // 服务端下发的是 SameSite=Strict, Chromium(QWebEngine)在这种
                // 程序式加载下不会携带 Strict cookie, 导致 401。Cookie 只在服务端按
                // 校验值和 authority 验证, SameSite 是纯客户端属性, 因此这里只把注入的
                // 副本放宽为 Lax(顶层 / 与同源 /api/* 都会带上), 不改 DSH 服务端 cookie。
                c.setSameSitePolicy(QNetworkCookie::SameSite::Lax);
                store->setCookie(c, origin);
            }
            // 加载干净的 /(无 303 跳转); 此时已带放宽后的登录 cookie
            static_cast<QWebEngineView *>(m_view)->load(clean);
            return;
        }
        // 没拿到 cookie(服务未就绪/网络错误): 短暂等待后重试; 超出次数则放弃(保持空白)。
        if (attempt + 1 < kMaxAttempts) {
            QTimer::singleShot(kRetryDelayMs, this, [this, tokenUrl, clean, attempt]() {
                tryTokenExchange(tokenUrl, clean, attempt + 1);
            });
        }
    });
}
#endif // DSH_HAVE_WEBENGINE

void CDSWebViewWindow::reload()
{
#ifdef DSH_HAVE_WEBENGINE
    static_cast<QWebEngineView *>(m_view)->reload();
#elif defined(DSH_HAVE_WEBVIEW) && QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    if (m_webView) {
        static_cast<QWebView *>(m_webView)->reload();
    }
#else
    // 占位模式：无操作
#endif
}
