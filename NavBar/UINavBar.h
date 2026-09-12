#ifndef UINAVBAR_H
#define UINAVBAR_H

#include <QWidget>

class CUINavBarItem;
class QIcon;

// 整体导航工作区：左侧导航栏（右侧内容区由 MainWindow 提供）
class CUINavBar : public QWidget
{
    Q_OBJECT

public:
    explicit CUINavBar(QWidget *parent = nullptr);
    ~CUINavBar() override;

    // 左侧导航栏
    CUINavBarItem *navBar() const;

    // 给第 1 部分增加一个按钮（图标随机，每次启动重新随机），
    // 返回与该按钮对应的唯一 ID（系统随机生成，不重复）
    QString AddTopBtn(const QString &title);
    // 根据 AddTopBtn 返回的 ID 获取按钮对应的标题（无效 ID 返回空字符串）
    QString GetTopBtnTitle(const QString &id) const;

    // ---- 第 3 部分按钮管理 ----
    // 给第 3 部分增加一个按钮（图标随机），返回唯一 ID
    QString AddBotBtn(const QString &title);
    // 根据 AddBotBtn 返回的 ID 获取按钮对应的标题（无效 ID 返回空字符串）
    QString GetBotBtnTitle(const QString &id) const;

    // ---- 第 2 部分树形菜单管理 ----
    // 增加一个根目录菜单（图标随机），返回唯一 ID
    QString AddTreeMainItem(const QString &title);
    // 在 id 为 mainid 的菜单下面增加一个子菜单（图标随机），返回唯一 ID
    QString AddTreeChildItem(const QString &title, const QString &mainid);
    // 根据菜单 ID 获取标题（无效 ID 返回空字符串）
    QString GetTreeItemTitle(const QString &id) const;
    // 展开/收起树形菜单：depth == -1 全部展开，== 0 全部收起，其他按 expandToDepth 规则
    void TreeItemExpandToDepth(int depth);

    // ---- 第 0 部分（任务属性：图标 + 三行文本）信息接口 ----
    void SetInfoIcon(const QIcon &icon);   // 设置第 0 部分的图标
    void SetInfoCode(const QString &code); // 设置第 1 行文本 InfoCode
    QString GetInfoCode() const;
    void SetInfoName(const QString &name); // 设置第 2 行文本 InfoName
    QString GetInfoName() const;
    void SetInfoText(const QString &text); // 设置第 3 行文本 InfoText
    QString GetInfoText() const;

signals:
    // 鼠标左键单击第 0 部分图标时发出
    void infoClicked();
    // 鼠标右键单击第 0 部分图标时发出
    void infoRightClicked();
    // 鼠标左键点击第 1 部分按钮时发出（参数为该按钮的 ID，仅第 1 部分按钮有效）
    void topbtnClicked(const QString &id);
    // 鼠标左键点击第 3 部分按钮时发出（参数为该按钮的 ID，仅第 3 部分按钮有效）
    void botbtnClicked(const QString &id);
    // 鼠标左键点击第 2 部分树形菜单时发出（id 为菜单 ID，isLeaf 是否为最末级）
    void treeItemClicked(const QString &id, bool isLeaf);

private:
    CUINavBarItem *m_navBar = nullptr; // 左侧导航栏
};

#endif // UINAVBAR_H
