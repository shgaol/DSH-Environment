#ifndef UINAVBARITEM_H
#define UINAVBARITEM_H

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QList>
#include <QStringList>
#include <QWidget>

class QButtonGroup;
class QEvent;
class QLabel;
class QScrollArea;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;

// 左侧导航栏：类似 DeepSeek Harness 工作区的侧边导航栏
// 分四部分：
//   0. 任务属性区：图标在上 + 多条文字说明在下
//   1. 顶部图标按钮列（可滚动）：对话 / 任务 / 代理 / 作业 / 技能 / 工作流
//   2. 树形菜单（单棵树，栏目作为顶层分组节点，任意节点都能加下级）
//   3. 底部图标按钮列（可滚动）：设置 / 菜单
// 按钮过多时，第 1、3 部分各自提供竖向滚动条
// 可展开/收起：收起时窄条只显示图标按钮，展开时显示图标 + 文字并显示树形菜单
class CUINavBarItem : public QWidget
{
    Q_OBJECT

public:
    // 内置图标类型（程序内绘制，无需外部图片资源）
    enum class Icon {
        Chat,         // 对话
        Tasks,        // 任务
        Agents,       // 代理
        Jobs,         // 作业
        Skills,       // 技能
        Workflow,     // 工作流
        Grid,         // 分组/栏目（工作区、系统）
        Settings,     // 设置
        Home,         // 首页
        Search,       // 搜索
        Folder,       // 文件夹
        File,         // 文件
        Heart,        // 收藏
        Bell,         // 通知
        Clock,        // 时间
        Mail,         // 邮件
        MapPin,       // 位置
        Send,         // 发送
        Zap,          // 闪电
        Lock,         // 锁定
        Plus,         // 加号
        Minus,        // 减号
        Check,        // 对勾
        Close,        // 关闭
        Info,         // 信息
        Question,     // 疑问
        Edit,         // 编辑
        Trash,        // 删除
        Refresh,      // 刷新
        Download,     // 下载
        Upload,       // 上传
        Copy,         // 复制
        Calendar,     // 日历
        Flag,         // 旗帜
        Camera,       // 相机
        Power,        // 电源
        Sun,          // 太阳
        Moon,         // 月亮
        Cloud,        // 云
        Alert,        // 警告
        Shield,       // 盾牌
        Eye,          // 眼睛
        Key,          // 钥匙
        Tag,          // 标签
        Gift,         // 礼物
        Music,        // 音乐
        Pause,        // 暂停
        Stop,         // 停止
        Battery,      // 电池
        Chart,        // 图表
        Card,         // 银行卡
        Filter,       // 筛选
        ChevronLeft,  // 收起（展开态时显示）
        ChevronRight, // 展开（收起态时显示）
        // A-Z 字母图标（蓝色系，加入随机图标库）
        IconA,        // 字母 A
        IconB,        // 字母 B
        IconC,        // 字母 C
        IconD,        // 字母 D
        IconE,        // 字母 E
        IconF,        // 字母 F
        IconG,        // 字母 G
        IconH,        // 字母 H
        IconI,        // 字母 I
        IconJ,        // 字母 J
        IconK,        // 字母 K
        IconL,        // 字母 L
        IconM,        // 字母 M
        IconN,        // 字母 N
        IconO,        // 字母 O
        IconP,        // 字母 P
        IconQ,        // 字母 Q
        IconR,        // 字母 R
        IconS,        // 字母 S
        IconT,        // 字母 T
        IconU,        // 字母 U
        IconV,        // 字母 V
        IconW,        // 字母 W
        IconX,        // 字母 X
        IconY,        // 字母 Y
        IconZ,        // 字母 Z
    };

    // 生成字母图标：指定颜色的圆角方块背景 + 白色加粗字母（程序内绘制；默认 64px）
    static QIcon makeLetterIcon(const QChar &letter, const QColor &bg, int size = 64);

    explicit CUINavBarItem(QWidget *parent = nullptr);
    ~CUINavBarItem() override;

    int currentIndex() const;     // 当前选中图标按钮序号，-1 表示无
    QString currentTitle() const; // 当前选中图标按钮标题
    int itemCount() const;        // 图标按钮数量（含“设置”）

    // 展开/收起侧边栏
    void setExpanded(bool expanded);
    bool isExpanded() const;

    // ---- 任务属性（第 0 部分：图标 + 三行文本）----
    // 设置第 0 部分的图标
    void SetInfoIcon(const QIcon &icon);
    // 第 1 行文本：InfoCode
    void SetInfoCode(const QString &code);
    QString GetInfoCode() const;
    // 第 2 行文本：InfoName
    void SetInfoName(const QString &name);
    QString GetInfoName() const;
    // 第 3 行文本：InfoText
    void SetInfoText(const QString &text);
    QString GetInfoText() const;

    // 树形菜单
    QTreeWidget *tree() const;
    // 在树中添加节点：
    //   - parent 为 nullptr 时加到树的顶层（作为分组节点）
    //   - 传入某个节点作为 parent，即可为该节点添加下级（支持任意层级）
    QTreeWidgetItem *addTreeNode(const QString &title, QTreeWidgetItem *parent = nullptr);
    QTreeWidgetItem *addTreeNode(const QString &title, Icon icon, QTreeWidgetItem *parent = nullptr);
    // 清空树
    void clearTree();

    // ---- 第 1 部分按钮管理 ----
    // 给第 1 部分增加一个按钮：
    //   - 图标从内置图标中随机选取（每次启动重新随机）
    //   - 返回与该按钮对应的唯一 ID（系统随机生成，保证不重复）
    QString AddTopBtn(const QString &title);
    // 根据 AddTopBtn 返回的 ID 获取按钮对应的标题（无效 ID 返回空字符串）
    QString GetTopBtnTitle(const QString &id) const;

