#include "DSHSrcManager.h"
#include "TableView.h"
#include "DHSSrcDlg.h"
#include "DHSScrCtrlDlg.h"
#include "Application.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QUrl>
#include <QMessageBox>
#include <QPlainTextEdit>

#include <cstdio>
#include <exception>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <string>

// Win32 API（放在 Qt 头文件之后，避免宏冲突）
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {
// 源码数据文件（存放在 文档/DSH-Environment/configure 配置目录下）
const QString kSrcJsonFile = QStringLiteral("srcmanger.json");

// 查找指定进程的控制台窗口（EnumWindows 回调，C 调用约定，不能是 lambda）
HWND g_consoleTarget = nullptr;
DWORD g_consolePid = 0;

BOOL CALLBACK findConsoleProc(HWND hwnd, LPARAM /*lParam*/)
{
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid == g_consolePid) {
        wchar_t cls[256] = {0};
        if (GetClassNameW(hwnd, cls, 256) != 0
            && wcscmp(cls, L"ConsoleWindowClass") == 0) {
            g_consoleTarget = hwnd;
            return FALSE; // 找到即停止枚举
        }
    }
    return TRUE;
}

// 打开一个独立的可见 cmd 窗口执行命令（/k 保持窗口打开，可继续输入）。
// 用 CREATE_NEW_CONSOLE：进程独立于本程序，本程序退出后 cmd 仍可继续使用。
// cmdLine 为空时仅打开一个裸 cmd 窗口；workingDir 指定起始工作目录（可为空）。
// 返回是否启动成功。
bool launchCmdWindow(const QString &cmdLine, const QString &workingDir = QString())
{
    const QString full = QStringLiteral("cmd.exe /k ") + cmdLine;
    std::wstring wcmd = full.toStdWString();
    std::wstring wdir;
    if (!workingDir.isEmpty()) {
        wdir = QDir::toNativeSeparators(workingDir).toStdWString();
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessW(nullptr, &wcmd[0], nullptr, nullptr, FALSE,
                        CREATE_NEW_CONSOLE, nullptr,
                        wdir.empty() ? nullptr : &wdir[0], &si, &pi)) {
        return false;
    }
    CloseHandle(pi.hThread);

    // 把新开的 cmd 窗口带到前台并聚焦，方便直接输入命令
    g_consolePid = pi.dwProcessId;
    g_consoleTarget = nullptr;
    for (int i = 0; i < 40 && !g_consoleTarget; ++i) {
        EnumWindows(findConsoleProc, 0);
        if (!g_consoleTarget) {
            Sleep(50);
        }
    }
    if (g_consoleTarget) {
        ShowWindow(g_consoleTarget, SW_SHOW);
        SetForegroundWindow(g_consoleTarget);
        SetFocus(g_consoleTarget);
    }
    CloseHandle(pi.hProcess);
    return true;
}
} // namespace

CDSHSrcManager::CDSHSrcManager(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("DSH源码管理"));
    resize(900, 600);

    // 重启服务: 状态列变化(变为“已经关闭”)时, 若处于等待则执行开启
    connect(this, &CDSHSrcManager::serviceStateChanged,
            this, &CDSHSrcManager::onServiceStateChanged);

    // 表2“服务是否启动”状态监控: 每 3 秒探查所有行端口, 刷新状态列
    m_stateMonitorTimer = new QTimer(this);
    m_stateMonitorTimer->setInterval(3000);
    connect(m_stateMonitorTimer, &QTimer::timeout,
            this, &CDSHSrcManager::monitorTable2ServiceState);
    m_stateMonitorTimer->start();

    // 表 1 数据模型：ID / 名称 / 源码目录 / 版本 / 是否install / 是否build / 说明
    m_model1 = new QStandardItemModel(0, 7, this);
    m_model1->setHorizontalHeaderLabels({
        QStringLiteral("ID"), QStringLiteral("名称"), QStringLiteral("源码目录"),
        QStringLiteral("版本"), QStringLiteral("是否install"),
        QStringLiteral("是否build"), QStringLiteral("说明")});

    auto *mainLayout = new QVBoxLayout(this); // 上下结构
    mainLayout->setContentsMargins(6, 6, 6, 6);
    mainLayout->setSpacing(6);

    // ---- 第 1 行：表 1 上方按钮区 + CTableView ----
    auto *col1 = new QWidget(this);
    auto *v1 = new QVBoxLayout(col1);
    v1->setContentsMargins(0, 0, 0, 0);
    v1->setSpacing(4);
    m_btnBar1 = new QHBoxLayout;
    m_btnBar1->setContentsMargins(0, 0, 0, 0);
    v1->addLayout(m_btnBar1);
    // 表 1 按钮区：增加 / 删除
    auto *addBtn = new QPushButton(QStringLiteral("增加"), col1);
    m_btnBar1->addWidget(addBtn);
    connect(addBtn, &QPushButton::clicked, this, &CDSHSrcManager::onAddSrc);

    auto *delBtn = new QPushButton(QStringLiteral("删除"), col1);
    m_btnBar1->addWidget(delBtn);
    connect(delBtn, &QPushButton::clicked, this, &CDSHSrcManager::onDelSrc);

    m_table1 = new CTableView(col1);
    m_table1->setModel(m_model1);
    m_table1->setColumnWidth(0, 230); // ID 列宽度
    v1->addWidget(m_table1, 1);

    // 双击表 1 记录：弹出只读查看对话框
    connect(m_table1, &QTableView::doubleClicked, this,
            [this](const QModelIndex &index) {
                const int row = index.row();
                CDHSScrCtrlDlg dlg(
                    m_model1->item(row, 0)->text(), // ID（记录到对话框中）
                    m_model1->item(row, 1)->text(), // 名称
                    m_model1->item(row, 2)->text(), // 源码目录
                    m_model1->item(row, 3)->text(), // 版本
                    m_model1->item(row, 6)->text(), // 说明
                    this);
                // 回显该记录已保存的执行状态
                dlg.setInstallChecked(
                    m_model1->item(row, 4)->text() == QStringLiteral("是"));
                dlg.setBuildChecked(
                    m_model1->item(row, 5)->text() == QStringLiteral("是"));
                dlg.exec();

                // 执行状态可能被修改：同步回表格并保存 JSON
                m_model1->item(row, 4)->setText(
                    dlg.installChecked() ? QStringLiteral("是") : QStringLiteral("否"));
                m_model1->item(row, 5)->setText(
                    dlg.buildChecked() ? QStringLiteral("是") : QStringLiteral("否"));
                saveData();
                // 配置可能被修改：刷新表 2 的 mainid 匹配数据
                refreshProfileTable(m_model1->item(row, 0)->text());
            });

    // 表 2 数据模型：mainid 匹配的配置 + 服务状态列（状态只显示，不保存）
    m_model2 = new QStandardItemModel(0, 9, this);
    m_model2->setHorizontalHeaderLabels({
        QStringLiteral("mainid"), QStringLiteral("源码名称"), QStringLiteral("版本"),
        QStringLiteral("是否install"), QStringLiteral("是否build"),
        QStringLiteral("DSH_HOME"), QStringLiteral("Profile"), QStringLiteral("端口"),
        QStringLiteral("服务是否启动")});

    // ---- 第 2 行：表 2 上方按钮区（预留）+ CTableView ----
    auto *col2 = new QWidget(this);
    auto *v2 = new QVBoxLayout(col2);
    v2->setContentsMargins(0, 0, 0, 0);
    v2->setSpacing(4);
    m_btnBar2 = new QHBoxLayout; // 预留：后续在此添加按钮
    m_btnBar2->setContentsMargins(0, 0, 0, 0);
    v2->addLayout(m_btnBar2);
    m_table2 = new CTableView(col2);
    m_table2->setModel(m_model2);
    m_table2->setColumnWidth(0, 200);
    m_table2->setColumnWidth(5, 200);
    v2->addWidget(m_table2, 1);

    mainLayout->addWidget(col1, 1);
    mainLayout->addWidget(col2, 1);

    // 鼠标左键点中表 1 某行时：表 2 显示 mainid 相同的配置
    connect(m_table1->selectionModel(), &QItemSelectionModel::currentRowChanged,
            this, [this](const QModelIndex &current, const QModelIndex &) {
                const QString id = current.isValid()
                    ? m_model1->item(current.row(), 0)->text()
                    : QString();
                refreshProfileTable(id);
            });

    // 表 2 右键菜单：开始服务 / 停止服务
    m_table2->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table2, &QWidget::customContextMenuRequested,
            this, &CDSHSrcManager::onTable2ContextMenu);

    // 打开窗口时读取已保存的数据
    loadData();
}

