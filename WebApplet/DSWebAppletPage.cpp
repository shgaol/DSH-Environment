#include "DSWebAppletPage.h"
#include "DSWebAppletDlg.h"
#include "DSWebAppletIconFetcher.h"
#include "../NavBar/UINavBarItem.h" // CUINavBarItem::makeLetterIcon（名称首字兜底图标）

#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSize>
#include <QUrl>
#include <QVBoxLayout>

namespace {
// 快捷方式图标/网格尺寸：图标固定 48×48（QIcon::pixmap 保证不会画得比请求尺寸更大），
// 格子高度按“图标约 52px + 名称约 48px（2~3 行）”给足，名称放不下时由列表自动省略
const QSize kAppletIconSize(48, 48);
const QSize kAppletGridSize(112, 100);
// 兜底图标底色（与导航栏字母图标同色）
const QColor kAppletIconColor(0x25, 0x63, 0xEB);
} // namespace

CDSWebAppletPage::CDSWebAppletPage(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("网页小程序"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);

    // ---- 按钮区：增加 / 修改 / 删除 ----
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(6);

    auto *addBtn = new QPushButton(QStringLiteral("增加"), this);
    auto *modifyBtn = new QPushButton(QStringLiteral("修改"), this);
    auto *delBtn = new QPushButton(QStringLiteral("删除"), this);
    bar->addWidget(addBtn);
    bar->addWidget(modifyBtn);
    bar->addWidget(delBtn);
    bar->addStretch(1);
    layout->addLayout(bar);

    connect(addBtn, &QPushButton::clicked, this, &CDSWebAppletPage::onAdd);
    connect(modifyBtn, &QPushButton::clicked, this, &CDSWebAppletPage::onModify);
    connect(delBtn, &QPushButton::clicked, this, &CDSWebAppletPage::onDelete);

    // ---- 快捷方式区：图标 + 名称（图标模式，自动换行；放不下由滚动条解决）----
    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(kAppletIconSize);
    m_list->setGridSize(kAppletGridSize);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setWordWrap(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    // 名称文字的颜色显式写死（普通=深色、选中=白字 + 蓝底），不依赖当前系统样式/调色板：
    // 某些样式会按“高亮前景色”绘制选中项的文字，一旦高亮底没铺满整格，就会出现
    // 「白字 + 白底」——看起来就是这个快捷方式没有名称。
    m_list->setStyleSheet(QStringLiteral(
        "QListWidget { background-color: #FFFFFF; }"
        "QListWidget::item { color: #1F2229; }"
        "QListWidget::item:selected { background-color: #2563EB; color: #FFFFFF; }"));
    layout->addWidget(m_list, 1);

    // 双击打开；在选中项上回车同样打开（打开时按名称去重，重复请求只会激活已有窗口）
    connect(m_list, &QListWidget::itemDoubleClicked, this, &CDSWebAppletPage::onActivated);
    connect(m_list, &QListWidget::itemActivated, this, &CDSWebAppletPage::onActivated);
    // 右键：修改 / 删除
    connect(m_list, &QWidget::customContextMenuRequested,
            this, &CDSWebAppletPage::onContextMenu);

    m_hintLabel = new QLabel(
        QStringLiteral("双击小程序打开网页；右键小程序可修改 / 删除。"), this);
    layout->addWidget(m_hintLabel);

    // 打开表页时读取已保存的数据
    reload();
}

CDSWebAppletPage::~CDSWebAppletPage() = default;

QString CDSWebAppletPage::siteHostSuffix(const QString &url)
{
    const QString host = QUrl(url).host().toLower();
    if (host.isEmpty()) {
        return QString(); // 网址异常：不设站内后缀（所有 http(s) 链接都按站外交给 Edge）
    }
    const QStringList parts = host.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    // localhost、127.0.0.1 这类单段/IPv4 主机：整个主机名就是站内
    bool allNumeric = !parts.isEmpty();
    for (const QString &part : parts) {
        bool ok = false;
        part.toInt(&ok);
        if (!ok) {
            allNumeric = false;
            break;
        }
    }
    if (parts.size() <= 2 || allNumeric) {
        return host;
    }
    // 顶级域名下的二级后缀（.com.cn 等）：站点后缀要取最后三段，
    // 否则会把整个 com.cn 当站内，导致其它站点也被当成站内链接。
    static const QStringList kSecondLevelSuffixes{
        QStringLiteral("com.cn"), QStringLiteral("net.cn"), QStringLiteral("org.cn"),
        QStringLiteral("gov.cn"), QStringLiteral("edu.cn"), QStringLiteral("com.hk"),
        QStringLiteral("com.tw"), QStringLiteral("com.au"), QStringLiteral("co.jp"),
        QStringLiteral("co.uk"), QStringLiteral("co.kr"),
    };
    const QString lastTwo = parts.at(parts.size() - 2) + QLatin1Char('.')
                            + parts.at(parts.size() - 1);
    if (kSecondLevelSuffixes.contains(lastTwo)) {
        return parts.at(parts.size() - 3) + QLatin1Char('.') + lastTwo;
    }
    return lastTwo; // 如 chat.deepseek.com → deepseek.com、www.toutiao.com → toutiao.com
}

int CDSWebAppletPage::currentRow() const
{
    return m_list->currentRow();
}

void CDSWebAppletPage::reload()
{
    m_applets = CDSWebAppletStore::load();
    m_list->clear();
    for (const DSWebApplet &applet : m_applets) {
        m_list->addItem(makeItem(applet));
    }
}

void CDSWebAppletPage::saveAll() const
{
    CDSWebAppletStore::save(m_applets);
}

QListWidgetItem *CDSWebAppletPage::makeItem(const DSWebApplet &applet) const
{
    auto *item = new QListWidgetItem(appletIcon(applet), applet.name);
    // 关键：给条目一个明确的尺寸（= 格子尺寸）。
    // QListView 图标模式下条目矩形 = min(条目的 sizeHint, gridSize)，只会向下裁剪、不会向上撑大
    // （qlistview.cpp: item->w = qMin(grid.width(), item->w)）。不显式给尺寸时，sizeHint 只按
    // “图标高度 + 一行名称”算，比格子矮；而首字兜底图标是 48×48 满尺寸的实心图标，
    // 它会把条目矩形几乎占满，留给名称的高度接近 0 —— 表现就是「只有图标，看不到名称」。
    // 这里把尺寸钉成格子大小，图标（约 52px）之外还剩约 52px 给名称。
    item->setSizeHint(kAppletGridSize);
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignTop);
    item->setToolTip(QStringLiteral("%1\n%2").arg(applet.name, applet.url));
    // 名称/网址也存到条目上，便于右键等场景核对（列表行与 m_applets 一一对应）
    item->setData(Qt::UserRole, applet.name);
    item->setData(Qt::UserRole + 1, applet.url);
    return item;
}