    // ---- 第 3 部分按钮管理 ----
    // 给第 3 部分增加一个按钮（图标随机，每次启动重新随机），返回唯一 ID
    QString AddBotBtn(const QString &title);
    // 根据 AddBotBtn 返回的 ID 获取按钮对应的标题（无效 ID 返回空字符串）
    QString GetBotBtnTitle(const QString &id) const;

    // ---- 第 2 部分树形菜单管理 ----
    // 增加一个根目录菜单（图标随机，每次启动重新随机），返回唯一 ID
    QString AddTreeMainItem(const QString &title);
    // 在 id 为 mainid 的菜单下面增加一个子菜单（图标随机），返回唯一 ID；
    // mainid 无效时返回空字符串
    QString AddTreeChildItem(const QString &title, const QString &mainid);
    // 根据菜单 ID 获取标题（无效 ID 返回空字符串）
    QString GetTreeItemTitle(const QString &id) const;
    // 展开/收起树形菜单：
    //   depth == -1：全部展开；depth == 0：全部收起；
    //   其他值：按 QTreeView::expandToDepth(depth) 的规则展开
    void TreeItemExpandToDepth(int depth);

signals:
    // 鼠标左键单击第 0 部分图标时发出
    void infoClicked();
    // 鼠标右键单击第 0 部分图标时发出
    void infoRightClicked();
    // 鼠标左键点击第 1 部分按钮时发出（参数为该按钮的 ID，仅第 1 部分按钮有效）
    void topbtnClicked(const QString &id);
    // 鼠标左键点击第 3 部分按钮时发出（参数为该按钮的 ID，仅第 3 部分按钮有效）
    void botbtnClicked(const QString &id);
    // 点击顶部/底部图标按钮时发出（index 从 0 开始）
    void itemClicked(int index, const QString &title);
    // 鼠标左键点击第 2 部分树形菜单时发出（id 为菜单 ID，isLeaf 是否为最末级）
    void treeItemClicked(const QString &id, bool isLeaf);
    // 展开/收起状态变化时发出
    void expandedChanged(bool expanded);

private:
    // 创建一个可滚动的按钮列容器（无边框、透明背景），返回其内部布局
    QScrollArea *createButtonColumn(QVBoxLayout **innerLayout);
    QToolButton *createItemButton(const QString &title, Icon icon);
    QTreeWidget *createTree(); // 创建一棵已配置好的树（不含数据）
    void initUi();
    void initStyle();
    void updateInfoIconSize(); // 按当前展开状态设置第 0 部分图标尺寸
    void applyExpandedState(); // 根据 m_expanded 更新按钮、树与栏宽
    void updateTreeIconVisibility(); // 更新第 2 部分入口图标：仅收起且树有内容时显示
    void updateSectionVisibility();  // 第 1/2/3 部分：无内容时隐藏，有数据时才显示
    void updateButtonState(QToolButton *btn, const QString &title, Icon icon); // 按当前状态刷新单个按钮
    bool eventFilter(QObject *obj, QEvent *event) override; // 捕获第 0 部分图标单击

    QButtonGroup *m_group = nullptr;         // 图标按钮互斥选中组
    QList<QToolButton *> m_buttons;          // 与 m_titles/m_buttonIcons 一一对应
    QStringList m_titles;                    // 各图标按钮标题
    QList<Icon> m_buttonIcons;               // 各图标按钮的图标类型（收起时按蓝色绘制）
    QLabel *m_attrIcon = nullptr;      // 第 0 部分：图标（上）
    QIcon m_infoIcon;                  // 第 0 部分：当前设置的图标（保存以便切换尺寸）
    QLabel *m_infoCodeLabel = nullptr; // 第 0 部分：第 1 行文本 InfoCode
    QLabel *m_infoNameLabel = nullptr; // 第 0 部分：第 2 行文本 InfoName
    QLabel *m_infoTextLabel = nullptr; // 第 0 部分：第 3 行文本 InfoText
    QToolButton *m_toggleButton = nullptr;   // 底部展开/收起按钮
    QToolButton *m_treeIconButton = nullptr; // 收起时第 2 部分入口图标（点击展开显示树）
    QVBoxLayout *m_layout = nullptr;         // 主布局
    QScrollArea *m_topColumn = nullptr;      // 第 1 部分按钮列容器（无按钮时隐藏）
    QVBoxLayout *m_topColumnLayout = nullptr;   // 第 1 部分按钮列（可滚动）
    QScrollArea *m_bottomColumn = nullptr;   // 第 3 部分按钮列容器（无按钮时隐藏）
    QVBoxLayout *m_bottomColumnLayout = nullptr; // 第 3 部分按钮列（可滚动）
    int m_spacer1Index = -1;                 // 弹性占位 1 在布局中的序号
    int m_spacer2Index = -1;                 // 弹性占位 2 在布局中的序号
    QTreeWidget *m_tree = nullptr;           // 树形菜单
    QHash<QString, QToolButton *> m_topButtonIds; // AddTopBtn 返回的 ID -> 按钮
    QHash<QString, QToolButton *> m_botButtonIds; // AddBotBtn 返回的 ID -> 按钮
    QHash<QString, QTreeWidgetItem *> m_treeItemIds;      // 树节点 ID -> item
    QHash<QTreeWidgetItem *, QString> m_treeItemIdByItem; // item -> ID（点击时反查）
    bool m_expanded = false;                 // 当前是否展开
};

#endif // UINAVBARITEM_H