CDSHSrcManager::~CDSHSrcManager()
{
    // 关闭时：把正在运行的服务进程树一并关闭，避免残留孤儿进程。
    // 先取出所有服务并清空列表，再对拷贝逐个清理（避免清理过程中引用失效）
    const QList<ServiceInfo> services = m_services;
    m_services.clear();
    for (const ServiceInfo &svc : services) {
        ServiceInfo copy = svc; // 拷贝一份操作，避免影响原列表
        killBackground(copy);
    }
}

CTableView *CDSHSrcManager::table1() const
{
    return m_table1;
}

QHBoxLayout *CDSHSrcManager::buttonBar1() const
{
    return m_btnBar1;
}

CTableView *CDSHSrcManager::table2() const
{
    return m_table2;
}

QHBoxLayout *CDSHSrcManager::buttonBar2() const
{
    return m_btnBar2;
}

void CDSHSrcManager::onAddSrc()
{
    CDHSSrcDlg dlg(this);
    if (dlg.exec() != QDialog::Accepted) {
        return; // 取消或未通过校验
    }

    // 生成唯一 ID（UUID，保证不重复）作为该条数据的唯一标识
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);

    // 追加一行到表 1（新记录默认未执行 install/build）
    const int row = m_model1->rowCount();
    m_model1->insertRow(row);
    m_model1->setItem(row, 0, new QStandardItem(id));
    m_model1->setItem(row, 1, new QStandardItem(dlg.name()));
    m_model1->setItem(row, 2, new QStandardItem(dlg.srcDir()));
    m_model1->setItem(row, 3, new QStandardItem(dlg.version()));
    m_model1->setItem(row, 4, new QStandardItem(QStringLiteral("否")));
    m_model1->setItem(row, 5, new QStandardItem(QStringLiteral("否")));
    m_model1->setItem(row, 6, new QStandardItem(dlg.desc()));

    saveData();
}

void CDSHSrcManager::onDelSrc()
{
    const QModelIndex idx = m_table1->currentIndex();
    if (!idx.isValid()) {
        // 未选中：提示
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("未选中数据！"));
        return;
    }
    const QString id = m_model1->item(idx.row(), 0)->text(); // 源记录 id(也是 profile 的 mainid)

    // 确认删除
    const QString name = m_model1->item(idx.row(), 1)->text();
    const QMessageBox::StandardButton ret = QMessageBox::question(
        this, QStringLiteral("确认删除"),
        QStringLiteral("确定删除【%1】这条数据吗？").arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        return; // 点“否”：不删除
    }

    m_model1->removeRow(idx.row());
    saveData(); // 同步保存 JSON
    // 同步删除该源对应的 profile(mainid==id), 并保存 profiles.json
    removeProfilesForSource(id);
    // 表 2 由 m_table1 的 currentRowChanged 连接自动刷新为新的选中源
}

void CDSHSrcManager::removeProfilesForSource(const QString &id)
{
    const QString path = CApplication::instance()->configDir()
                         + QStringLiteral("/profiles.json");
    if (!QFile::exists(path)) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return;
    }

    // 保留 mainid != id 的记录
    QJsonArray kept;
    const QJsonArray arr = doc.object().value(QStringLiteral("profiles")).toArray();
    for (const QJsonValue &value : arr) {
        if (value.toObject().value(QStringLiteral("mainid")).toString() != id) {
            kept.append(value);
        }
    }

    QJsonObject root;
    root.insert(QStringLiteral("profiles"), kept);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile out(path);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        out.close();
    }
}

void CDSHSrcManager::saveData() const
{
    const QString path = CApplication::instance()->configDir()
                         + QLatin1Char('/') + kSrcJsonFile;
    // 目录不存在则自动创建
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonArray arr;
    for (int r = 0; r < m_model1->rowCount(); ++r) {
        QJsonObject obj;
        obj.insert(QStringLiteral("id"), m_model1->item(r, 0)->text());
        obj.insert(QStringLiteral("name"), m_model1->item(r, 1)->text());
        obj.insert(QStringLiteral("srcDir"), m_model1->item(r, 2)->text());
        obj.insert(QStringLiteral("version"), m_model1->item(r, 3)->text());
        obj.insert(QStringLiteral("install"),
                   m_model1->item(r, 4)->text() == QStringLiteral("是"));
        obj.insert(QStringLiteral("build"),
                   m_model1->item(r, 5)->text() == QStringLiteral("是"));
        obj.insert(QStringLiteral("desc"), m_model1->item(r, 6)->text());
        arr.append(obj);
    }
    QJsonObject root;
    root.insert(QStringLiteral("srcs"), arr);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
}