QIcon CDSWebAppletPage::appletIcon(const DSWebApplet &applet) const
{
    // 已取到该网页的图标 → 直接用它做快捷方式图标
    const QString path = CDSWebAppletStore::iconPath(applet.iconFile);
    if (!path.isEmpty() && QFile::exists(path)) {
        const QIcon icon(path);
        if (!icon.isNull()) {
            return icon;
        }
    }
    // 还没取到（或图标文件丢失）→ 用名称首字图标兜底，保证快捷方式一定有图标；
    // 尺寸与网页图标一致（48×48），避免兜底图标比网页图标“胖一圈”而挤掉下面的名称
    QString first = applet.name.left(1);
    if (first.isEmpty()) {
        first = QStringLiteral("W"); // 名称异常为空时的兜底字
    }
    return CUINavBarItem::makeLetterIcon(first.at(0), kAppletIconColor,
                                         kAppletIconSize.width());
}

void CDSWebAppletPage::refreshItem(int row)
{
    if (row < 0 || row >= m_applets.size()) {
        return;
    }
    QListWidgetItem *item = m_list->item(row);
    if (!item) {
        return;
    }
    const DSWebApplet &applet = m_applets.at(row);
    item->setText(applet.name);
    item->setIcon(appletIcon(applet));
    item->setToolTip(QStringLiteral("%1\n%2").arg(applet.name, applet.url));
    item->setData(Qt::UserRole, applet.name);
    item->setData(Qt::UserRole + 1, applet.url);
}

QStringList CDSWebAppletPage::nameListExcept(int excludeRow) const
{
    QStringList names;
    for (int row = 0; row < m_applets.size(); ++row) {
        if (row == excludeRow) {
            continue; // 修改时排除自己，否则原名称会被判为重复
        }
        names << m_applets.at(row).name;
    }
    return names;
}

void CDSWebAppletPage::fetchIcon(const DSWebApplet &applet)
{
    // 父对象挂在 qApp 下：表页被关闭后图标仍会读取完成、落盘并写回 JSON；
    // 读取器结束后自行 deleteLater，不会残留。
    auto *fetcher = new CDSWebAppletIconFetcher(
        applet.name, applet.url, applet.iconFile, QCoreApplication::instance());
    connect(fetcher, &CDSWebAppletIconFetcher::iconReady,
            this, &CDSWebAppletPage::onIconReady);
    fetcher->start();
}

