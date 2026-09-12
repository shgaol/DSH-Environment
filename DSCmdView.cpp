#include "DSCmdView.h"

#include "DSConPty.h"

#include <QClipboard>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextCursor>
#include <QVBoxLayout>

namespace {
// 完成哨兵: startScript 在命令序列末尾追加这条 echo, 输出里检测到它即认为脚本任务完成。
// 选一个极难与正常输出撞上的字符串。
const char *kTaskDoneMarker = "__DSH_TASK_DONE_7F3B__";

// 启动时注入的切代码页命令, 其回显行不显示(不该出现在输出里)
const char *kChcpEcho = "@chcp 65001";

// 去掉 ANSI/VT 转义序列, 只保留可打印文本。cmd/工具会输出 \x1b[2J、\x1b[?25h、\x1b[18X 等,
// QPlainTextEdit 不解析它们, 直接显示会变成乱码。这里按字节扫描, 跳过转义序列。
QByteArray stripAnsi(const QByteArray &data)
{
    QByteArray out;
    out.reserve(data.size());
    int i = 0;
    const int n = data.size();
    while (i < n) {
        const char c = data.at(i);
        if (c == '\x1b') { // ESC
            ++i;
            if (i >= n)
                break;
            const char nx = data.at(i);
            if (nx == '[') { // CSI: ESC [ 参数... 终结符(0x40..0x7E)
                ++i;
                while (i < n && !(data.at(i) >= 0x40 && data.at(i) <= 0x7E))
                    ++i;
                if (i < n)
                    ++i; // 跳过终结符
            } else if (nx == ']') { // OSC: ESC ] ... BEL 或 ESC \
                ++i;
                while (i < n) {
                    const char ch = data.at(i);
                    if (ch == '\x07') { ++i; break; }
                    if (ch == '\x1b' && i + 1 < n && data.at(i + 1) == '\\') { i += 2; break; }
                    ++i;
                }
            } else if (nx == '(' || nx == ')' || nx == '*' || nx == '+') {
                // 字符集选择: ESC ( B 等, 跳过两个字符
                i += 2;
                if (i > n)
                    i = n;
            } else {
                ++i; // 其它单字符 ESC 序列, 跳过 ESC 后的首字符(值域不大, 简化处理)
            }
        } else {
            out.append(c);
            ++i;
        }
    }
    return out;
}

// 压缩连续空行: 把 3 个及以上连续换行缩成 2 个(即最多保留一个空行), 避免 cmd 清屏
// (\x1b[K\r\n 变回 \r\n 后的连续空行)在输出里堆积成大片空白。
QString collapseBlankLines(const QString &text)
{
    QString out;
    out.reserve(text.size());
    int consecutive = 0;
    for (const QChar &ch : text) {
        if (ch == QLatin1Char('\n')) {
            ++consecutive;
            if (consecutive <= 2)
                out += ch;
        } else {
            consecutive = 0;
            out += ch;
        }
    }
    return out;
}
} // namespace

CDSCmdView::CDSCmdView(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("Cmd"));

    // 1) 输出显示框(只读, 黑底浅字); 顺序接收 cmd 输出
    m_output = new QPlainTextEdit(this);
    m_output->setReadOnly(true);
    m_output->setUndoRedoEnabled(false);
    m_output->setMaximumBlockCount(5000); // 限制回滚, 防内存膨胀
    QFont mf = font();
    mf.setFamily(QStringLiteral("Consolas"));
    mf.setStyleHint(QFont::Monospace);
    mf.setFixedPitch(true);
    mf.setPointSize(9);
    m_output->setFont(mf);
    m_output->setStyleSheet(QStringLiteral(
        "QPlainTextEdit { background-color:#000000; color:#d0d0d0;"
        "  border:1px solid #2C2F35; }"));

    // 2) 命令输入框(底部)
    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("输入命令后回车"));
    m_input->setClearButtonEnabled(true);
    m_input->setStyleSheet(QStringLiteral(
        "background-color:#000000; color:#ffffff;"
        "border:1px solid #2C2F35; padding:3px 6px;"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_output, 1);
    layout->addWidget(m_input);      // 命令栏(输入框)
    m_input->setFocus();

    // 3) ConPTY 进程(官方 cmd.exe, 保持会话使 cd/path 等持续生效)
    m_pty = new CDSConPty(this);

    // ---- 接线 ----
    connect(m_pty, &CDSConPty::outputReady, this, &CDSCmdView::onPtyOutput, Qt::QueuedConnection);
    // 回车由 eventFilter 统一拦截处理(既执行命令, 又不触发父 QDialog 关闭), 因此不再连 returnPressed。
    m_input->installEventFilter(this);
    connect(m_pty, &CDSConPty::processExited, this, &CDSCmdView::onPtyExited, Qt::QueuedConnection);
    connect(m_pty, &CDSConPty::errorOccurred, this, &CDSCmdView::onError);

    resize(1000, 720); // 默认窗口尺寸
}