void CDSHSrcManager::loadData()
{
    const QString path = CApplication::instance()->configDir()
                         + QLatin1Char('/') + kSrcJsonFile;
    // 文件不存在则不读取
    if (!QFile::exists(path)) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return;
    }

    const QJsonArray arr = doc.object().value(QStringLiteral("srcs")).toArray();
    for (const QJsonValue &value : arr) {
        const QJsonObject obj = value.toObject();
        const int row = m_model1->rowCount();
        m_model1->insertRow(row);
        m_model1->setItem(row, 0, new QStandardItem(obj.value(QStringLiteral("id")).toString()));
        m_model1->setItem(row, 1, new QStandardItem(obj.value(QStringLiteral("name")).toString()));
        m_model1->setItem(row, 2, new QStandardItem(obj.value(QStringLiteral("srcDir")).toString()));
        m_model1->setItem(row, 3, new QStandardItem(obj.value(QStringLiteral("version")).toString()));
        m_model1->setItem(row, 4,
                          new QStandardItem(obj.value(QStringLiteral("install")).toBool()
                                                ? QStringLiteral("是")
                                                : QStringLiteral("否")));
        m_model1->setItem(row, 5,
                          new QStandardItem(obj.value(QStringLiteral("build")).toBool()
                                                ? QStringLiteral("是")
                                                : QStringLiteral("否")));
        m_model1->setItem(row, 6, new QStandardItem(obj.value(QStringLiteral("desc")).toString()));
    }
}

void CDSHSrcManager::refreshProfileTable(const QString &mainid)
{
    // 清空表 2
    m_model2->removeRows(0, m_model2->rowCount());
    if (mainid.isEmpty()) {
        return;
    }

    // 读取 profiles.json，只显示 mainid 匹配的数据
    const QString path = CApplication::instance()->configDir()
                         + QStringLiteral("/profiles.json");
    if (!QFile::exists(path)) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) {
        return;
    }

    const QJsonArray arr = doc.object().value(QStringLiteral("profiles")).toArray();
    for (const QJsonValue &value : arr) {
        const QJsonObject obj = value.toObject();
        if (obj.value(QStringLiteral("mainid")).toString() != mainid) {
            continue;
        }
        const int row = m_model2->rowCount();
        m_model2->insertRow(row);
        m_model2->setItem(row, 0, new QStandardItem(obj.value(QStringLiteral("mainid")).toString()));
        m_model2->setItem(row, 1, new QStandardItem(obj.value(QStringLiteral("srcName")).toString()));
        m_model2->setItem(row, 2, new QStandardItem(obj.value(QStringLiteral("version")).toString()));
        m_model2->setItem(row, 3,
            new QStandardItem(obj.value(QStringLiteral("install")).toBool()
                                  ? QStringLiteral("是") : QStringLiteral("否")));
        m_model2->setItem(row, 4,
            new QStandardItem(obj.value(QStringLiteral("build")).toBool()
                                  ? QStringLiteral("是") : QStringLiteral("否")));
        m_model2->setItem(row, 5, new QStandardItem(obj.value(QStringLiteral("dshHome")).toString()));
        // 运行类型：兼容旧字段 instanceName
        m_model2->setItem(row, 6,
            new QStandardItem(obj.value(QStringLiteral("runType"))
                                  .toString(obj.value(QStringLiteral("instanceName")).toString())));
        m_model2->setItem(row, 7, new QStandardItem(obj.value(QStringLiteral("port")).toString()));
        // 服务状态列：有内存跟踪的直接显示，未跟踪的（如窗口重开后残留的服务）实际探测端口
        const QString key = m_model2->item(row, 0)->text()
                            + QStringLiteral("|")
                            + m_model2->item(row, 7)->text();
        bool tracked = false;
        bool running = false;
        for (const ServiceInfo &svc : m_services) {
            if (svc.key == key) {
                tracked = true;
                running = svc.running;
                break;
            }
        }
        if (tracked) {
            m_model2->setItem(row, 8,
                new QStandardItem(running ? QStringLiteral("已经启动")
                                          : QStringLiteral("已经关闭")));
        } else {
            m_model2->setItem(row, 8, new QStandardItem(QStringLiteral("已经关闭")));
            // 异步探测端口：能连上说明服务实际在运行
            const quint16 port = m_model2->item(row, 7)->text().toUShort();
            if (port != 0) {
                auto *sock = new QTcpSocket(this);
                connect(sock, &QTcpSocket::connected, this,
                        [this, key, sock]() {
                            setRowServiceStateByKey(key, true);
                            sock->deleteLater();
                        });
                connect(sock, &QTcpSocket::errorOccurred, this,
                        [sock]() { sock->deleteLater(); });
                sock->connectToHost(QHostAddress::LocalHost, port);
            }
        }
    }
}

void CDSHSrcManager::onTable2ContextMenu(const QPoint &pos)
{
    const QModelIndex idx = m_table2->indexAt(pos);
    if (!idx.isValid()) {
        return;
    }
    m_table2->selectRow(idx.row());
    const int row = idx.row();

    QMenu menu(this);
    // 菜单顺序（自上而下）：
    //  1 以web方式打开服务  2 开启服务  3 重启服务  4 关闭服务
    //  5 打开DSH客户端  6 打开网页  7 打开cmd
    QAction *webSvcAct = menu.addAction(QStringLiteral("以web方式打开服务"));
    QAction *svcAct = menu.addAction(QStringLiteral("开启服务"));
    QAction *restartAct = menu.addAction(QStringLiteral("重启服务"));
    QAction *closeAct = menu.addAction(QStringLiteral("关闭服务"));
    QAction *dshAct = menu.addAction(QStringLiteral("打开DSH客户端"));
    QAction *webAct = menu.addAction(QStringLiteral("打开网页"));
    QAction *cmdAct = menu.addAction(QStringLiteral("打开cmd"));
    QAction *chosen = menu.exec(m_table2->viewport()->mapToGlobal(pos));
    if (chosen == cmdAct) {
        openCmdWindow(row);
    } else if (chosen == webAct) {
        // 优先打开该端口服务启动后记录到的带 token 网址; 未记录则退回到不带 token 的地址
        const QString port = m_model2->item(row, 7)->text();
        const quint16 p = static_cast<quint16>(port.toUShort());
        const QString tokenUrl = m_tokenUrlByPort.value(p, QString());
        const QString url = tokenUrl.isEmpty()
            ? QStringLiteral("http://127.0.0.1:%1").arg(port)
            : tokenUrl;
        emit webViewRequested(url);
    } else if (chosen == dshAct) {
        // 按选中记录端口打开 DSH 客户端(127.0.0.1:port),去重由 MainWindow 处理
        emit dshChatRequested(m_model2->item(row, 7)->text());
    } else if (chosen == restartAct) {
        restartService(row);
    } else if (chosen == closeAct) {
        closeService(row);
    } else if (chosen == webSvcAct) {
        // 以web方式打开服务：在 MDI 区域新增一个 CDSCmdView 终端窗口，
        // 执行对应的 web 服务命令，窗口标题为“列 Profile:行 端口”
        openServiceTerminal(row, true);
    } else if (chosen == svcAct) {
        // 开启服务：仅在服务已关闭时有效; 若已启动则报错
        const QStandardItem *st = m_model2->item(row, 8);
        if (st && st->text() == QStringLiteral("已经启动")) {
            QMessageBox::warning(this, QStringLiteral("提示"),
                                 QStringLiteral("服务开启状态,不能再开启"));
            return;
        }
        // 开启服务：在 MDI 区域新增一个 CDSCmdView 终端窗口，
        // 执行对应的服务命令，窗口标题为“列 Profile:行 端口”
        openServiceTerminal(row);
    }
}

