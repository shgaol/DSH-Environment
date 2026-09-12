#include "DSConPty.h"

#include <windows.h>

// KernelBase(较新)或 kernel32(较旧)加载 ConPTY 导出。均无需链接 conpty.lib。
static bool locCreatePseudoConsole(CDSConPty::CreatePseudoConsoleFn &out)
{
    HMODULE mod = GetModuleHandleW(L"kernel32.dll");
    if (!mod)
        mod = GetModuleHandleW(L"KernelBase.dll");
    if (!mod)
        return false;
    FARPROC p = GetProcAddress(mod, "CreatePseudoConsole");
    if (!p)
        p = GetProcAddress(GetModuleHandleW(L"KernelBase.dll"), "CreatePseudoConsole");
    if (!p)
        return false;
    out = reinterpret_cast<CDSConPty::CreatePseudoConsoleFn>(p);
    return true;
}

static bool locClosePseudoConsole(CDSConPty::ClosePseudoConsoleFn &out)
{
    HMODULE mod = GetModuleHandleW(L"kernel32.dll");
    if (!mod)
        mod = GetModuleHandleW(L"KernelBase.dll");
    if (!mod)
        return false;
    FARPROC p = GetProcAddress(mod, "ClosePseudoConsole");
    if (!p)
        p = GetProcAddress(GetModuleHandleW(L"KernelBase.dll"), "ClosePseudoConsole");
    if (!p)
        return false;
    out = reinterpret_cast<CDSConPty::ClosePseudoConsoleFn>(p);
    return true;
}

static bool locResizePseudoConsole(CDSConPty::ResizePseudoConsoleFn &out)
{
    HMODULE mod = GetModuleHandleW(L"kernel32.dll");
    if (!mod)
        mod = GetModuleHandleW(L"KernelBase.dll");
    if (!mod)
        return false;
    FARPROC p = GetProcAddress(mod, "ResizePseudoConsole");
    if (!p)
        p = GetProcAddress(GetModuleHandleW(L"KernelBase.dll"), "ResizePseudoConsole");
    if (!p)
        return false;
    out = reinterpret_cast<CDSConPty::ResizePseudoConsoleFn>(p);
    return true;
}

CDSConPty::CDSConPty(QObject *parent)
    : QObject(parent)
{
    loadConPty();
}

CDSConPty::~CDSConPty()
{
    close();
}

bool CDSConPty::loadConPty()
{
    return locCreatePseudoConsole(m_createPseudoConsole) &&
           locClosePseudoConsole(m_closePseudoConsole) &&
           locResizePseudoConsole(m_resizePseudoConsole);
}

QString CDSConPty::quoteCommandLineArg(const QString &arg) const
{
    if (arg.isEmpty())
        return QStringLiteral("\"\"");
    bool needQuote = arg.contains(QLatin1Char(' ')) || arg.contains(QLatin1Char('"')) ||
                     arg.contains(QLatin1Char('\t'));
    if (needQuote) {
        QString s = arg;
        s.replace(QLatin1String("\\"), QLatin1String("\\\\"));
        s.replace(QLatin1String("\""), QLatin1String("\\\""));
        return QLatin1Char('"') + s + QLatin1Char('"');
    }
    return arg;
}