CDSCmdView::~CDSCmdView()
{
    // 先停掉读线程, 再进行子对象清理(避免读线程在析构期间发信号)
    m_pty->close();
}

bool CDSCmdView::start(const QString &program, const QStringList &args, const QString &workingDir)
{
    if (m_pty->isRunning())
        return false;
    m_output->clear();
    if (!m_pty->start(program, args, workingDir)) {
        emit errorOccurred(QStringLiteral("启动 cmd 失败"));
        return false;
    }
    // 把控制台输出代码页切到 UTF-8(65001), 根治 pnpm/Node 等输出中的
    // '│' 制表符与中文因 GBK/UTF-8 编码不一致导致的乱码。
    // '@' 抑制 cmd 对该命令行的回显, '>nul' 抑制 "Active code page:" 输出。
    if (program.compare(QStringLiteral("cmd.exe"), Qt::CaseInsensitive) == 0)
        m_pty->write("@chcp 65001 >nul\r\n");
    emit sessionStarted();
    return true;
}

bool CDSCmdView::startScript(const QString &workingDir, const QStringList &lines)
{
    if (!start(QStringLiteral("cmd.exe"), {}, workingDir))
        return false;
    // 把命令序列整体写入伪终端(每条带回车); 末尾追加完成哨兵, 用于检测脚本任务结束
    QByteArray script;
    for (const QString &line : lines) {
        script += line.toUtf8();
        script += "\r\n";
    }
    script += "echo " + QByteArray(kTaskDoneMarker) + "\r\n";
    m_pty->write(script);
    startTask(); // 脚本任务开始
    return true;
}

bool CDSCmdView::isRunning() const
{
    return m_pty->isRunning();
}

void CDSCmdView::closeSession()
{
    m_pty->close();
}

void CDSCmdView::sendText(const QString &text)
{
    m_pty->write(text.toUtf8());
}

void CDSCmdView::pasteFromClipboard()
{
    QClipboard *cb = QGuiApplication::clipboard();
    if (!cb)
        return;
    const QString text = cb->text();
    if (text.isEmpty())
        return;
    m_input->setText(text);
    m_input->setFocus();
}

// 拦截输入框回车键: 执行命令同时消费该按键, 避免它冒泡到父 QDialog 触发默认按钮而关闭对话框。
bool CDSCmdView::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_input && event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            onInputReturned(); // 发送命令
            return true;       // 消费事件, 不让父对话框收到回车
        }
    }
    return QWidget::eventFilter(obj, event);
}

// 追加文本到显示框并滚动到底; 支持 '\r' 的“覆盖当前行”语义(回到行首重写),
// 这样 npm/pnpm 等用 '\r' 原地刷新的进度动画显示在单行, 而不是换行竖排刷屏。
void CDSCmdView::appendOutput(const QString &text)
{
    QTextCursor cur = m_output->textCursor();
    cur.movePosition(QTextCursor::End);
    int i = 0;
    while (i < text.size()) {
        const QChar ch = text.at(i);
        if (ch == QLatin1Char('\r')) {
            // 回车刷新: 回到当前行首, 删除本行已有内容, 之后的内容从行首重新写入
            cur.movePosition(QTextCursor::StartOfLine, QTextCursor::KeepAnchor);
            cur.removeSelectedText();
            cur.movePosition(QTextCursor::End);
        } else if (ch == QLatin1Char('\n')) {
            cur.insertText(QStringLiteral("\n"));
        } else {
            cur.insertText(QString(ch));
        }
        ++i;
    }
    m_output->setTextCursor(cur);
    m_output->ensureCursorVisible();
}