QString CDSHSrcManager::serviceKeyForRow(int row) const
{
    return m_model2->item(row, 0)->text()
           + QStringLiteral("|")
           + m_model2->item(row, 7)->text();
}

// 记录服务启动后终端识别到的带 token 网址(按端口保存, 供“打开网站”等使用)
void CDSHSrcManager::recordWebUrl(const QString &url)
{
    const QUrl u(url);
    const int port = u.port();
    if (port > 0)
        m_tokenUrlByPort[static_cast<quint16>(port)] = url;
}

QString CDSHSrcManager::tokenUrlForPort(quint16 port) const
{
    return m_tokenUrlByPort.value(port);
}

void CDSHSrcManager::startService(int row, bool webMode)
{
#ifdef _MSC_VER
    // MSVC：SEH 捕获硬崩溃（访问违例/栈溢出等），避免 Release 下无声退出
    __try {
        startServiceImpl(row, webMode);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fprintf(stderr, "[startService] SEH exception code = 0x%lX\n",
                (unsigned long)GetExceptionCode());
    }
#else
    // MinGW/GCC：无 SEH，直接调用（C++ 异常已在 startServiceImpl 内捕获）
    startServiceImpl(row, webMode);
#endif
}

void CDSHSrcManager::startServiceImpl(int row, bool webMode)
{
    try {
    const QString preKey = serviceKeyForRow(row);
    // 服务实际运行端口：web 模式固定使用 8888（独立于该行“端口”列）
    const quint16 effPort = webMode ? 8888 : m_model2->item(row, 7)->text().toUShort();

    // 预检 1：服务已在本程序管理中且确认运行 → 提示已开始
    for (const ServiceInfo &svc : m_services) {
        if (svc.key == preKey && svc.running) {
            QMessageBox::information(this, QStringLiteral("提示"),
                                     QStringLiteral("服务已经开始了！"));
            return;
        }
    }

    // 预检 2：端口可连上 → 端口已被占用，无法开启（web 模式检测固定 8888）
    if (effPort != 0) {
        QTcpSocket probe;
        probe.connectToHost(QHostAddress::LocalHost, effPort);
        if (probe.waitForConnected(500)) {
            QMessageBox::warning(this, QStringLiteral("提示"),
                                 QStringLiteral("端口已经被占用，服务无法开启！"));
            return;
        }
    }

    // install / build 都必须已执行
    if (m_model2->item(row, 3)->text() != QStringLiteral("是")
        || m_model2->item(row, 4)->text() != QStringLiteral("是")) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("实例未执行！"));
        return;
    }

    const QString dshHome = m_model2->item(row, 5)->text();
    const QString profile = m_model2->item(row, 6)->text(); // Profile（原运行类型）
    const QString port = m_model2->item(row, 7)->text();

    // 源码目录：来自上面表（表 1）当前选中的记录；Git/Node 目录来自环境设置
    const QModelIndex cur = m_table1->currentIndex();
    const QString srcDir = cur.isValid()
        ? m_model1->item(cur.row(), 2)->text()
        : QString();
    const QString gitDir = CApplication::instance()->gitDir();
    const QString nodeDir = CApplication::instance()->nodeDir();

    if (dshHome.isEmpty() || profile.isEmpty() || port.isEmpty() || srcDir.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("配置不完整，无法启动！"));
        return;
    }

    // 命令执行前，弹出对话框显示四条命令的内容（命令保留）
    const QString cmd1 = QStringLiteral("set DSH_HOME=%1").arg(dshHome);
    const QString cmd2 = QStringLiteral("cd /d \"%1\"").arg(srcDir);
    QStringList pathParts;
    pathParts << QStringLiteral("\"%1\"").arg(srcDir);
    if (!gitDir.isEmpty()) {
        pathParts << QStringLiteral("\"%1\"").arg(gitDir);
    }
    if (!nodeDir.isEmpty()) {
        pathParts << QStringLiteral("\"%1\"").arg(nodeDir);
    }
    pathParts << QStringLiteral("\"%1\\node_modules\\.bin\"").arg(srcDir);
    pathParts << QStringLiteral("%PATH%");
    const QString cmd3 = QStringLiteral("path %1").arg(pathParts.join(QLatin1Char(';')));
    // 最终命令：用 & 串接两条命令，cmd 依序执行（先拷贝 web 目录，再启动 dsh 服务）。
    // 说明：
    //   1) 用 & 而非 && —— robocopy 成功时返回码为 1（非 0），若用 && 会被误判为失败而跳过 pnpm dsh。
    //   2) 之前用 QProcess::start("cmd.exe", {"/c", cmd4}) 时，QProcess 会对含空格/引号的 cmd4
    //      额外多包一层引号，导致 cmd /c 遇到「一层引号里又含多个内部引号 + &」解析错乱，
    //      robocopy 或 & 之后的 pnpm dsh 被吞掉。改用 setNativeArguments 把命令原样传给 cmd.exe /c，
    //      不再多加外层引号，& 即被 cmd 正确解析为先后两条命令。
    QString cmd4;
    if (webMode) {
        // 以web方式打开服务：去掉 robocopy 拷贝，也不再用该行 Profile/端口的 pnpm dsh，
        // 改为用固定命令启动（web 配置、8888 端口）
        cmd4 = QStringLiteral("pnpm dsh --profile web --port 8888");
    } else if (profile.compare(QStringLiteral("web"), Qt::CaseInsensitive) != 0
               && profile.compare(QStringLiteral("desktop"), Qt::CaseInsensitive) != 0) {
        // 开启服务：先 robocopy 拷贝 web 目录到该行 Profile 目录, 再启动(用 & 串接;
        // robocopy 成功返回码为 1, 故用 & 而非 &&, 否则会误判失败跳过 pnpm dsh)。
        cmd4 = QStringLiteral(
            "robocopy \"%1\\profiles\\web\" \"%1\\profiles\\%2\" /E /R:1 /W:1 & "
            "pnpm dsh --profile %2 --port %3 --no-open")
            .arg(dshHome, profile, port);
    } else {
        // Profile 为 web/desktop 时不拷贝 web 目录, 直接按该 Profile/端口启动
        cmd4 = QStringLiteral("pnpm dsh --profile %1 --port %2 --no-open")
            .arg(profile, port);
    }

    // 弹出可复制的确认对话框：命令文本可选中复制，点“执行”才继续
    QDialog confirm(this);
    confirm.setWindowTitle(QStringLiteral("确认执行"));
    confirm.setMinimumWidth(560);
    auto *confirmLayout = new QVBoxLayout(&confirm);
    auto *tip = new QLabel(QStringLiteral("以下命令将按顺序执行（可选中复制）："), &confirm);
    confirmLayout->addWidget(tip);
    auto *cmdView = new QPlainTextEdit(&confirm);
    cmdView->setReadOnly(true);
    cmdView->setPlainText(QStringLiteral("%1\n%2\n%3\n%4\n").arg(cmd1, cmd2, cmd3, cmd4));
    cmdView->setFixedHeight(160);
    confirmLayout->addWidget(cmdView);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, &confirm);
    buttons->button(QDialogButtonBox::Yes)->setText(QStringLiteral("执行"));
    buttons->button(QDialogButtonBox::No)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &confirm, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &confirm, &QDialog::reject);
    confirmLayout->addWidget(buttons);
    if (confirm.exec() != QDialog::Accepted) {
        return; // 点“取消”
    }

    // 作为后台服务启动：set/cd/path 翻译为进程环境变量 + 工作目录，
    // 进程由程序持有，停止时用 taskkill /T 结束整个进程树
    const QString key = serviceKeyForRow(row);

    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("DSH_HOME"), dshHome);
    QStringList pathList;
    pathList << srcDir;
    if (!gitDir.isEmpty()) {
        pathList << gitDir;
    }
    if (!nodeDir.isEmpty()) {
        pathList << nodeDir;
    }
    pathList << srcDir + QStringLiteral("\\node_modules\\.bin");
    pathList << env.value(QStringLiteral("PATH"));
    env.insert(QStringLiteral("PATH"), pathList.join(QLatin1Char(';')));
    proc->setProcessEnvironment(env);
    proc->setWorkingDirectory(srcDir);

    ServiceInfo info;
    info.key = key;
    info.port = effPort;   // web 模式固定 8888
    info.webMode = webMode;
    info.proc = proc;
    m_services.append(info);

    // 注意：不再连接 QProcess::finished 处理器。
    // 之前它会在 killBackground 的 waitForFinished 期间回调、从 m_services 移除条目，
    // 导致 killBackground 持有的引用悬垂，读 svc.port 崩溃。
    // 进程自行退出/死亡由 pollServices（启动阶段）和 monitorServices（运行阶段）兜底检测。

    // 隐藏窗口运行；用 setNativeArguments 把命令原样传给 cmd.exe /c，
    // 不让 QProcess 再对含空格/引号的命令额外包一层引号，从而 cmd /c 能正确解析 & 连续的两条命令。
    proc->setNativeArguments(QStringLiteral("/c ") + cmd4);
    proc->start(QStringLiteral("cmd.exe"));
    if (proc->error() == QProcess::FailedToStart) {
        m_services.removeLast();
        proc->deleteLater();
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("服务启动失败（进程未能运行）！"));
        return;
    }

    // 登记端口：程序退出时按端口自动关闭该服务
    CApplication::instance()->registerServicePort(info.port);

    // 开始轮询检测端口（1 分钟内）
    if (!m_pollTimer) {
        m_pollTimer = new QTimer(this);
        connect(m_pollTimer, &QTimer::timeout, this, &CDSHSrcManager::pollServices);
    }
    m_pollTimer->start(500);
    // 启动检测期间显示“启动中...”（避免误显示“已经关闭”）
    setRowServiceStateTextByKey(key, QStringLiteral("启动中..."));
    } catch (const std::exception &e) {
        QMessageBox::critical(this, QStringLiteral("异常"),
                              QStringLiteral("开启服务发生异常：\n%1").arg(e.what()));
    } catch (...) {
        QMessageBox::critical(this, QStringLiteral("异常"),
                              QStringLiteral("开启服务发生未知异常！"));
    }
}

