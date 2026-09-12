#ifndef DSCONPTY_H
#define DSCONPTY_H

// ============================================================================
// CDSConPty —— Windows ConPTY 伪控制台进程包装
//
// 用 Windows 10 1809+ 的 Pseudo Console API 启动一个真实交互式控制台进程
// (默认 cmd.exe), 以获得正确的提示符回显、方向键、行编辑等行为。
//
// 选择动态加载 CreatePseudoConsole/ResizePseudoConsole/ClosePseudoConsole,
// 这样 MSVC/MinGW 均无需链接 conpty.lib, 也不会拖入 lxqt-build-tools 的
// 任何构建依赖。函数由 KernelBase.dll / kernel32.dll 导出(Win10 1809+)。
//
// 约定(本项目规则): 不使用智能指针; 不使用 QTimer。
//   读线程用阻塞 ReadFile + CancelIoEx 终止, 输出经 Qt queued signal 送回主线程。
// ============================================================================

#include <QObject>
#include <QByteArray>
#include <QStringList>

#include <atomic>
#include <thread>

// ConPTY 依赖的 Windows 类型: 头文件必须在类定义前看到它们
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// ConPTY 句柄类型(未 include conpty.h 时自行定义)
#ifndef _HPCON_DEFINED
typedef void *HPCON;
#define _HPCON_DEFINED
#endif

class CDSConPty : public QObject
{
    Q_OBJECT

public:
    explicit CDSConPty(QObject *parent = nullptr);
    ~CDSConPty() override;

    // 启动一个伪控制台进程(默认 cmd.exe)。成功返回 true。
    bool start(const QString &program = QStringLiteral("cmd.exe"),
               const QStringList &args = QStringList(),
               const QString &workingDir = QString());

    bool isRunning() const { return m_running; }
    DWORD pid() const { return m_pid; }
    QString program() const { return m_program; }

    // 写入输入(键盘/粘贴)
    bool write(const QByteArray &data);
    // 调整伪控制台尺寸(行列)
    void resize(int rows, int cols);
    // 终止进程并释放资源(析构也会调用)
    void close();

    // --- ConPTY 函数指针类型(public, 供文件级加载工具函数访问)---
    typedef HRESULT(WINAPI *CreatePseudoConsoleFn)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
    typedef VOID(WINAPI *ClosePseudoConsoleFn)(HPCON);
    typedef BOOL(WINAPI *ResizePseudoConsoleFn)(HPCON, COORD);

signals:
    void outputReady(const QByteArray &data);  // 伪终端输出(UTF-8 / VT 流)
    void started();
    void processExited();
    void errorOccurred(const QString &message);

private:
    void readerLoop();                  // 读线程体
    bool loadConPty();                  // 动态加载 ConPTY API
    QString quoteCommandLineArg(const QString &arg) const;

    CreatePseudoConsoleFn m_createPseudoConsole = nullptr;
    ClosePseudoConsoleFn m_closePseudoConsole    = nullptr;
    ResizePseudoConsoleFn m_resizePseudoConsole  = nullptr;

    HPCON m_hConsole = nullptr;              // 伪控制台
    HANDLE m_hInputWrite = INVALID_HANDLE_VALUE; // 父进程写输入
    HANDLE m_hOutputRead = INVALID_HANDLE_VALUE; // 父进程读输出
    HANDLE m_hProcess = nullptr;             // 子进程句柄

    std::thread m_reader;
    std::atomic_bool m_stop{false};
    std::atomic_bool m_running{false};

    DWORD m_pid = 0;
    QString m_program;
};

#endif // DSCONPTY_H