void CDSCmdView::onPtyOutput(const QByteArray &raw)
{
    if (raw.isEmpty())
        return;

    // 跨 chunk 的多字节 UTF-8 字符(如 '│' U+2502 的 E2 94 82)可能被拆开,
    // 若按块单独 fromUtf8 会把拆开的字节变成替换符 '�', 显示为乱码。
    // 这里把本块拼到待处理缓冲区, 只解码“完整”前缀, 尾部不完整的字节留给下一块。
    const QByteArray buf = m_utf8Pending + raw;
    int complete = 0; // 完整字节数(不含末尾可能不完整的多字节序列)
    const int n = buf.size();
    int i = 0;
    while (i < n) {
        const uchar c = static_cast<uchar>(buf.at(i));
        int need;
        if (c < 0x80)                 need = 1;  // ASCII
        else if ((c & 0xE0) == 0xC0)  need = 2;  // 110xxxxx
        else if ((c & 0xF0) == 0xE0)  need = 3;  // 1110xxxx
        else if ((c & 0xF8) == 0xF0)  need = 4;  // 11110xxx
        else                          need = 1;  // 非法 lead 字节, 原样透传
        if (i + need > n)
            break; // 末尾不完整, 留到下一块再解码
        i += need;
        complete = i;
    }
    m_utf8Pending = buf.mid(complete); // 暂存不完整尾部

    if (complete == 0)
        return;
    // 去掉 ANSI/VT 转义序列, 只保留可打印文本(控制序列 QPlainTextEdit 不解析, 需剔除)
    const QByteArray cleaned = stripAnsi(buf.left(complete));
    // 优先按 UTF-8 解码(配合 chcp 65001, 控制台已是 UTF-8); 若仍出现替换符再按 GBK 兜底
    QString text = QString::fromUtf8(cleaned);
    if (text.contains(QChar::ReplacementCharacter)) {
        const QString alt = QString::fromLocal8Bit(cleaned);
        if (alt.count(QChar::ReplacementCharacter) < text.count(QChar::ReplacementCharacter))
            text = alt;
    }
    // cmd 的 CRLF 归一为 '\n'; 单独的 '\r'(进度刷新)保留, 交给 appendOutput 做单行覆盖显示
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));

    const QString marker = QString::fromLatin1(kTaskDoneMarker);
    // 去掉注入的 chcp 命令回显行与完成哨兵行(都不该显示); 检测到哨兵即任务完成。
    bool taskDone = false;
    {
        const QStringList lines = text.split(QLatin1Char('\n'));
        QStringList kept;
        for (const QString &ln : lines) {
            if (ln.contains(marker)) {
                taskDone = true;
                continue; // 丢弃完成哨兵的回显/输出行
            }
            if (ln.contains(QLatin1String(kChcpEcho)))
                continue; // 丢弃注入的 chcp 命令回显行
            kept << ln;
        }
        text = kept.join(QLatin1Char('\n'));
    }

    // 识别服务启动后打印的 "dsh web: <带token网址>" 行, 供上层用程序内网页窗口打开
    checkWebUrl(text);

    // 压缩连续空行(最多保留一个空行), 抑制 cmd 清屏产生的空白; 再按顺序追加。
    text = collapseBlankLines(text);
    if (!text.isEmpty())
        appendOutput(text);

    if (taskDone)
        finishTask(); // 任务完成 → 输出区加“任务已经完成”
}

// 在输出文本中识别 "dsh web: <url>" 行, 取出带 token 的网址并发出 webUrlCaptured。
// 用于“开启服务”(dsh ... --no-open)后, 用程序内网页窗口打开对应网站。
void CDSCmdView::checkWebUrl(const QString &text)
{
    const QString marker = QStringLiteral("dsh web: ");
    int pos = text.indexOf(marker);
    if (pos < 0)
        return;
    const int start = pos + marker.size();
    // URL 到下一个空格(其后通常是 " (LAN: ...)")或换行处结束
    int stop = text.indexOf(QLatin1Char(' '), start);
    const int cr = text.indexOf(QLatin1Char('\r'), start);
    const int lf = text.indexOf(QLatin1Char('\n'), start);
    if (cr >= 0 && (stop < 0 || cr < stop)) stop = cr;
    if (lf >= 0 && (stop < 0 || lf < stop)) stop = lf;
    const QString url = (stop < start)
        ? text.mid(start).trimmed()
        : text.mid(start, stop - start).trimmed();
    if (!url.isEmpty() && url.startsWith(QLatin1String("http")))
        emit webUrlCaptured(url);
}

// 输入框回车 -> 发送命令(含回车); 并追加完成哨兵, 使任何命令跑完都能检测到“任务已经完成”
void CDSCmdView::onInputReturned()
{
    const QString cmd = m_input->text();
    m_input->clear();
    if (!m_pty->isRunning())
        return;
    if (cmd.isEmpty())
        return;
    const QByteArray input = cmd.toUtf8() + "\r\n";
    const QByteArray sentinel = QByteArray("echo ") + QByteArray(kTaskDoneMarker) + "\r\n";
    m_pty->write(input);
    // 追加一条无副作用的 echo 哨兵; cmd 执行完命令后会输出它, 我们据此判定命令结束。
    m_pty->write(sentinel);
    startTask(); // 任务开始
}

// 任务开始: 标记正在执行任务, 并重置“已提示”标志
void CDSCmdView::startTask()
{
    m_taskRunning = true;
    m_taskDoneShown = false;
}

// 任务结束: 输出区加一行“任务已经完成”(每次任务只追加一次)
void CDSCmdView::finishTask()
{
    if (m_taskDoneShown)
        return; // 已追加过, 避免重复
    m_taskDoneShown = true;
    m_taskRunning = false;
    appendOutput(QStringLiteral("\n任务已经完成\n"));
    emit taskFinished();
}

void CDSCmdView::onPtyExited()
{
    appendOutput(QStringLiteral("\n任务已经完成\n"));
    m_taskRunning = false;
    emit sessionFinished();
}

void CDSCmdView::onError(const QString &message)
{
    emit errorOccurred(message);
}