void CDSHSrcManager::stopService(int row)
{
    const QString key = serviceKeyForRow(row);
    for (int i = 0; i < m_services.size(); ++i) {
        if (m_services.at(i).key == key) {
            // 直接把后台进程树关掉
            killBackground(m_services[i]);
            m_services.removeAt(i);
            break;
        }
    }
    setRowServiceStateByKey(key, false);
    QMessageBox::information(this, QStringLiteral("提示"),
                             QStringLiteral("服务已停止！"));
}

void CDSHSrcManager::closeService(int row, bool confirm)
{
    // 1. 判断该行“服务是否启动”列是否为“已经启动”
    const QStandardItem *stateItem = m_model2->item(row, 8);
    const bool started = stateItem && stateItem->text() == QStringLiteral("已经启动");

    // 仅当服务已启动时才可关闭; 已关闭则报错
    if (!started) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("服务关闭状态,不能再关闭"));
        return;
    }

    // 执行前确认(仅“关闭服务”菜单弹出; 重启服务内部用 confirm=false 已先确认)
    if (confirm) {
        const auto ret = QMessageBox::question(
            this, QStringLiteral("确认"), QStringLiteral("确定要关闭服务吗?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;
    }

    // 2. 根据 QMdiArea, 找到标题为“行Profile:行端口”的终端窗口并关闭(由 MainWindow 处理)
    const QString profile = m_model2->item(row, 6)->text();
    const QString port = m_model2->item(row, 7)->text();
    const QString title = profile + QLatin1Char(':') + port;
    emit closeCmdTerminalRequested(title);
}

void CDSHSrcManager::restartService(int row)
{
    // 仅在服务已经启动时有效; 已关闭则无从重启
    const QStandardItem *stateItem = m_model2->item(row, 8);
    const bool started = stateItem && stateItem->text() == QStringLiteral("已经启动");
    if (!started) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("服务关闭状态,不能再重启"));
        return;
    }

    // 执行前确认
    const auto ret = QMessageBox::question(
        this, QStringLiteral("确认"), QStringLiteral("确定要重启服务吗?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;

    // 等待“已经关闭”状态信号后再开启
    m_pendingRestart = true;
    m_pendingRestartKey = serviceKeyForRow(row);
    m_pendingRestartRow = row;

    // 先执行“关闭服务”(关闭终端窗口; confirm=false 避免再次确认)
    closeService(row, false);
}

void CDSHSrcManager::onServiceStateChanged(const QString &key, bool running)
{
    // 重启流程: 等待的 key 状态变为“已经关闭”后, 再执行“开启服务”
    if (m_pendingRestart && key == m_pendingRestartKey && !running) {
        m_pendingRestart = false;
        const int row = m_pendingRestartRow;
        m_pendingRestartRow = -1;
        openServiceTerminal(row); // 开启服务(自带 WebView)
    }
}

void CDSHSrcManager::monitorTable2ServiceState()
{
    if (!m_model2)
        return;
    // 上一轮还没探测完(行多时异步串行), 跳过本轮, 避免重入/堆积
    if (m_stateProbeRow >= 0)
        return;
    m_stateProbeRow = 0;
    probeNextRow();
}

void CDSHSrcManager::setStateCell(int row, bool up, const QString &key)
{
    QStandardItem *it = m_model2->item(row, 8);
    const QString want = up ? QStringLiteral("已经启动") : QStringLiteral("已经关闭");
    if (it && it->text() != want) {
        it->setText(want);
        emit serviceStateChanged(key, up);
    }
}

void CDSHSrcManager::probeNextRow()
{
    if (!m_model2 || m_stateProbeRow < 0 || m_stateProbeRow >= m_model2->rowCount()) {
        m_stateProbeRow = -1; // 本轮结束
        return;
    }
    const int r = m_stateProbeRow;
    QStandardItem *portItem = m_model2->item(r, 7);
    QStandardItem *idItem = m_model2->item(r, 0);
    if (!portItem) {
        ++m_stateProbeRow;
        probeNextRow();
        return;
    }

    const quint16 port = portItem->text().toUShort();
    const QString key = (idItem ? idItem->text() : QString())
                        + QLatin1Char('|') + portItem->text();

    // 端口为空/0: 直接视为“已经关闭”
    if (port == 0) {
        setStateCell(r, false, key);
        ++m_stateProbeRow;
        probeNextRow();
        return;
    }

    // 异步探测当前行端口(每次新建 socket, 用信号回调, 不阻塞主线程; 行多时自动串行)
    auto *sock = new QTcpSocket(this);
    connect(sock, &QTcpSocket::connected, this, [this, r, key, sock]() {
        setStateCell(r, true, key);
        sock->deleteLater();
        ++m_stateProbeRow;
        probeNextRow();
    });
    connect(sock, &QTcpSocket::errorOccurred, this, [this, r, key, sock]() {
        setStateCell(r, false, key);
        sock->deleteLater();
        ++m_stateProbeRow;
        probeNextRow();
    });
    sock->connectToHost(QHostAddress::LocalHost, port);
}

void CDSHSrcManager::openCmdWindow(int row)
{
    const QString dshHome = m_model2->item(row, 5)->text();
    const QModelIndex cur = m_table1->currentIndex();
    const QString srcDir = cur.isValid()
        ? m_model1->item(cur.row(), 2)->text()
        : QString();
    const QString gitDir = CApplication::instance()->gitDir();
    const QString nodeDir = CApplication::instance()->nodeDir();

    if (dshHome.isEmpty() || srcDir.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("配置不完整，无法打开 cmd！"));
        return;
    }

    // 插件市场镜像（为空时用默认）
    QString mirror = CApplication::instance()->pluginMirror();
    if (mirror.isEmpty()) {
        mirror = QStringLiteral("https://registry.npmmirror.com");
    }

    // 直接命令行方式在本机不可靠（窗口不显示），因此仍用批处理执行预设命令；
    // 批处理末尾自删除，不残留文件。
    QString bat;
    // 窗口标题：使用该记录的 ID（mainid），用于按标题找到窗口并注入命令
    const QString srcId = m_model2->item(row, 0)->text();
    bat += QStringLiteral("title %1\r\n")
        .arg(srcId.isEmpty() ? QStringLiteral("DSH") : srcId);
    bat += QStringLiteral("set DSH_HOME=%1\r\n").arg(dshHome);
    bat += QStringLiteral("cd /d \"%1\"\r\n").arg(srcDir);
    // path 源码目录;git目录;Node目录;源码目录\node_modules\.bin;%PATH%
    QStringList pathParts;
    pathParts << QStringLiteral("\"%1\"").arg(srcDir);
    if (!gitDir.isEmpty()) {
        pathParts << QStringLiteral("\"%1\"").arg(gitDir);
    }
    if (!nodeDir.isEmpty()) {
        pathParts << QStringLiteral("\"%1\"").arg(nodeDir);
    }
    pathParts << QStringLiteral("\"%1\\node_modules\\.bin\"").arg(srcDir);
    pathParts << QStringLiteral("%PATH%");
    bat += QStringLiteral("path %1\r\n").arg(pathParts.join(QLatin1Char(';')));
    bat += QStringLiteral("npm config set registry %1\r\n").arg(mirror);
    // 自删除：执行完后删除本批处理文件（窗口由 cmd /k 保持打开）
    bat += QStringLiteral("del \"%~f0\"\r\n");

    // 每次打开使用唯一文件名（UUID），避免不同项目之间互相覆盖/混淆脚本内容；
    // 批处理末尾自删除，不残留文件。
    // 每次打开使用端口号作为 bat 文件名（端口号一般不会重复）；
    // 若端口为空则兜底用 UUID，保证文件名唯一、互不覆盖
    QString portText = m_model2->item(row, 7)->text();
    if (portText.isEmpty()) {
        portText = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    const QString batPath = CApplication::instance()->configDir()
                            + QStringLiteral("/opencmd_")
                            + portText
                            + QStringLiteral(".bat");
    QDir().mkpath(QFileInfo(batPath).absolutePath());
    QFile file(batPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("无法写入 opencmd.bat！"));
        return;
    }
    file.write(bat.toLocal8Bit()); // cmd 默认代码页（中文系统为 GBK）
    file.close();

    // 打开独立 cmd 窗口执行该批处理（/k 保持窗口打开，可继续输入插件命令）
    if (!launchCmdWindow(QStringLiteral("\"%1\"").arg(batPath), srcDir)) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("无法启动 cmd 窗口！"));
    }
}