void CDSWebAppletPage::onAdd()
{
    CDSWebAppletDlg dlg(nameListExcept(-1), false, this);
    if (dlg.exec() != QDialog::Accepted) {
        return; // 取消或未通过校验（名称/网址为空、名称重复、网址格式不对）
    }

    DSWebApplet applet;
    applet.name = dlg.name();
    applet.url = dlg.url();
    // 先占好图标文件名：图标读回来直接落盘，界面按名称+网址匹配刷新
    applet.iconFile = CDSWebAppletStore::makeIconFileName();

    m_applets.append(applet);
    saveAll(); // 先保存名称与网址
    m_list->addItem(makeItem(applet));
    m_list->setCurrentRow(m_list->count() - 1);

    // 保存之后再读取该网页的图标（异步；取到后刷新为图标 + 名称）
    fetchIcon(applet);
}

void CDSWebAppletPage::onModify()
{
    const int row = currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("未选中数据！"));
        return;
    }

    const DSWebApplet old = m_applets.at(row);
    CDSWebAppletDlg dlg(nameListExcept(row), true, this);
    dlg.setName(old.name);
    dlg.setUrl(old.url);
    if (dlg.exec() != QDialog::Accepted) {
        return; // 取消或未通过校验（规则同“增加”）
    }

    DSWebApplet applet = old;
    applet.name = dlg.name();
    applet.url = dlg.url();
    const bool urlChanged = (applet.url != old.url);
    if (urlChanged) {
        // 网址变了 → 图标要重新读取：删掉旧图标，图标文件名另起一个
        CDSWebAppletStore::removeIconFile(old.iconFile);
        applet.iconFile.clear();
    }
    // 需要读取图标的情况：网址变了、还没有图标、或图标文件丢了（此时“修改+确定”即可重取）
    const bool needIcon = urlChanged || applet.iconFile.isEmpty()
        || !QFile::exists(CDSWebAppletStore::iconPath(applet.iconFile));
    if (needIcon && applet.iconFile.isEmpty()) {
        applet.iconFile = CDSWebAppletStore::makeIconFileName();
    }

    m_applets[row] = applet;
    saveAll();
    refreshItem(row);

    if (needIcon) {
        fetchIcon(applet);
    }
}

void CDSWebAppletPage::onDelete()
{
    const int row = currentRow();
    if (row < 0) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("未选中数据！"));
        return;
    }

    const DSWebApplet applet = m_applets.at(row);
    const QMessageBox::StandardButton ret = QMessageBox::question(
        this, QStringLiteral("确认删除"),
        QStringLiteral("确定删除【%1】这个小程序吗？").arg(applet.name),
        QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        return; // 点“否”：不删除
    }

    m_applets.removeAt(row);
    delete m_list->takeItem(row);
    CDSWebAppletStore::removeIconFile(applet.iconFile); // 连图标文件一起删掉
    saveAll();
}

void CDSWebAppletPage::onActivated(QListWidgetItem *item)
{
    if (!item) {
        return;
    }
    const int row = m_list->row(item);
    if (row < 0 || row >= m_applets.size()) {
        return;
    }
    const DSWebApplet &applet = m_applets.at(row);
    emit openRequested(applet.name, applet.url);
}

void CDSWebAppletPage::onContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_list->itemAt(pos);
    if (!item) {
        return; // 空白处右键：不弹菜单
    }
    // 右键先选中被点中的小程序，修改/删除都作用于它
    m_list->setCurrentItem(item);

    QMenu menu(this);
    QAction *modifyAct = menu.addAction(QStringLiteral("修改"));
    QAction *delAct = menu.addAction(QStringLiteral("删除"));
    QAction *chosen = menu.exec(m_list->viewport()->mapToGlobal(pos));
    if (chosen == modifyAct) {
        onModify();
    } else if (chosen == delAct) {
        onDelete();
    }
}

void CDSWebAppletPage::onIconReady(const QString &name, const QString &url, const QString &iconFile)
{
    // PNG 与 JSON 都已由读取器写好，这里只刷新界面；
    // 同时把内存里的条目也更新掉，避免后续保存 JSON 时把图标字段覆盖成空。
    for (int row = 0; row < m_applets.size(); ++row) {
        if (m_applets.at(row).name == name && m_applets.at(row).url == url) {
            m_applets[row].iconFile = iconFile;
            refreshItem(row);
            return;
        }
    }
}
