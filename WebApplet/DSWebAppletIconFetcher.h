#ifndef DSWEBAPPLETICONFETCHER_H
#define DSWEBAPPLETICONFETCHER_H

#include <QIcon>
#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QTimer;
#ifdef DSH_HAVE_WEBENGINE
class QWebEnginePage;
#endif

// 网页小程序图标读取器：保存名称+网址后，异步读取该网页的图标，
// 存成 PNG（icons 目录）并写回 webapplets.json，取到后发出 iconReady。
//
// 取值顺序（逐级兜底）：
//   1) Qt6：后台页面已解码好的图标（iconChanged）；
//   2) 后台页面加载网址，取网页声明的图标地址（iconUrl）再下载（data: 图标就地解码）；
//   3) 直接下载 <站点>/favicon.ico；
//   4) 都取不到 → 不发信号（界面用名称首字图标兜底）。
//
// 生命周期：由表页创建，但父对象挂在 qApp 上并自行 deleteLater ——
// 即使“网页小程序”表页随后被关闭，图标仍会取回并写回 JSON。
class CDSWebAppletIconFetcher : public QObject
{
    Q_OBJECT

public:
    CDSWebAppletIconFetcher(const QString &name, const QString &url, const QString &iconFile,
                            QObject *parent = nullptr);
    ~CDSWebAppletIconFetcher() override;

    // 开始读取（异步；结束时会 deleteLater，调用方不必管理）
    void start();

signals:
    // 图标已保存（iconFile 为 icons 目录下的文件名）；失败则不发信号
    void iconReady(const QString &name, const QString &url, const QString &iconFile);

private:
    // 后台页面探测网页图标（无 WebEngine 时直接走 favicon 兜底）
    void startProbe();
    // 下载指定图标地址：isFallback=true 表示这是最后的 favicon.ico 兜底
    void requestIcon(const QUrl &iconUrl, bool isFallback);
    // 兜底：<协议>://<主机>[:端口]/favicon.ico
    void tryFaviconFile();
    // 拿到图标：保存 PNG + 写回 JSON + 发信号 + 收尾
    void finishWithIcon(const QIcon &icon);
    // 收不到图标：不写文件/JSON，直接收尾（界面继续用名称首字图标）
    void finishFailed();
    // 断开并清理探测页面与定时器，最后 deleteLater
    void cleanup();

    QString m_name;     // 条目名称（写回 JSON 时校验用）
    QString m_url;      // 条目网址（同上）
    QString m_iconFile; // 图标文件名（UUID.png）
    bool m_done = false;              // 是否已结束（成功或失败）
    bool m_iconDownloadStarted = false; // 是否已按网页声明的图标地址开始下载
    bool m_triedFavicon = false;      // 是否已走过 /favicon.ico 兜底
    QTimer *m_timeout = nullptr;      // 探测超时（超时后走 favicon 兜底）
    QNetworkAccessManager *m_nam = nullptr; // 图标下载
#ifdef DSH_HAVE_WEBENGINE
    QWebEnginePage *m_page = nullptr; // 后台探测页面（无视图）
#endif
};

#endif // DSWEBAPPLETICONFETCHER_H