void CDSHSrcManager::openServiceTerminal(int row, bool webMode)
{
    const QString dshHome = m_model2->item(row, 5)->text();
    const QString profile = m_model2->item(row, 6)->text(); // Profile
    const QString port = m_model2->item(row, 7)->text();    // 端口

    // 源码目录：来自上面表（表 1）当前选中的记录；Git/Node 目录来自环境设置
    const QModelIndex cur = m_table1->currentIndex();
    const QString srcDir = cur.isValid()
        ? m_model1->item(cur.row(), 2)->text()
        : QString();
    const QString gitDir = CApplication::instance()->gitDir();
    const QString nodeDir = CApplication::instance()->nodeDir();

    if (dshHome.isEmpty() || profile.isEmpty() || port.isEmpty() || srcDir.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("配置不完整，无法打开服务！"));
        return;
    }

    // 窗口标题 = 右键选择行“Profile”列:右键选择行“端口”列
    const QString title = profile + QLatin1Char(':') + port;

    // 命令序列（同 startServiceImpl，但改为在 CDSCmdView 终端窗口执行）
    QStringList lines;
    lines << QStringLiteral("set DSH_HOME=%1").arg(dshHome);
    lines << QStringLiteral("cd /d \"%1\"").arg(srcDir);
    QStringList pathParts;
    pathParts << QStringLiteral("\"%1\"").arg(srcDir);
    if (!gitDir.isEmpty()) {
        pathParts << QStringLiteral("\"%1\"").arg(gitDir);
    }
    if (!nodeDir.isEmpty()) {
        pathParts << QStringLiteral("\"%1\"").arg(nodeDir);
    }
    pathParts << QStringLiteral("\"%1\\node_modules\\.bin\"").arg(srcDir);
    pathParts << QStringLiteral("%PATH%");
    lines << QStringLiteral("path %1").arg(pathParts.join(QLatin1Char(';')));

    if (webMode) {
        // 以web方式打开服务：固定命令 web 配置 / 8888 端口
        lines << QStringLiteral("pnpm dsh --profile web --port 8888");
    } else {
        // 开启服务：Profile 为 web/desktop 时不拷贝 web 目录(直接用该配置), 否则先 robocopy 再启动。
        // CDSCmdView 是逐行执行, robocopy 单独一行(无需 &); profile 端口沿用该行。
        if (profile.compare(QStringLiteral("web"), Qt::CaseInsensitive) != 0
            && profile.compare(QStringLiteral("desktop"), Qt::CaseInsensitive) != 0) {
            lines << QStringLiteral("robocopy \"%1\\profiles\\web\" \"%1\\profiles\\%2\" /E /R:1 /W:1")
                .arg(dshHome, profile);
        }
        lines << QStringLiteral("pnpm dsh --profile %1 --port %2 --no-open")
            .arg(profile, port);
    }

    // 执行前弹出确认对话框, 命令可选中复制, 便于纠错; 点“执行”才继续
    QDialog confirm(this);
    confirm.setWindowTitle(QStringLiteral("确认执行"));
    confirm.setMinimumWidth(600);
    auto *confirmLayout = new QVBoxLayout(&confirm);
    auto *tip = new QLabel(QStringLiteral("将执行以下命令(可选中复制)：" ), &confirm);
    confirmLayout->addWidget(tip);
    auto *cmdView = new QPlainTextEdit(&confirm);
    cmdView->setReadOnly(true);
    cmdView->setFont(QFont(QStringLiteral("Consolas"), 10));
    cmdView->setPlainText(lines.join(QLatin1Char('\n')));
    cmdView->setFixedHeight(180);
    confirmLayout->addWidget(cmdView);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Yes | QDialogButtonBox::No, &confirm);
    buttons->button(QDialogButtonBox::Yes)->setText(QStringLiteral("执行"));
    buttons->button(QDialogButtonBox::No)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, &confirm, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &confirm, &QDialog::reject);
    confirmLayout->addWidget(buttons);
    if (confirm.exec() != QDialog::Accepted) {
        return; // 点“取消”
    }

    // 请求 MainWindow 在 MDI 中打开一个 CDSCmdView 终端窗口并执行这些命令
    emit cmdTerminalRequested(title, srcDir, lines);

    // 仅“开启服务”(webMode=false, 命令带 --no-open 不会自己开浏览器)时,
    // 轮询端口维护“服务是否启动”状态列; 网页(带 token 网址)由终端识别 "dsh web:" 行后
    // 交给 MainWindow 用自带的 WebView 打开。
    if (!webMode) {
        startWebViewProbe(QStringLiteral("http://127.0.0.1:%1").arg(port),
                          static_cast<quint16>(port.toUShort()), serviceKeyForRow(row));
    }
}

