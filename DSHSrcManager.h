#ifndef DSHSRCMANAGER_H
#define DSHSRCMANAGER_H

#include <QHash>
#include <QList>
#include <QStringList>
#include <QWidget>

class CTableView;
class QHBoxLayout;
class QPoint;
class QProcess;
class QStandardItemModel;
class QTcpSocket;
class QTimer;

// DSH 源码管理窗口（QWidget，作为 QTabWidget 的页签内容）：
// 两张表格（各自上方预留按钮区）
// 表 2 支持右键“开始服务 / 停止服务”，把 pnpm dsh 作为后台服务管理，
// 并在“服务是否启动”列显示运行状态（只显示，不保存）
class CDSHSrcManager : public QWidget
{
    Q_OBJECT

public:
    explicit CDSHSrcManager(QWidget *parent = nullptr);
    ~CDSHSrcManager() override;

    // 第 1 张表 / 其上方按钮区
    CTableView *table1() const;
    QHBoxLayout *buttonBar1() const;

    // 第 2 张表 / 其上方按钮区
    CTableView *table2() const;
    QHBoxLayout *buttonBar2() const;

    // 记录服务启动后终端识别到的带 token 网址(供“打开网站”等使用)
    void recordWebUrl(const QString &url);
    // 按端口取已记录的带 token 网址(客户端登录用; 未记录返回空串)
    QString tokenUrlForPort(quint16 port) const;

signals:
    // 请求在 MDI 区域打开 Web 视图窗口（url 如 http://127.0.0.1:3000）
    void webViewRequested(const QString &url);
    // 请求在 MDI 区域打开 DSH 客户端窗口(按端口)
    void dshChatRequested(const QString &port);
    // 请求在 MDI 区域打开 CDSCmdView 终端窗口，并执行给定命令序列
    //   title       窗口标题(如 profile:端口)
    //   workingDir  终端初始工作目录
    //   lines       要在终端执行的命令(每条一行，不含回车)
    void cmdTerminalRequested(const QString &title, const QString &workingDir,
                              const QStringList &lines);
    // 请求关闭 MDI 中标题为 title 的终端窗口(用于“关闭服务”时先关掉再重开)
    void closeCmdTerminalRequested(const QString &title);
    // 表2某个服务的“服务是否启动”列状态发生实际变化(key = mainid|端口)
    void serviceStateChanged(const QString &key, bool running);

private slots:
    // “增加”按钮：弹出录入对话框，追加到表 1，并保存 JSON
    void onAddSrc();
    // “删除”按钮：确认后删除表 1 选中行，并保存 JSON
    void onDelSrc();
    // 表 2 右键菜单：开始/停止服务
    void onTable2ContextMenu(const QPoint &pos);
    // 服务启动检测（500ms 轮询，最多 1 分钟）
    void pollServices();
    // 周期健康检查：每 1 分钟扫描一次运行中服务的端口，连不上则杀掉后台并标记关闭
    void monitorServices();
    // 重启服务等待：状态变为“已经关闭”后再开启
    void onServiceStateChanged(const QString &key, bool running);
    // 表2“服务是否启动”状态监控：每 3 秒触发一轮, 顺序异步探测所有行端口
    void monitorTable2ServiceState();
    // 异步探测下一行端口
    void probeNextRow();
    // 更新某行“服务是否启动”列(变化时发信号)
    void setStateCell(int row, bool up, const QString &key);

private:
    // 后台服务信息
    struct ServiceInfo {
        QString key;            // mainid|端口
        QProcess *proc = nullptr; // 服务进程（cmd /c robocopy 拷贝 profiles/web 后启动 pnpm dsh ...）
        QTcpSocket *sock = nullptr; // 端口探测
        quint16 port = 0;       // 服务端口（web 模式为固定 8888）
        bool running = false;   // 是否已确认启动
        bool webMode = false;   // 是否以 web 方式打开服务（仅启动，不自动打开 MDI 网页窗口）
        int pollCount = 0;      // 已轮询次数（超过 120 = 60 秒超时）
    };

    // 保存到 配置目录/srcmanger.json；打开时从 JSON 读取
    void saveData() const;
    void loadData();
    // 刷新表 2：显示 profiles.json 中 mainid 与给定 id 相同的数据
    void refreshProfileTable(const QString &mainid);
    // 删除源记录时, 同步移除 profiles.json 中 mainid==id 的所有 profile 并保存
    void removeProfilesForSource(const QString &id);

