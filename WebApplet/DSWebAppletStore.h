#ifndef DSWEBAPPLETSTORE_H
#define DSWEBAPPLETSTORE_H

#include <QList>
#include <QString>

// 网页小程序条目：一个快捷方式 = 图标 + 名称，双击后用 MDI 网页窗口打开 url。
//   name     名称（必填，不能重复；同时作为 MDI 网页窗口的去重标识）
//   url      网址（必填）
//   iconFile 图标文件名（icons 目录下；空串表示还没取到图标，界面用名称首字图标兜底）
struct DSWebApplet
{
    QString name;
    QString url;
    QString iconFile;
};

// 网页小程序数据存取（全部静态方法，无状态；表页与图标读取器共用同一套路径规则）：
//   数据目录  文档/DSH-Environment/configure/webapplets
//   数据文件  .../webapplets/webapplets.json   {"applets":[{"name","url","icon"}]}
//   图标目录  .../webapplets/icons             <uuid>.png（网页图标，PNG）
// 说明：图标文件名用 UUID 而不是名称，避免名称里的 \ / : * ? " < > | 等字符落到文件名上。
class CDSWebAppletStore
{
public:
    // 数据目录 / 图标目录 / JSON 文件路径
    static QString dataDir();
    static QString iconsDir();
    static QString jsonPath();
    // 图标文件名 → 完整路径（空文件名返回空串）
    static QString iconPath(const QString &iconFile);
    // 生成一个新的图标文件名（UUID.png）
    static QString makeIconFileName();

    // 读取 / 保存全部小程序（JSON 不合法或条目缺名称/网址时按空列表/跳过处理）
    static QList<DSWebApplet> load();
    static bool save(const QList<DSWebApplet> &applets);

    // 图标读取完成后写回 JSON：只有仍存在 name 与 url 都一致的条目时才写入
    //（期间条目被删除/改名/改网址则不写，避免把图标记到别的条目上）
    static bool setIconFile(const QString &name, const QString &url, const QString &iconFile);

    // 删除条目时顺手删掉它的图标文件（文件不存在时忽略）
    static void removeIconFile(const QString &iconFile);
};

#endif // DSWEBAPPLETSTORE_H
