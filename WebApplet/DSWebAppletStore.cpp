#include "DSWebAppletStore.h"
#include "../Application.h" // CApplication::configDir()（配置目录：文档/DSH-Environment/configure）

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

namespace {
// 网页小程序数据：配置目录下的 webapplets 子目录
const QString kAppletsDirName = QStringLiteral("webapplets");
const QString kAppletsJsonFile = QStringLiteral("webapplets.json");
const QString kIconsDirName = QStringLiteral("icons");
const QString kAppletsKey = QStringLiteral("applets");

// 配置目录：文档/DSH-Environment/configure
// 与 CApplication::configDir() 使用同一规则；app 实例不可用时按同样规则兜底。
QString configureDir()
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
} // namespace

QString CDSWebAppletStore::dataDir()
{
    return configureDir() + QLatin1Char('/') + kAppletsDirName;
}

QString CDSWebAppletStore::iconsDir()
{
    return dataDir() + QLatin1Char('/') + kIconsDirName;
}

QString CDSWebAppletStore::jsonPath()
{
    return dataDir() + QLatin1Char('/') + kAppletsJsonFile;
}

QString CDSWebAppletStore::iconPath(const QString &iconFile)
{
    if (iconFile.isEmpty()) {
        return QString();
    }
    return iconsDir() + QLatin1Char('/') + iconFile;
}

QString CDSWebAppletStore::makeIconFileName()
{
    // 名称可能含文件名非法字符，故用 UUID 作为图标文件名
    return QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".png");
}

QList<DSWebApplet> CDSWebAppletStore::load()
{
    QList<DSWebApplet> applets;
    const QString path = jsonPath();
    // 文件不存在则不读取（首次使用时还没有数据）
    if (!QFile::exists(path)) {
        return applets;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return applets;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return applets;
    }

    const QJsonArray arr = doc.object().value(kAppletsKey).toArray();
    for (const QJsonValue &value : arr) {
        const QJsonObject obj = value.toObject();
        DSWebApplet applet;
        applet.name = obj.value(QStringLiteral("name")).toString();
        applet.url = obj.value(QStringLiteral("url")).toString();
        applet.iconFile = obj.value(QStringLiteral("icon")).toString();
        // 名称与网址是必填项：手工改坏的记录直接跳过，不在界面上显示空快捷方式
        if (applet.name.isEmpty() || applet.url.isEmpty()) {
            continue;
        }
        applets.append(applet);
    }
    return applets;
}

bool CDSWebAppletStore::save(const QList<DSWebApplet> &applets)
{
    const QString path = jsonPath();
    // 目录不存在则自动创建
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonArray arr;
    for (const DSWebApplet &applet : applets) {
        QJsonObject obj;
        obj.insert(QStringLiteral("name"), applet.name);
        obj.insert(QStringLiteral("url"), applet.url);
        obj.insert(QStringLiteral("icon"), applet.iconFile);
        arr.append(obj);
    }
    QJsonObject root;
    root.insert(kAppletsKey, arr);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool CDSWebAppletStore::setIconFile(const QString &name, const QString &url, const QString &iconFile)
{
    QList<DSWebApplet> applets = load();
    bool found = false;
    for (DSWebApplet &applet : applets) {
        if (applet.name == name && applet.url == url) {
            applet.iconFile = iconFile;
            found = true;
        }
    }
    if (!found) {
        return false; // 条目已被删除/改名/改网址：不写回
    }
    return save(applets);
}

void CDSWebAppletStore::removeIconFile(const QString &iconFile)
{
    const QString path = iconPath(iconFile);
    if (!path.isEmpty() && QFile::exists(path)) {
        QFile::remove(path);
    }
}