void CDSHSrcManager::startWebViewProbe(const QString &url, quint16 port, const QString &key)
{
    // 停掉上一次探测
    if (m_webProbeTimer) {
        m_webProbeTimer->stop();
    }

    if (!m_webProbeSocket) {
        m_webProbeSocket = new QTcpSocket(this);
        // 端口可连上 => 服务已启动: 更新状态列并打开 WebView, 之后转慢速监测
        connect(m_webProbeSocket, &QTcpSocket::connected, this, [this]() {
            if (m_webProbeConfirmed)
                return;
            m_webProbeConfirmed = true;
            setRowServiceStateByKey(m_webProbeKey, true);   // “已经启动”
            // 不再用不带 token 的地址打开网页: 带 token 的网址由 CDSCmdView 解析
            // "dsh web:" 行后通过 webUrlCaptured 提供, 由 MainWindow 打开。
            if (m_webProbeTimer) {
                m_webProbeTimer->start(3000); // 确认后改为 3s 慢速监测服务是否仍在
            }
        });
        // 连接被拒绝(端口不再监听) => 服务已停止
        connect(m_webProbeSocket, &QTcpSocket::errorOccurred, this, [this]() {
            if (m_webProbeConfirmed
                && m_webProbeSocket->error() == QAbstractSocket::ConnectionRefusedError) {
                m_webProbeConfirmed = false;
                setRowServiceStateByKey(m_webProbeKey, false); // “已经关闭”
                if (m_webProbeTimer) {
                    m_webProbeTimer->stop();
                }
            }
        });
    }
    if (!m_webProbeTimer) {
        m_webProbeTimer = new QTimer(this);
        m_webProbeTimer->setInterval(400); // 启动阶段 400ms 轮询一次
        connect(m_webProbeTimer, &QTimer::timeout, this, [this]() {
            if (m_webProbeUrl.isEmpty()) {
                m_webProbeTimer->stop();
                return;
            }
            if (!m_webProbeConfirmed && ++m_webProbeTries > 150) { // 约 60s 还没起来
                m_webProbeTimer->stop();
                m_webProbeUrl.clear();
                return;
            }
            if (m_webProbeSocket && m_webProbeSocket->state() == QAbstractSocket::UnconnectedState) {
                m_webProbeSocket->connectToHost(QHostAddress::LocalHost, m_webProbePort);
            }
        });
    }

    m_webProbeUrl = url;
    m_webProbeKey = key;
    m_webProbePort = port;
    m_webProbeTries = 0;
    m_webProbeConfirmed = false;
    if (m_webProbeSocket) {
        m_webProbeSocket->abort();
        m_webProbeSocket->connectToHost(QHostAddress::LocalHost, port);
    }
    if (m_webProbeTimer) {
        m_webProbeTimer->start(400); // 若服务已就绪, 首次即可连上
    }
}