bool CDSConPty::start(const QString &program, const QStringList &args, const QString &workingDir)
{
    if (m_running)
        return false;

    if (!m_createPseudoConsole || !m_closePseudoConsole || !m_resizePseudoConsole) {
        if (!loadConPty()) {
            emit errorOccurred(QStringLiteral("当前系统不支持 ConPTY (需 Windows 10 1809+)"));
            return false;
        }
    }

    m_program = program;
    m_stop = false;

    // 1) 建输入/输出管道
    HANDLE hInRead = nullptr, hInWrite = nullptr;
    HANDLE hOutRead = nullptr, hOutWrite = nullptr;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&hInRead, &hInWrite, &sa, 0) || !CreatePipe(&hOutRead, &hOutWrite, &sa, 0)) {
        if (hInRead) CloseHandle(hInRead);
        if (hInWrite) CloseHandle(hInWrite);
        if (hOutRead) CloseHandle(hOutRead);
        if (hOutWrite) CloseHandle(hOutWrite);
        emit errorOccurred(QStringLiteral("创建 ConPTY 管道失败"));
        return false;
    }

    m_hOutputRead = hOutRead;   // 父进程读
    m_hInputWrite = hInWrite;   // 父进程写

    // 2) 创建伪控制台
    COORD size{};
    // 列宽调大(默认 80 会让 "dsh web: http://.../?token=..." 这类长行被换行截断,
    // 导致上层 checkWebUrl 只抓到 URL 的 42 字符 token、少 1 个字符而 401)。
    // 加宽后长行不再被伪控制台换行; 显示端 QPlainTextEdit 仍按窗口宽度自行折行, 不受影响。
    size.X = 500;
    size.Y = 24;
    HRESULT hr = m_createPseudoConsole(size, hInRead, hOutWrite, 0, &m_hConsole);
    // 伪控制台已接管这两个端, 父进程关闭自己的副本
    CloseHandle(hInRead);
    CloseHandle(hOutWrite);
    if (FAILED(hr)) {
        if (m_hOutputRead) CloseHandle(m_hOutputRead);
        if (m_hInputWrite) CloseHandle(m_hInputWrite);
        m_hOutputRead = INVALID_HANDLE_VALUE;
        m_hInputWrite = INVALID_HANDLE_VALUE;
        emit errorOccurred(QStringLiteral("CreatePseudoConsole 失败"));
        return false;
    }

    // 3) 构造命令行并启动子进程
    QString cmdLine = program;
    for (const QString &a : args)
        cmdLine += QLatin1Char(' ') + quoteCommandLineArg(a);
    std::wstring cmdW = cmdLine.toStdWString();

    STARTUPINFOEX si{};
    si.StartupInfo.cb = sizeof(si);
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    BYTE *attrBuf = new BYTE[attrSize];
    LPPROC_THREAD_ATTRIBUTE_LIST attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf);
    InitializeProcThreadAttributeList(attrList, 1, 0, &attrSize);
    UpdateProcThreadAttribute(attrList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
                              m_hConsole, sizeof(m_hConsole), nullptr, nullptr);
    si.lpAttributeList = attrList;

    PROCESS_INFORMATION pi{};
    std::wstring workW = workingDir.isEmpty() ? std::wstring() : workingDir.toStdWString();
    LPCWSTR workPtr = workingDir.isEmpty() ? nullptr : workW.c_str();

    BOOL ok = CreateProcessW(nullptr, cmdW.data(), nullptr, nullptr, FALSE,
                             EXTENDED_STARTUPINFO_PRESENT, nullptr, workPtr,
                             &si.StartupInfo, &pi);

    DeleteProcThreadAttributeList(attrList);
    delete[] attrBuf;

    if (!ok) {
        if (m_closePseudoConsole && m_hConsole)
            m_closePseudoConsole(m_hConsole);
        if (m_hOutputRead) CloseHandle(m_hOutputRead);
        if (m_hInputWrite) CloseHandle(m_hInputWrite);
        m_hOutputRead = INVALID_HANDLE_VALUE;
        m_hInputWrite = INVALID_HANDLE_VALUE;
        m_hConsole = nullptr;
        emit errorOccurred(QStringLiteral("启动进程失败: %1").arg(GetLastError()));
        return false;
    }

    m_hProcess = pi.hProcess;
    m_pid = pi.dwProcessId;
    CloseHandle(pi.hThread); // 线程句柄不再需要

    m_running = true;

    // 4) 启动读线程(阻塞 ReadFile 输出)
    m_reader = std::thread(&CDSConPty::readerLoop, this);

    emit started();
    return true;
}

void CDSConPty::readerLoop()
{
    char buf[8192];
    while (!m_stop.load()) {
        DWORD read = 0;
        BOOL ok = ReadFile(m_hOutputRead, buf, sizeof(buf), &read, nullptr);
        if (!ok) {
            DWORD err = GetLastError();
            // 对端关闭 / 终止 => 子进程退出
            if (err == ERROR_BROKEN_PIPE || err == ERROR_OPERATION_ABORTED) {
                break;
            }
            break;
        }
        if (read == 0)
            break;
        emit outputReady(QByteArray(buf, static_cast<int>(read)));
    }

    m_running = false;
    emit processExited();
}

bool CDSConPty::write(const QByteArray &data)
{
    if (!m_running || data.isEmpty())
        return false;
    if (m_hInputWrite == INVALID_HANDLE_VALUE)
        return false;

    const char *p = data.constData();
    int remaining = data.size();
    while (remaining > 0) {
        DWORD written = 0;
        BOOL ok = WriteFile(m_hInputWrite, p, static_cast<DWORD>(remaining), &written, nullptr);
        if (!ok)
            return false;
        if (written == 0)
            return false;
        remaining -= static_cast<int>(written);
        p += written;
    }
    return true;
}

void CDSConPty::resize(int rows, int cols)
{
    if (!m_running || !m_resizePseudoConsole || !m_hConsole)
        return;
    if (rows < 1)
        rows = 1;
    if (cols < 1)
        cols = 1;
    COORD size;
    size.X = static_cast<SHORT>(cols);
    size.Y = static_cast<SHORT>(rows);
    m_resizePseudoConsole(m_hConsole, size);
}

void CDSConPty::close()
{
    if (!m_running && m_hProcess == nullptr && m_hConsole == nullptr)
        return;

    m_stop = true;

    // 终止子进程, 停止产生输出
    if (m_hProcess) {
        TerminateProcess(m_hProcess, 0);
        CloseHandle(m_hProcess);
        m_hProcess = nullptr;
        m_pid = 0;
    }

    // 销毁伪控制台: 关闭其输出管道写端
    if (m_closePseudoConsole && m_hConsole) {
        m_closePseudoConsole(m_hConsole);
        m_hConsole = nullptr;
    }

    // 先关闭读管道句柄, 使阻塞的 ReadFile 立刻返回(ERROR_OPERATION_ABORTED),
    // 之后再 join 读线程, 避免读线程卡死导致主线程 close() 永不返回(界面冻结)。
    // CancelIoEx 对同步管道读取不一定生效, 因此以关闭句柄为主。
    if (m_hOutputRead != INVALID_HANDLE_VALUE) {
        CancelIoEx(m_hOutputRead, nullptr);
        CloseHandle(m_hOutputRead);
        m_hOutputRead = INVALID_HANDLE_VALUE;
    }

    if (m_reader.joinable())
        m_reader.join();

    if (m_hInputWrite != INVALID_HANDLE_VALUE) {
        CloseHandle(m_hInputWrite);
        m_hInputWrite = INVALID_HANDLE_VALUE;
    }

    m_running = false;
}
