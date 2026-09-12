#ifndef DSWEBAPPLETPAGE_H
#define DSWEBAPPLETPAGE_H

#include <QIcon>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "DSWebAppletStore.h" // DSWebApplet（小程序条目）

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPoint;

// “网页小程序”表页（MDI 子窗口的内容部件）：
//   工具栏：增加 / 修改 / 删除
//   快捷方式区：图标 + 名称（图标模式，自动换行，放不下出滚动条）
//   交互：双击打开对应网页；右键某个小程序弹出菜单（修改 / 删除）
//   数据：保存后异步读取该网页的图标，取到后刷新显示（见 CDSWebAppletIconFetcher）
//   持久化：文档/DSH-Environment/configure/webapplets/webapplets.json（见 CDSWebAppletStore）
class CDSWebAppletPage : public QWidget
{
    Q_OBJECT

public:
    explicit CDSWebAppletPage(QWidget *parent = nullptr);
    ~CDSWebAppletPage() override;

    // 由小程序网址推出“站内域名后缀”：https://chat.deepseek.com/ → deepseek.com、
    // https://www.163.com/ → 163.com、https://www.abc.com.cn/ → abc.com.cn。
    // 用途与 kSiteInfos 里 DeepSeek 传 "deepseek.com"、今日头条传 "toutiao.com" 相同：
    // 打开小程序窗口时站内链接在窗口内导航、站外链接交给 Edge（见 MainWindow::openWebAppletWindow）。
    static QString siteHostSuffix(const QString &url);

signals:
    // 双击（或在选中项上回车）：请求在 MDI 中打开该网页小程序
    void openRequested(const QString &name, const QString &url);

private slots:
    // “增加”：弹录入对话框 → 保存 JSON → 读取网页图标
    void onAdd();
    // “修改”（工具栏按钮 / 右键菜单）：弹录入对话框（名称不重复、名称与网址必填）
    void onModify();
    // “删除”（工具栏按钮 / 右键菜单）：确认后删除选中的小程序
    void onDelete();
    // 双击/回车打开选中的小程序
    void onActivated(QListWidgetItem *item);
    // 右键菜单：修改 / 删除
    void onContextMenu(const QPoint &pos);
    // 网页图标读取完成：刷新对应条目的图标（JSON 已由读取器写回）
    void onIconReady(const QString &name, const QString &url, const QString &iconFile);

private:
    // 从 JSON 重新载入并重建列表
    void reload();
    // 保存全部小程序到 JSON
    void saveAll() const;
    // 当前选中行（未选中返回 -1）
    int currentRow() const;
    // 生成列表项（图标 + 名称 + 提示）
    QListWidgetItem *makeItem(const DSWebApplet &applet) const;
    // 条目图标：有网页图标用它，否则用名称首字图标兜底
    QIcon appletIcon(const DSWebApplet &applet) const;
    // 刷新某一行的图标/名称/提示
    void refreshItem(int row);
    // 已占用名称（excludeRow 为要排除的行，修改时排除自己；-1 表示不排除）
    QStringList nameListExcept(int excludeRow) const;
    // 启动图标读取（读取器挂在 qApp 下，表页关闭后仍会完成并写回 JSON）
    void fetchIcon(const DSWebApplet &applet);

    QListWidget *m_list = nullptr;   // 快捷方式列表（图标模式）
    QLabel *m_hintLabel = nullptr;   // 底部提示行
    QList<DSWebApplet> m_applets;    // 当前全部小程序（与列表行一一对应）
};

#endif // DSWEBAPPLETPAGE_H
