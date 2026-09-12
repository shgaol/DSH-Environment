#ifndef APPLICATION_H
#define APPLICATION_H

#include <QApplication>
#include <QSet>
#include <QString>

// 应用类：继承 QApplication，持有全局配置（Git/Node 目录等）
class CApplication : public QApplication
{
    Q_OBJECT

public:
    explicit CApplication(int &argc, char **argv);
    ~CApplication() override;

    // 获取唯一实例（main 中创建后即可使用）
    static CApplication *instance();

    // Git 目录
    QString gitDir() const;
    void setGitDir(const QString &dir);

    // Node 目录
    QString nodeDir() const;
    void setNodeDir(const QString &dir);

    // 插件市场镜像
    QString pluginMirror() const;
    void setPluginMirror(const QString &url);

    // ---- 环境设置 JSON 持久化 ----
    // 配置目录：文档/DSH-Environment/configure（不存在则按需创建）
    QString configDir() const;
    // 环境设置 JSON 文件路径（配置目录下）
    QString settingsFilePath() const;
    // 保存环境设置到 JSON 文件（目录不存在会自动创建）
    bool saveEnvSettings() const;
    // 从 JSON 文件读取环境设置：
    // 文件存在则读取并覆盖当前值，返回 true；文件不存在返回 false 且不做任何修改
    bool loadEnvSettings();

    // ---- 服务端口管理（程序退出时自动关闭对应服务）----
    // 登记一个正在运行的服务端口
    void registerServicePort(quint16 port);
    // 注销一个服务端口
    void unregisterServicePort(quint16 port);
    // 按端口查找监听进程并结束其进程树（含孤儿进程兜底）
    void killServicePort(quint16 port) const;
    // 按登记端口逐个关闭服务（程序退出时调用）
    void killAllServices() const;

private:
    static CApplication *s_instance; // 唯一实例指针

    QString m_gitDir;  // Git 目录
    QString m_nodeDir; // Node 目录
    QString m_pluginMirror; // 插件市场镜像
    QSet<quint16> m_servicePorts; // 正在运行的服务端口（退出时按端口关闭服务）
};

#endif // APPLICATION_H
