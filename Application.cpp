#include "Application.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

CApplication *CApplication::s_instance = nullptr;

CApplication::CApplication(int &argc, char **argv)
    : QApplication(argc, argv)
{
    s_instance = this;
}

CApplication::~CApplication()
{
    s_instance = nullptr;
}

CApplication *CApplication::instance()
{
    return s_instance;
}

QString CApplication::gitDir() const
{
    return m_gitDir;
}

void CApplication::setGitDir(const QString &dir)
{
    m_gitDir = dir;
}

QString CApplication::nodeDir() const
{
    return m_nodeDir;
}

void CApplication::setNodeDir(const QString &dir)
{
    m_nodeDir = dir;
}

QString CApplication::pluginMirror() const
{
    return m_pluginMirror;
}

void CApplication::setPluginMirror(const QString &url)
{
    m_pluginMirror = url;
}

QString CApplication::configDir() const
{
    // 配置目录：文档/DSH-Environment/configure（系统“文档”文件夹下的 DSH-Environment/configure 目录）。
    // 保存/读取的 configure 类文件（env.json、profiles.json、src.json 等）都放在这里。
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
           + QStringLiteral("/DSH-Environment/configure");
}

QString CApplication::settingsFilePath() const
{
    // 环境设置文件：env.json（位于 文档/DSH-Environment/configure 下）
    return configDir() + QStringLiteral("/env.json");
}

bool CApplication::saveEnvSettings() const
{
    const QString path = settingsFilePath();
    // 目录不存在则自动创建
    const QString dirPath = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dirPath)) {
        return false;
    }

    QJsonObject obj;
    obj.insert(QStringLiteral("gitDir"), m_gitDir);
    obj.insert(QStringLiteral("nodeDir"), m_nodeDir);
    obj.insert(QStringLiteral("pluginMirror"), m_pluginMirror);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool CApplication::loadEnvSettings()
{
    const QString path = settingsFilePath();
    // 文件不存在则不读取（不创建、不报错）
    if (!QFile::exists(path)) {
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return false;
    }

    const QJsonObject obj = doc.object();
    m_gitDir = obj.value(QStringLiteral("gitDir")).toString();
    m_nodeDir = obj.value(QStringLiteral("nodeDir")).toString();
    m_pluginMirror = obj.value(QStringLiteral("pluginMirror")).toString();
    return true;
}

void CApplication::registerServicePort(quint16 port)
{
    if (port != 0) {
        m_servicePorts.insert(port);
    }
}

void CApplication::unregisterServicePort(quint16 port)
{
    m_servicePorts.remove(port);
}

void CApplication::killServicePort(quint16 port) const
{
    // 用 netstat 找出监听该端口的进程 PID（服务可能已成孤儿进程，只能按端口找）
    QProcess netstat;
    netstat.start(QStringLiteral("netstat"), {QStringLiteral("-ano")});
    if (!netstat.waitForFinished(5000)) {
        return;
    }
    const QString output = QString::fromLocal8Bit(netstat.readAllStandardOutput());
    const QString portStr = QStringLiteral(":%1").arg(port);

    QSet<QString> pids;
    const QStringList lines = output.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.contains(portStr) && line.contains(QStringLiteral("LISTENING"))) {
            const QStringList parts = line.split(
                QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
            if (!parts.isEmpty()) {
                pids.insert(parts.last());
            }
        }
    }

    // 结束每个监听进程的整棵进程树
    for (const QString &pid : pids) {
        QProcess killer;
        killer.start(QStringLiteral("taskkill"),
                     {QStringLiteral("/PID"), pid, QStringLiteral("/T"), QStringLiteral("/F")});
        killer.waitForFinished(3000);
    }
}

void CApplication::killAllServices() const
{
    // 程序退出时：按登记的服务端口逐个关闭
    const QList<quint16> ports = m_servicePorts.values();
    for (quint16 port : ports) {
        killServicePort(port);
    }
}
