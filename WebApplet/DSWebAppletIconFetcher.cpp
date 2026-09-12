#include "DSWebAppletIconFetcher.h"
#include "DSWebAppletStore.h"
#include "../DSWebViewWindow.h" // 小程序窗口使用的 WebEngine profile（读图标时复用同一份登录态/缓存）

#include <QDir>
#include <QImage>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QSize>
#include <QTimer>

#ifdef DSH_HAVE_WEBENGINE
#include <QWebEnginePage>
#include <QWebEngineProfile>
#endif

namespace {
// 探测超时（毫秒）：网页迟迟不给图标地址时，直接走 /favicon.ico 兜底
const int kProbeTimeoutMs = 20000;
// 单个图标下载的超时（毫秒）
const int kDownloadTimeoutMs = 15000;
// 图标保存尺寸（界面上按 48×48 显示，存 64×64 更清晰）
const int kIconSize = 64;

// 浏览器 UA：部分站点按 UA 决定是否返回图标/是否给降级页面
const QByteArray kBrowserUa =
    QByteArrayLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

// 解析 data: URL 的内联图标（有些站点把图标直接内联在页面里）
QByteArray decodeDataUrl(const QUrl &url)
{
    const QString text = url.toString(QUrl::None);
    const int comma = text.indexOf(QLatin1Char(','));
    if (!text.startsWith(QLatin1String("data:"), Qt::CaseInsensitive) || comma < 0) {
        return QByteArray();
    }
    const QString meta = text.mid(5, comma - 5); // "data:" 与 "," 之间的媒体类型
    const QString payload = text.mid(comma + 1);
    if (meta.endsWith(QLatin1String(";base64"), Qt::CaseInsensitive)) {
        return QByteArray::fromBase64(payload.toLatin1());
    }
    return QUrl::fromPercentEncoding(payload.toUtf8()).toUtf8();
}
} // namespace

CDSWebAppletIconFetcher::CDSWebAppletIconFetcher(const QString &name, const QString &url,
                                                 const QString &iconFile, QObject *parent)
    : QObject(parent)
    , m_name(name)
    , m_url(url)
    , m_iconFile(iconFile)
{
    m_nam = new QNetworkAccessManager(this);
}

CDSWebAppletIconFetcher::~CDSWebAppletIconFetcher() = default;

void CDSWebAppletIconFetcher::start()
{
    if (m_done) {
        return;
    }
    // 探测超时：超时后按“网页没给图标”处理，走 /favicon.ico 兜底
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(kProbeTimeoutMs);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        if (!m_done) {
            tryFaviconFile();
        }
    });
    m_timeout->start();

    startProbe();
}

void CDSWebAppletIconFetcher::startProbe()
{
#ifdef DSH_HAVE_WEBENGINE
    // 后台页面（不放进任何视图）：只用来让 Chromium 解析站点图标
    // （<link rel="icon"> / manifest / /favicon.ico 等）。
    // profile 按网址解析：属于内置站点（DeepSeek / 今日头条 / GitHub）就用该站点的 profile
    // —— 与双击打开小程序窗口用的完全是同一份（登录态/cache 一致，图标也取得更准），
    // 其它网址用网页小程序自己的 profile。
    QWebEngineProfile *profile = CDSWebViewWindow::profileFor(
        CDSWebViewWindow::kindForUrl(m_url));
    m_page = profile ? new QWebEnginePage(profile, this) : new QWebEnginePage(this);

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Qt6：图标已由页面解码好，直接拿来用（省一次下载）
    connect(m_page, &QWebEnginePage::iconChanged, this, [this](const QIcon &icon) {
        if (m_done || icon.isNull()) {
            return;
        }
        finishWithIcon(icon);
    });
#endif

    // 网页声明的图标地址（在页面加载过程中发出，也可能晚于 loadFinished）
    connect(m_page, &QWebEnginePage::iconUrlChanged, this, [this](const QUrl &iconUrl) {
        if (m_done || m_iconDownloadStarted || m_triedFavicon) {
            return;
        }
        if (iconUrl.isValid() && !iconUrl.isEmpty()) {
            m_iconDownloadStarted = true;
            requestIcon(iconUrl, false);
        }
    });

    connect(m_page, &QWebEnginePage::loadFinished, this, [this](bool ok) {
        Q_UNUSED(ok);
        if (m_done) {
            return;
        }
        // 页面加载完还没有图标地址 → 认为网页没声明图标，走 favicon 兜底
        const QUrl iconUrl = m_page ? m_page->iconUrl() : QUrl();
        if (iconUrl.isValid() && !iconUrl.isEmpty() && !m_iconDownloadStarted && !m_triedFavicon) {
            m_iconDownloadStarted = true;
            requestIcon(iconUrl, false);
            return;
        }
        if (!m_iconDownloadStarted) {
            tryFaviconFile();
        }
    });

    m_page->load(QUrl(m_url));
#else
    // 没有 WebEngine 后端：只能直接取 /favicon.ico
    tryFaviconFile();
#endif
}