    // 开始服务：确认命令 → 以后台 QProcess 运行 pnpm dsh（异常由 startService 捕获）
    // webMode=true 表示“以web方式打开服务”：去掉 robocopy 拷贝与按行参数的原 pnpm dsh，
    // 改用固定命令 pnpm dsh --profile web --port 8888，且仅启动服务、不自动打开 MDI 网页
    void startServiceImpl(int row, bool webMode = false);
    // 开始服务（入口，含异常捕获）
    void startService(int row, bool webMode = false);
    // 停止服务：taskkill 结束整个进程树
    void stopService(int row);
    // 关闭服务：结束选中行服务（含孤儿进程，按端口兜底）
    // confirm=true 时先弹确认框(仅“关闭服务”菜单用; 重启服务内部调用传 false 避免重复确认)
    void closeService(int row, bool confirm = true);
    // 重启服务：先关闭再开启
    void restartService(int row);
    // 打开独立 cmd 窗口：预执行开启服务时的命令（去掉启动服务用的命令），供插件操作
    void openCmdWindow(int row);
    // 在 MDI 区域打开 CDSCmdView 终端并执行对应的服务命令，窗口标题为“列 Profile:行 端口”。
    // webMode=true 表示“以web方式打开服务”（固定 web 配置/8888 端口）；
    // webMode=false 表示“开启服务”（robocopy 拷贝 web 目录 + 按行 Profile/端口启动）。
    void openServiceTerminal(int row, bool webMode = false);

    // 行 → 服务 key（mainid|端口）
    QString serviceKeyForRow(int row) const;
    // 端口 → 该服务最近一次带 token 的网址(未记录则空)
    QHash<quint16, QString> m_tokenUrlByPort;
    // 开启服务后, 轮询指定端口直到可连接, 更新表2“服务是否启动”列并打开 WebView
    void startWebViewProbe(const QString &url, quint16 port, const QString &key);
    // 根据服务 key 更新表 2 中对应行的“服务是否启动”列
    void setRowServiceStateByKey(const QString &key, bool running);
    // 根据服务 key 设置表 2 对应行状态列的指定文本（如“启动中...”）
    void setRowServiceStateTextByKey(const QString &key, const QString &text);
    // 服务端口连接成功：标记已启动
    void markServiceRunning(const QString &key);
    // 杀掉后台进程树（taskkill /T /F）并清理
    void killBackground(ServiceInfo &svc);

    QList<ServiceInfo> m_services; // 正在启动/运行的服务
    QTimer *m_pollTimer = nullptr;   // 启动检测定时器（500ms，最多 1 分钟）
    QTimer *m_monitorTimer = nullptr; // 周期健康检查定时器（1 分钟）
    QTimer *m_stateMonitorTimer = nullptr; // 表2“服务是否启动”状态监控（3s）

    // 开启服务后, 轮询端口就绪再打开 WebView/更新状态列
    QTimer *m_webProbeTimer = nullptr;    // 端口就绪检测定时器
    QTcpSocket *m_webProbeSocket = nullptr; // 端口探测 socket
    QString m_webProbeUrl;                // 待打开的 WebView 地址
    QString m_webProbeKey;                // 对应服务 key(mainid|端口), 用于更新状态列
    quint16 m_webProbePort = 0;           // 要探测的端口
    int m_webProbeTries = 0;              // 已探测次数(超时上限)
    bool m_webProbeConfirmed = false;     // 是否已确认服务启动

    // 重启服务: 等待状态变为“已经关闭”后再开启
    bool m_pendingRestart = false;        // 是否处于“等待关闭完成”的重启流程
    QString m_pendingRestartKey;          // 待等待的服务 key
    int m_pendingRestartRow = -1;         // 该 key 对应的表2行

    // 表2状态监控(顺序异步): 当前正在探测的行, -1 表示空闲
    int m_stateProbeRow = -1;

    CTableView *m_table1 = nullptr;       // 第 1 张表
    CTableView *m_table2 = nullptr;       // 第 2 张表
    QHBoxLayout *m_btnBar1 = nullptr;     // 表 1 上方的按钮区
    QHBoxLayout *m_btnBar2 = nullptr;     // 表 2 上方的按钮区
    QStandardItemModel *m_model1 = nullptr; // 表 1 数据模型
    QStandardItemModel *m_model2 = nullptr; // 表 2 数据模型（mainid 匹配的配置）
};

#endif // DSHSRCMANAGER_H