void CDSHSrcManager::pollServices()
{
    for (int i = 0; i < m_services.size(); ++i) {
        ServiceInfo &svc = m_services[i];
        if (svc.running) {
            continue;
        }
        // 进程已退出且从未确认启动：判定为停止
        if (svc.proc->state() == QProcess::NotRunning) {
            setRowServiceStateByKey(svc.key, false);
            if (svc.sock) {
                svc.sock->deleteLater();
            }
            m_services.removeAt(i--);
            continue;
        }
        // 探测端口 127.0.0.1:port
        if (!svc.sock) {
            svc.sock = new QTcpSocket(this);
            const QString key = svc.key;
            connect(svc.sock, &QTcpSocket::connected, this,
                    [this, key]() { markServiceRunning(key); });
            connect(svc.sock, &QTcpSocket::errorOccurred, this,
                    [this, key]() {
                        for (int i = 0; i < m_services.size(); ++i) {
                            ServiceInfo &s = m_services[i];
                            if (s.key == key && s.sock) {
                                if (s.running) {
                                    // 健康检查失败：服务已死 → 杀掉后台，标记已经关闭
                                    killBackground(s);
                                    m_services.removeAt(i);
                                    setRowServiceStateByKey(key, false);
                                } else {
                                    s.sock->abort(); // 启动检测失败：下一轮重试
                                }
                                break;
                            }
                        }
                    });
        }
        svc.sock->abort();
        svc.sock->connectToHost(QHostAddress::LocalHost, svc.port);
    }

    // 没有待检测的服务时停止启动检测定时器
    bool hasPending = false;
    for (const ServiceInfo &svc : m_services) {
        if (!svc.running) {
            hasPending = true;
            break;
        }
    }
    if (!hasPending) {
        m_pollTimer->stop();
    }
}

void CDSHSrcManager::monitorServices()
{
    // 每 1 分钟：扫描所有运行中服务的端口，连不上则杀掉后台并标记已经关闭
    bool anyRunning = false;
    for (ServiceInfo &svc : m_services) {
        if (!svc.running) {
            continue;
        }
        anyRunning = true;
        if (!svc.sock) {
            continue; // 理论上启动阶段已建好，跳过本轮
        }
        svc.sock->abort();
        svc.sock->connectToHost(QHostAddress::LocalHost, svc.port);
    }
    if (!anyRunning && m_monitorTimer) {
        m_monitorTimer->stop();
    }
}

void CDSHSrcManager::markServiceRunning(const QString &key)
{
    // 仅在“首次确认启动”时打开网页：健康检查（每分钟）重连成功时 svc.running 已为 true，
    // 不会再次触发，避免网页窗口被定时反复打开/激活（即“网页定时刷新”）
    bool firstStart = false;
    bool webMode = false;
    for (ServiceInfo &svc : m_services) {
        if (svc.key == key) {
            webMode = svc.webMode;
            if (!svc.running) {
                svc.running = true;
                setRowServiceStateByKey(key, true);
                firstStart = true;
            }
            break;
        }
    }

    // 服务已启动：在 QMdiArea 中打开该端口的 WebEngine 网页窗口（仅首次启动时）
    // （命令带 --no-open，dsh 不会再自行打开默认浏览器；
    //   同一端口只开一个窗口，重复启动时 MainWindow 会激活已有窗口）
    // 以web方式打开的服务仅启动服务，不自动打开 MDI 网页窗口
    if (firstStart && !webMode) {
        const QString port = key.section(QLatin1Char('|'), 1, 1);
        const quint16 p = static_cast<quint16>(port.toUShort());
        const QString tokenUrl = m_tokenUrlByPort.value(p, QString());
        const QString url = tokenUrl.isEmpty()
            ? QStringLiteral("http://127.0.0.1:%1").arg(port)
            : tokenUrl;
        emit webViewRequested(url);
    }

    // 确保周期健康检查定时器在运行（每 1 分钟）
    if (!m_monitorTimer) {
        m_monitorTimer = new QTimer(this);
        connect(m_monitorTimer, &QTimer::timeout,
                this, &CDSHSrcManager::monitorServices);
    }
    m_monitorTimer->start(60000);
}

void CDSHSrcManager::setRowServiceStateByKey(const QString &key, bool running)
{
    const QString text = running ? QStringLiteral("已经启动")
                                 : QStringLiteral("已经关闭");
    bool changed = false;
    for (int r = 0; r < m_model2->rowCount(); ++r) {
        if (serviceKeyForRow(r) == key) {
            QStandardItem *it = m_model2->item(r, 8);
            if (it && it->text() != text) {
                it->setText(text);
                changed = true;
            }
            break;
        }
    }
    if (changed)
        emit serviceStateChanged(key, running);
}

void CDSHSrcManager::setRowServiceStateTextByKey(const QString &key, const QString &text)
{
    for (int r = 0; r < m_model2->rowCount(); ++r) {
        if (serviceKeyForRow(r) == key) {
            m_model2->item(r, 8)->setText(text);
            break;
        }
    }
}

void CDSHSrcManager::killBackground(ServiceInfo &svc)
{
    const qint64 pid = svc.proc->processId();
    // 结束整个进程树（cmd → npx → node），避免服务残留
    QProcess killer;
    killer.start(QStringLiteral("taskkill"),
                 {QStringLiteral("/PID"), QString::number(pid),
                  QStringLiteral("/T"), QStringLiteral("/F")});
    killer.waitForFinished(5000);
    // 等待服务进程真正结束，避免 QProcess 销毁时仍在运行产生警告
    svc.proc->kill();
    svc.proc->waitForFinished(3000);
    // 保险：按端口再查一次监听进程并结束（防止孤儿进程）
    CApplication::instance()->killServicePort(svc.port);
    CApplication::instance()->unregisterServicePort(svc.port);
    if (svc.sock) {
        svc.sock->deleteLater();
        svc.sock = nullptr;
    }
}
