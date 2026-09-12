#ifndef DSCMDVIEW_H
#define DSCMDVIEW_H

// ============================================================================
// CDSCmdView —— 类似 CMD 的终端视图控件(可嵌入任意布局)
//
// 实现:
//   QPlainTextEdit  -> 输出显示框(顺序接收 cmd 输出)
//   QLineEdit       -> 输入命令(回车发送)
//   CDSConPty       -> Windows ConPTY 伪控制台进程(cmd.exe)
//
// 数据流:
//   PTY 输出 -> outputReady -> 增量 UTF-8 解码 -> 去 ANSI -> 顺序追加到 QPlainTextEdit
//   QLineEdit 回车 -> 写入字节(含 '\r') -> PTY 写入(回显与执行由 cmd 完成)
//
// 因为始终用同一个 ConPTY cmd 会话, 像 cd / path 这样的命令会持续生效。
// 遵循规则: 无智能指针; 无 QTimer; 不用 QML。
// 继承 QWidget 而非 QMainWindow, 方便作为子部件嵌入对话框/标签页等布局。
// ============================================================================

#include <QByteArray>
#include <QStringList>
#include <QWidget>

class QLineEdit;
class QPlainTextEdit;
class CDSConPty;

class CDSCmdView : public QWidget
{
    Q_OBJECT

public:
    explicit CDSCmdView(QWidget *parent = nullptr);
    ~CDSCmdView() override;

    // 启动一个交互式控制台进程(默认 cmd.exe)。成功返回 true。
    bool start(const QString &program = QStringLiteral("cmd.exe"),
               const QStringList &args = QStringList(),
               const QString &workingDir = QString());

    // 以指定工作目录启动 cmd 终端, 并按序执行 lines 中的命令(每条一行, 不含回车)。
    // 用于“以web方式打开服务”等把一条命令序列投放到终端窗口执行的场景。
    bool startScript(const QString &workingDir, const QStringList &lines);

    bool isRunning() const;

    // 关闭会话并结束进程
    void closeSession();

    // 直接向终端发送文本(回车让 cmd 执行, 需自行包含 '\r')
    void sendText(const QString &text);

    // 粘贴剪贴板内容到终端
    void pasteFromClipboard();

protected:
    // 拦截输入框的回车键, 消费该事件, 避免触发父 QDialog 的默认按钮/接受动作而关闭对话框
    bool eventFilter(QObject *obj, QEvent *event) override;

signals:
    void sessionStarted();
    void sessionFinished();
    void errorOccurred(const QString &message);
    void titleChanged(const QString &title);
    // 脚本任务完成(在输出里检测到完成哨兵后发出)
    void taskFinished();
    // 在输出里识别到 "dsh web: <带token网址>" 行时发出(用于“开启服务”后用程序内网页窗口打开)
    void webUrlCaptured(const QString &url);

private slots:
    void onPtyOutput(const QByteArray &data); // PTY 输出 → 显示框
    void onInputReturned();                  // 输入框回车 → 发送命令
    void onPtyExited();
    void onError(const QString &message);

private:
    void appendOutput(const QString &text);  // 追加文本到显示框并滚到底
    void checkWebUrl(const QString &text);   // 扫描输出, 识别 "dsh web:" 网址行并发出 webUrlCaptured
    void startTask();                        // 任务开始(任务栏显示“任务执行中”)
    void finishTask();                       // 任务结束(输出区加“任务已经结束”)

    QPlainTextEdit *m_output = nullptr;   // 输出显示框(只读, 顺序接收 cmd 输出)
    QLineEdit *m_input = nullptr;       // 命令输入框(命令栏)
    QByteArray m_utf8Pending;           // 暂存跨 chunk 被拆开的不完整 UTF-8 字节
    bool m_taskRunning = false;         // 是否正在执行任务(用于结束检测)
    bool m_taskDoneShown = false;       // 本次任务是否已追加过“任务已经完成”(去重)
    CDSConPty *m_pty = nullptr;         // ConPTY 伪控制台(cmd.exe)
};

#endif // DSCMDVIEW_H