void CDSWebAppletIconFetcher::requestIcon(const QUrl &iconUrl, bool isFallback)
{
    if (m_done) {
        return;
    }

    // 内联图标（data: URL）：不经过网络，就地解码
    if (iconUrl.scheme().compare(QLatin1String("data"), Qt::CaseInsensitive) == 0) {
        const QByteArray data = decodeDataUrl(iconUrl);
        const QImage image = data.isEmpty() ? QImage() : QImage::fromData(data);
        if (!image.isNull()) {
            finishWithIcon(QIcon(QPixmap::fromImage(image)));
            return;
        }
        if (isFallback) {
            finishFailed();
        } else {
            tryFaviconFile();
        }
        return;
    }

    // 相对地址按网页地址补全
    const QUrl target = iconUrl.isRelative() ? QUrl(m_url).resolved(iconUrl) : iconUrl;
    if (!target.isValid() || target.host().isEmpty()) {
        if (isFallback) {
            finishFailed();
        } else {
            tryFaviconFile();
        }
        return;
    }

    QNetworkRequest req(target);
    // 超时保护：下载卡住时按失败处理
    req.setTransferTimeout(kDownloadTimeoutMs);
    // 跟随安全的重定向（不少站点的图标地址会 302 到 CDN）
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader(QByteArrayLiteral("User-Agent"), kBrowserUa);
    req.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("image/*,*/*;q=0.8"));

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, isFallback]() {
        const QByteArray data = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
        if (m_done) {
            return;
        }
        if (ok && !data.isEmpty()) {
            const QImage image = QImage::fromData(data);
            if (!image.isNull()) {
                finishWithIcon(QIcon(QPixmap::fromImage(image)));
                return;
            }
        }
        // 下载失败或不是可识别的图片：非兜底流程 → 再试 /favicon.ico；兜底流程 → 放弃
        if (isFallback) {
            finishFailed();
        } else {
            tryFaviconFile();
        }
    });
}

void CDSWebAppletIconFetcher::tryFaviconFile()
{
    if (m_done || m_triedFavicon) {
        return; // 只兜底一次
    }
    m_triedFavicon = true;
    if (m_timeout) {
        m_timeout->stop(); // 已进入兜底流程，探测超时不再需要
    }

    // <协议>://<主机>[:端口]/favicon.ico
    const QUrl base(m_url);
    QUrl favicon;
    favicon.setScheme(base.scheme().isEmpty() ? QStringLiteral("https") : base.scheme());
    favicon.setHost(base.host());
    if (base.port() > 0) {
        favicon.setPort(base.port());
    }
    favicon.setPath(QStringLiteral("/favicon.ico"));
    if (favicon.host().isEmpty()) {
        finishFailed();
        return;
    }
    requestIcon(favicon, true);
}

void CDSWebAppletIconFetcher::finishWithIcon(const QIcon &icon)
{
    if (m_done) {
        return;
    }
    m_done = true;

    // 统一缩放到 kIconSize：不同站点给的图标尺寸五花八门，落盘统一大小
    const QPixmap pixmap = icon.pixmap(QSize(kIconSize, kIconSize));
    QString savedFile;
    if (!pixmap.isNull()) {
        QDir().mkpath(CDSWebAppletStore::iconsDir());
        const QString path = CDSWebAppletStore::iconPath(m_iconFile);
        if (!path.isEmpty() && pixmap.save(path, "PNG")) {
            // 写回 JSON（条目已被删除/改名/改网址时不会写）
            CDSWebAppletStore::setIconFile(m_name, m_url, m_iconFile);
            savedFile = m_iconFile;
        }
    }

    cleanup();
    if (!savedFile.isEmpty()) {
        emit iconReady(m_name, m_url, savedFile);
    }
}

void CDSWebAppletIconFetcher::finishFailed()
{
    if (m_done) {
        return;
    }
    m_done = true;
    cleanup(); // 取不到图标：不写文件、不写 JSON，界面继续用名称首字图标
}

void CDSWebAppletIconFetcher::cleanup()
{
    if (m_timeout) {
        m_timeout->stop();
        m_timeout->deleteLater();
        m_timeout = nullptr;
    }
#ifdef DSH_HAVE_WEBENGINE
    if (m_page) {
        m_page->disconnect(this); // 先断开信号，避免收尾过程中还有回调进来
        m_page->deleteLater();
        m_page = nullptr;
    }
#endif
    deleteLater();
}
