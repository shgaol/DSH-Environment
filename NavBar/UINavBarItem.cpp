#include "UINavBarItem.h"

#include <QAbstractItemView>
#include <QButtonGroup>
#include <QFont>
#include <QFrame>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QScrollBar>
#include <QSize>
#include <QSizePolicy>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUuid>
#include <QVBoxLayout>
#include <QVector>
#include <QtMath>

namespace {

constexpr qreal kPi = 3.14159265358979323846;

// 字母图标对应的字符：IconA~IconZ 必须按字母顺序连续排列（中间不可插入其他枚举项）
QChar letterChar(CUINavBarItem::Icon kind)
{
    const int base = static_cast<int>(CUINavBarItem::Icon::IconA);
    const int idx = static_cast<int>(kind) - base;
    if (idx >= 0 && idx < 26) {
        return QChar::fromLatin1(static_cast<char>('A' + idx));
    }
    return QLatin1Char('?');
}

// 在指定尺寸画布上绘制线性图标（坐标按 24x24 设计，自动等比放大，放大后依然清晰）
QPixmap drawIcon(CUINavBarItem::Icon kind, const QColor &color, int size = 24)
{
    const qreal c = 12.0;

    QPixmap pm(size, size);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    // 把 24x24 的设计坐标等比缩放到目标尺寸后直接绘制，
    // 避免“先画小图再缩放位图”产生的模糊
    p.scale(size / 24.0, size / 24.0);
    QPen pen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    switch (kind) {
    case CUINavBarItem::Icon::Chat: {
        QPainterPath path;
        path.addRoundedRect(QRectF(4, 4.5, 16, 11.5), 3.5, 3.5);
        path.moveTo(7.5, 15.5); // 气泡小尾巴
        path.lineTo(5.5, 19.5);
        path.lineTo(10, 15.5);
        p.drawPath(path);
        p.drawLine(QPointF(8.5, 10.5), QPointF(15.5, 10.5));
        break;
    }

    case CUINavBarItem::Icon::Tasks: {
        const qreal ys[3] = {7, 12, 17};
        for (qreal y : ys) {
            QPainterPath check; // 左侧小对勾
            check.moveTo(4.5, y);
            check.lineTo(6, y + 1.5);
            check.lineTo(8, y - 1.5);
            p.drawPath(check);
            p.drawLine(QPointF(10, y), QPointF(19, y)); // 右侧横线
        }
        break;
    }

    case CUINavBarItem::Icon::Agents:
        p.drawEllipse(QPointF(c, 8.5), 3.4, 3.4); // 头
        {
            QPainterPath shoulders; // 肩
            shoulders.moveTo(5, 19);
            shoulders.quadTo(c, 14, 19, 19);
            p.drawPath(shoulders);
        }
        break;

    case CUINavBarItem::Icon::Jobs: {
        QPainterPath tri; // 播放三角形
        tri.moveTo(9.5, 7);
        tri.lineTo(9.5, 17);
        tri.lineTo(16.5, 12);
        tri.closeSubpath();
        p.drawPath(tri);
        break;
    }

    case CUINavBarItem::Icon::Skills: {
        QPainterPath star; // 五角星
        const qreal outer = 7.0, inner = 3.0;
        for (int i = 0; i < 10; ++i) {
            const qreal r = (i % 2 == 0) ? outer : inner;
            const qreal ang = -kPi / 2 + i * kPi / 5;
            const QPointF pt(c + r * qCos(ang), c + r * qSin(ang));
            if (i == 0) {
                star.moveTo(pt);
            } else {
                star.lineTo(pt);
            }
        }
        star.closeSubpath();
        p.drawPath(star);
        break;
    }

    case CUINavBarItem::Icon::Workflow: {
        p.drawLine(QPointF(12, 8.5), QPointF(7, 15.5)); // 上节点到左下
        p.drawLine(QPointF(12, 8.5), QPointF(17, 15.5)); // 上节点到右下
        p.drawEllipse(QPointF(12, 6), 2.4, 2.4);
        p.drawEllipse(QPointF(6, 18), 2.4, 2.4);
        p.drawEllipse(QPointF(18, 18), 2.4, 2.4);
        break;
    }

    case CUINavBarItem::Icon::Grid: {
        const qreal x0 = 5.0, x1 = 13.5, w = 5.5; // 2x2 方格
        p.drawRoundedRect(QRectF(x0, 5.0, w, w), 1.2, 1.2);
        p.drawRoundedRect(QRectF(x1, 5.0, w, w), 1.2, 1.2);
        p.drawRoundedRect(QRectF(x0, 13.5, w, w), 1.2, 1.2);
        p.drawRoundedRect(QRectF(x1, 13.5, w, w), 1.2, 1.2);
        break;
    }

    case CUINavBarItem::Icon::Settings: {
        const qreal ys[3] = {8, 12, 16};         // 三条调节杆
        const qreal knobs[3] = {14.5, 9.5, 15.5}; // 滑块
        for (int i = 0; i < 3; ++i) {
            p.drawLine(QPointF(5, ys[i]), QPointF(19, ys[i]));
            p.drawEllipse(QPointF(knobs[i], ys[i]), 2.0, 2.0);
        }
        break;
    }

    case CUINavBarItem::Icon::Home: {
        QPainterPath roof; // 屋顶
        roof.moveTo(5, 11);
        roof.lineTo(12, 4.5);
        roof.lineTo(19, 11);
        p.drawPath(roof);
        p.drawLine(QPointF(7, 11), QPointF(7, 19));   // 左墙
        p.drawLine(QPointF(7, 19), QPointF(17, 19));  // 地面
        p.drawLine(QPointF(17, 19), QPointF(17, 11)); // 右墙
        p.drawLine(QPointF(10.5, 19), QPointF(10.5, 15)); // 门
        p.drawLine(QPointF(10.5, 15), QPointF(13.5, 15));
        p.drawLine(QPointF(13.5, 15), QPointF(13.5, 19));
        break;
    }

    case CUINavBarItem::Icon::Search: {
        p.drawEllipse(QPointF(11, 11), 4.5, 4.5); // 镜片
        p.drawLine(QPointF(14.5, 14.5), QPointF(18.5, 18.5)); // 手柄
        break;
    }

    case CUINavBarItem::Icon::Folder: {
        QPainterPath folder; // 文件夹主体
        folder.moveTo(4, 7);
        folder.lineTo(4, 18);
        folder.lineTo(20, 18);
        folder.lineTo(20, 7);
        p.drawPath(folder);
        QPainterPath tab; // 标签
        tab.moveTo(8, 7);
        tab.lineTo(8, 5);
        tab.lineTo(12, 5);
        tab.lineTo(14, 7);
        p.drawPath(tab);
        break;
    }

    case CUINavBarItem::Icon::File: {
        QPainterPath doc; // 文件（折角）
        doc.moveTo(6, 4);
        doc.lineTo(14, 4);
        doc.lineTo(18, 8);
        doc.lineTo(18, 20);
        doc.lineTo(6, 20);
        doc.closeSubpath();
        p.drawPath(doc);
        p.drawLine(QPointF(14, 4), QPointF(14, 8));  // 折角斜边
        p.drawLine(QPointF(14, 8), QPointF(18, 8));  // 折角横边
        p.drawLine(QPointF(9, 12.5), QPointF(15, 12.5)); // 内容行
        p.drawLine(QPointF(9, 15.5), QPointF(15, 15.5));
        break;
    }

    case CUINavBarItem::Icon::Heart: {
        p.drawEllipse(QPointF(8.5, 9.5), 3, 3);  // 左瓣
        p.drawEllipse(QPointF(15.5, 9.5), 3, 3); // 右瓣
        QPainterPath v; // 尖端
        v.moveTo(5.5, 11);
        v.lineTo(18.5, 11);
        v.lineTo(12, 19.5);
        v.closeSubpath();
        p.drawPath(v);
        break;
    }

    case CUINavBarItem::Icon::Bell: {
        QPainterPath bell;
        bell.moveTo(7, 10.5);
        bell.quadTo(7, 6, 12, 6); // 钟顶
        bell.quadTo(17, 6, 17, 10.5);
        bell.lineTo(17, 15.5);
        bell.lineTo(19, 17.5); // 钟沿
        bell.lineTo(5, 17.5);
        bell.lineTo(7, 15.5);
        bell.closeSubpath();
        p.drawPath(bell);
        p.drawEllipse(QPointF(12, 19.5), 1.6, 1.6); // 铃舌
        break;
    }

    case CUINavBarItem::Icon::Clock: {
        p.drawEllipse(QPointF(12, 12), 7, 7); // 表盘
        p.drawLine(QPointF(12, 12), QPointF(12, 7.5));    // 分针
        p.drawLine(QPointF(12, 12), QPointF(15.5, 13.5)); // 时针
        break;
    }

    case CUINavBarItem::Icon::Mail: {
        p.drawRoundedRect(QRectF(4, 7, 16, 10), 1.5, 1.5); // 信封
        p.drawLine(QPointF(4.5, 8), QPointF(12, 13.5));    // 折线
        p.drawLine(QPointF(19.5, 8), QPointF(12, 13.5));
        break;
    }

    case CUINavBarItem::Icon::MapPin: {
        p.drawEllipse(QPointF(12, 9.5), 3.5, 3.5); // 圆头
        QPainterPath tail; // 尖端
        tail.moveTo(9.5, 11.5);
        tail.lineTo(14.5, 11.5);
        tail.lineTo(12, 18.5);
        tail.closeSubpath();
        p.drawPath(tail);
        break;
    }

    case CUINavBarItem::Icon::Send: {
        QPainterPath plane; // 纸飞机
        plane.moveTo(4.5, 12.5);
        plane.lineTo(19.5, 5);
        plane.lineTo(16, 18.5);
        plane.lineTo(12, 13.5);
        plane.closeSubpath();
        p.drawPath(plane);
        break;
    }

    case CUINavBarItem::Icon::Zap: {
        QPainterPath bolt; // 闪电
        bolt.moveTo(13.5, 3.5);
        bolt.lineTo(8, 13.5);
        bolt.lineTo(11.5, 13.5);
        bolt.lineTo(10, 20.5);
        bolt.lineTo(16.5, 10.5);
        bolt.lineTo(12.5, 10.5);
        bolt.closeSubpath();
        p.drawPath(bolt);
        break;
    }

    case CUINavBarItem::Icon::Lock: {
        p.drawArc(QRectF(7.5, 5.5, 9, 9), 180 * 16, 180 * 16); // 锁环（上半圆）
        p.drawRoundedRect(QRectF(5.5, 10.5, 13, 8.5), 1.5, 1.5); // 锁体
        p.drawEllipse(QPointF(12, 13.8), 1.2, 1.2); // 锁孔
        p.drawLine(QPointF(12, 14.8), QPointF(12, 16.5));
        break;
    }

    case CUINavBarItem::Icon::Plus:
        p.drawLine(QPointF(c - 4, c), QPointF(c + 4, c));
        p.drawLine(QPointF(c, c - 4), QPointF(c, c + 4));
        break;

    case CUINavBarItem::Icon::Minus:
        p.drawLine(QPointF(c - 4, c), QPointF(c + 4, c));
        break;

    case CUINavBarItem::Icon::Check: {
        QPainterPath tick;
        tick.moveTo(5, 12.5);
        tick.lineTo(9.5, 17);
        tick.lineTo(19, 7);
        p.drawPath(tick);
        break;
    }

    case CUINavBarItem::Icon::Close:
        p.drawLine(QPointF(6, 6), QPointF(18, 18));
        p.drawLine(QPointF(18, 6), QPointF(6, 18));
        break;

    case CUINavBarItem::Icon::Info:
        p.drawEllipse(QPointF(12, 12), 6, 6);
        p.drawLine(QPointF(12, 11), QPointF(12, 16));
        p.drawEllipse(QPointF(12, 7.5), 1.2, 1.2);
        break;

    case CUINavBarItem::Icon::Question:
        p.drawEllipse(QPointF(12, 7.5), 3.5, 3.5); // 问号头部
        p.drawLine(QPointF(12, 11), QPointF(12, 14));
        p.drawEllipse(QPointF(12, 16.5), 1.2, 1.2); // 问号点
        break;

    case CUINavBarItem::Icon::Edit: {
        QPainterPath caret; // 编辑/列表
        caret.moveTo(6, 8);
        caret.lineTo(9, 5);
        caret.lineTo(12, 8);
        p.drawPath(caret);
        p.drawLine(QPointF(6, 12), QPointF(18, 12));
        p.drawLine(QPointF(6, 16), QPointF(14, 16));
        break;
    }

    case CUINavBarItem::Icon::Trash: {
        p.drawLine(QPointF(6, 7), QPointF(18, 7));    // 盖子
        p.drawLine(QPointF(10, 7), QPointF(10, 4.5)); // 把手
        p.drawLine(QPointF(10, 4.5), QPointF(14, 4.5));
        p.drawLine(QPointF(14, 4.5), QPointF(14, 7));
        p.drawLine(QPointF(7.5, 10), QPointF(9, 19)); // 桶身
        p.drawLine(QPointF(9, 19), QPointF(15, 19));
        p.drawLine(QPointF(15, 19), QPointF(16.5, 10));
        p.drawLine(QPointF(10.5, 13), QPointF(10.5, 17));
        p.drawLine(QPointF(13.5, 13), QPointF(13.5, 17));
        break;
    }

    case CUINavBarItem::Icon::Refresh: {
        p.drawArc(QRectF(6.5, 6.5, 11, 11), 40 * 16, 250 * 16); // 环形箭头
        QPainterPath head; // 箭头
        head.moveTo(13.5, 6.2);
        head.lineTo(17, 5.5);
        head.lineTo(16.5, 9);
        p.drawPath(head);
        break;
    }

    case CUINavBarItem::Icon::Download: {
        p.drawLine(QPointF(12, 4), QPointF(12, 14));
        QPainterPath arr;
        arr.moveTo(7.5, 10);
        arr.lineTo(12, 14.5);
        arr.lineTo(16.5, 10);
        p.drawPath(arr);
        p.drawLine(QPointF(5, 17.5), QPointF(19, 17.5)); // 托盘
        break;
    }

    case CUINavBarItem::Icon::Upload: {
        p.drawLine(QPointF(12, 16), QPointF(12, 6));
        QPainterPath arr;
        arr.moveTo(7.5, 9.5);
        arr.lineTo(12, 5);
        arr.lineTo(16.5, 9.5);
        p.drawPath(arr);
        p.drawLine(QPointF(5, 17.5), QPointF(19, 17.5)); // 托盘
        break;
    }

    case CUINavBarItem::Icon::Copy:
        p.drawRoundedRect(QRectF(5, 3, 10, 10), 1.5, 1.5); // 后页
        p.drawRoundedRect(QRectF(9, 7, 10, 10), 1.5, 1.5); // 前页
        break;

    case CUINavBarItem::Icon::Calendar: {
        p.drawRoundedRect(QRectF(5, 5, 14, 13), 1.5, 1.5); // 日历体
        p.drawLine(QPointF(5, 9), QPointF(19, 9));         // 表头线
        p.drawLine(QPointF(8.5, 5), QPointF(8.5, 3));      // 挂环
        p.drawLine(QPointF(15.5, 5), QPointF(15.5, 3));
        p.drawEllipse(QPointF(9, 12.5), 1, 1);             // 日期点
        p.drawEllipse(QPointF(13, 12.5), 1, 1);
        p.drawEllipse(QPointF(17, 12.5), 1, 1);
        break;
    }

    case CUINavBarItem::Icon::Flag: {
        p.drawLine(QPointF(8, 4), QPointF(8, 20)); // 旗杆
        QPainterPath pennant; // 三角旗
        pennant.moveTo(8, 5);
        pennant.lineTo(17, 5);
        pennant.lineTo(8, 9.5);
        pennant.closeSubpath();
        p.drawPath(pennant);
        break;
    }

    case CUINavBarItem::Icon::Camera: {
        p.drawRoundedRect(QRectF(4, 7.5, 16, 9), 2, 2);   // 机身
        p.drawLine(QPointF(7.5, 7.5), QPointF(7.5, 5.5)); // 顶部凸起
        p.drawLine(QPointF(7.5, 5.5), QPointF(16.5, 5.5));
        p.drawLine(QPointF(16.5, 5.5), QPointF(16.5, 7.5));
        p.drawEllipse(QPointF(12, 12), 3, 3);             // 镜头
        break;
    }

    case CUINavBarItem::Icon::Power:
        p.drawLine(QPointF(12, 3.5), QPointF(12, 11)); // 竖线
        p.drawArc(QRectF(6.5, 8.5, 11, 11), 30 * 16, 240 * 16); // 圆弧
        break;

    case CUINavBarItem::Icon::Sun: {
        p.drawEllipse(QPointF(12, 12), 4, 4); // 太阳
        for (int i = 0; i < 8; ++i) {         // 光芒
            const qreal ang = i * kPi / 4;
            const qreal r1 = 6.2, r2 = 8.6;
            p.drawLine(QPointF(12 + r1 * qCos(ang), 12 + r1 * qSin(ang)),
                       QPointF(12 + r2 * qCos(ang), 12 + r2 * qSin(ang)));
        }
        break;
    }

    case CUINavBarItem::Icon::Moon: // 月牙（两段偏移圆弧）
        p.drawArc(QRectF(6.5, 6.5, 11, 11), 115 * 16, 130 * 16);
        p.drawArc(QRectF(9.5, 8.5, 11, 11), 115 * 16, 130 * 16);
        break;

    case CUINavBarItem::Icon::Cloud: {
        QPainterPath cloud;
        cloud.moveTo(6, 16);
        cloud.quadTo(4, 16, 4, 13.5);
        cloud.quadTo(4, 10.5, 6.5, 10.5);
        cloud.quadTo(8, 8.5, 10.5, 9);
        cloud.quadTo(13, 7.5, 15, 9.5);
        cloud.quadTo(18.5, 9.5, 18.5, 12.5);
        cloud.quadTo(18.5, 16, 16, 16);
        cloud.closeSubpath();
        p.drawPath(cloud);
        break;
    }

    case CUINavBarItem::Icon::Alert: {
        QPainterPath tri; // 警告三角
        tri.moveTo(12, 4.5);
        tri.lineTo(20.5, 19);
        tri.lineTo(3.5, 19);
        tri.closeSubpath();
        p.drawPath(tri);
        p.drawLine(QPointF(12, 9.5), QPointF(12, 14.5));
        p.drawEllipse(QPointF(12, 16.8), 1.1, 1.1);
        break;
    }

    case CUINavBarItem::Icon::Shield: {
        QPainterPath sh; // 盾牌
        sh.moveTo(12, 3.5);
        sh.lineTo(19, 6);
        sh.lineTo(19, 11.5);
        sh.quadTo(19, 16.5, 12, 20.5);
        sh.quadTo(5, 16.5, 5, 11.5);
        sh.lineTo(5, 6);
        sh.closeSubpath();
        p.drawPath(sh);
        QPainterPath tick; // 对勾
        tick.moveTo(8.5, 11.5);
        tick.lineTo(11, 14);
        tick.lineTo(15.5, 9);
        p.drawPath(tick);
        break;
    }

    case CUINavBarItem::Icon::Eye:
        p.drawEllipse(QPointF(12, 12), 6.5, 4.5); // 眼形
        p.drawEllipse(QPointF(12, 12), 2, 2);     // 瞳孔
        break;

    case CUINavBarItem::Icon::Key:
        p.drawEllipse(QPointF(8.5, 9.5), 3.6, 3.6); // 钥匙环
        p.drawLine(QPointF(11.2, 12.2), QPointF(17.5, 18.5)); // 杆
        p.drawLine(QPointF(17.5, 18.5), QPointF(19.5, 16.5)); // 齿
        p.drawLine(QPointF(15.5, 16.5), QPointF(13.5, 18.5));
        break;

    case CUINavBarItem::Icon::Tag: {
        QPainterPath tag; // 标签
        tag.moveTo(4.5, 4.5);
        tag.lineTo(13.5, 4.5);
        tag.lineTo(19.5, 10.5);
        tag.lineTo(10.5, 19.5);
        tag.lineTo(4.5, 13.5);
        tag.closeSubpath();
        p.drawPath(tag);
        p.drawEllipse(QPointF(8.5, 8.5), 1.2, 1.2); // 孔
        break;
    }

    case CUINavBarItem::Icon::Gift:
        p.drawRoundedRect(QRectF(6, 10, 12, 9), 1, 1);   // 盒身
        p.drawLine(QPointF(4.5, 10), QPointF(19.5, 10)); // 盒盖
        p.drawLine(QPointF(12, 10), QPointF(12, 19));    // 竖丝带
        p.drawLine(QPointF(8, 6.5), QPointF(12, 9.5));   // 蝴蝶结左
        p.drawLine(QPointF(16, 6.5), QPointF(12, 9.5));  // 蝴蝶结右
        p.drawEllipse(QPointF(12, 9.5), 1.1, 1.1);       // 结心
        break;

    case CUINavBarItem::Icon::Music:
        p.drawLine(QPointF(14.5, 5.5), QPointF(14.5, 16)); // 符干
        p.drawEllipse(QPointF(11.5, 16.5), 2.4, 2.4);      // 符头
        {
            QPainterPath flag; // 符尾
            flag.moveTo(14.5, 5.5);
            flag.quadTo(18, 7, 16.5, 10);
            p.drawPath(flag);
        }
        break;

    case CUINavBarItem::Icon::Pause:
        p.drawLine(QPointF(8.5, 6), QPointF(8.5, 18));
        p.drawLine(QPointF(15.5, 6), QPointF(15.5, 18));
        break;

    case CUINavBarItem::Icon::Stop:
        p.setBrush(color); // 实心方块
        p.drawRoundedRect(QRectF(7, 7, 10, 10), 2, 2);
        p.setBrush(Qt::NoBrush);
        break;

    case CUINavBarItem::Icon::Battery:
        p.drawRoundedRect(QRectF(4.5, 8, 14, 8.5), 1.5, 1.5); // 电池体
        p.drawLine(QPointF(18.5, 11), QPointF(19.5, 11));     // 正极
        p.drawLine(QPointF(18.5, 13.5), QPointF(19.5, 13.5));
        p.drawLine(QPointF(7, 11), QPointF(12, 11));          // 电量
        break;

    case CUINavBarItem::Icon::Chart: // 柱状图
        p.drawLine(QPointF(8.5, 12), QPointF(8.5, 19));
        p.drawLine(QPointF(13, 8), QPointF(13, 19));
        p.drawLine(QPointF(17.5, 5), QPointF(17.5, 19));
        p.drawLine(QPointF(5.5, 19), QPointF(20, 19));
        break;

    case CUINavBarItem::Icon::Card:
        p.drawRoundedRect(QRectF(4.5, 6, 15, 12), 2, 2); // 银行卡
        p.drawLine(QPointF(4.5, 10), QPointF(19.5, 10)); // 磁条
        p.drawEllipse(QPointF(9, 14), 1.2, 1.2);         // 芯片
        break;

    case CUINavBarItem::Icon::Filter: {
        QPainterPath funnel; // 漏斗/筛选
        funnel.moveTo(4.5, 5);
        funnel.lineTo(19.5, 5);
        funnel.lineTo(14, 12);
        funnel.lineTo(14, 18.5);
        funnel.lineTo(10, 18.5);
        funnel.lineTo(10, 12);
        funnel.closeSubpath();
        p.drawPath(funnel);
        break;
    }

    case CUINavBarItem::Icon::ChevronRight: {
        QPainterPath ch; // 右箭头（展开）
        ch.moveTo(9, 7);
        ch.lineTo(15, 12);
        ch.lineTo(9, 17);
        p.drawPath(ch);
        break;
    }

    case CUINavBarItem::Icon::ChevronLeft: {
        QPainterPath ch; // 左箭头（收起）
        ch.moveTo(15, 7);
        ch.lineTo(9, 12);
        ch.lineTo(15, 17);
        p.drawPath(ch);
        break;
    }

    // A-Z 字母图标：居中绘制字母（颜色由调用方决定；按钮/收起态经 makeBlueIcon 渲染为蓝色）
    case CUINavBarItem::Icon::IconA:
    case CUINavBarItem::Icon::IconB:
    case CUINavBarItem::Icon::IconC:
    case CUINavBarItem::Icon::IconD:
    case CUINavBarItem::Icon::IconE:
    case CUINavBarItem::Icon::IconF:
    case CUINavBarItem::Icon::IconG:
    case CUINavBarItem::Icon::IconH:
    case CUINavBarItem::Icon::IconI:
    case CUINavBarItem::Icon::IconJ:
    case CUINavBarItem::Icon::IconK:
    case CUINavBarItem::Icon::IconL:
    case CUINavBarItem::Icon::IconM:
    case CUINavBarItem::Icon::IconN:
    case CUINavBarItem::Icon::IconO:
    case CUINavBarItem::Icon::IconP:
    case CUINavBarItem::Icon::IconQ:
    case CUINavBarItem::Icon::IconR:
    case CUINavBarItem::Icon::IconS:
    case CUINavBarItem::Icon::IconT:
    case CUINavBarItem::Icon::IconU:
    case CUINavBarItem::Icon::IconV:
    case CUINavBarItem::Icon::IconW:
    case CUINavBarItem::Icon::IconX:
    case CUINavBarItem::Icon::IconY:
    case CUINavBarItem::Icon::IconZ: {
        QFont font = p.font();
        font.setPixelSize(16); // 24x24 设计坐标下约占 2/3，与其余图标的笔画尺寸匹配
        font.setBold(true);
        p.setFont(font);
        p.drawText(QRectF(0, 0, 24, 24), Qt::AlignCenter, QString(letterChar(kind)));
        break;
    }
    }

    p.end();
    return pm;
}

// 树节点图标：普通态深色（浅色背景上始终可见），选中/激活态白色
QIcon makeNavIcon(CUINavBarItem::Icon kind)
{
    const QPixmap dark = drawIcon(kind, QColor(0x2B, 0x2F, 0x36)); // 深色，替代近白
    const QPixmap white = drawIcon(kind, QColor(0xEA, 0xEC, 0xEF));
    const QPixmap dim = drawIcon(kind, QColor(0x55, 0x58, 0x5E));

    QIcon icon;
    icon.addPixmap(dark, QIcon::Normal, QIcon::Off);
    icon.addPixmap(white, QIcon::Active, QIcon::Off);
    icon.addPixmap(white, QIcon::Active, QIcon::On);
    icon.addPixmap(white, QIcon::Selected, QIcon::Off);
    icon.addPixmap(dim, QIcon::Disabled, QIcon::Off);
    return icon;
}

// 蓝色图标（收起态使用：普通态蓝色，选中/悬停态白色）
QIcon makeBlueIcon(CUINavBarItem::Icon kind)
{
    const QPixmap blue = drawIcon(kind, QColor(0x3B, 0x82, 0xF6));
    const QPixmap white = drawIcon(kind, QColor(0xFF, 0xFF, 0xFF));
    const QPixmap dim = drawIcon(kind, QColor(0x55, 0x58, 0x5E));

    QIcon icon;
    icon.addPixmap(blue, QIcon::Normal, QIcon::Off);
    icon.addPixmap(white, QIcon::Active, QIcon::Off);
    icon.addPixmap(white, QIcon::Active, QIcon::On);
    icon.addPixmap(white, QIcon::Selected, QIcon::Off);
    icon.addPixmap(dim, QIcon::Disabled, QIcon::Off);
    return icon;
}

// 树节点绘制代理：手动绘制图标 + 文字，保证图标一直显示
// （Qt5 中 QTreeView::item 的 QSS 样式会导致节点图标不绘制，因此不用 QSS 画节点）
class NavTreeDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        const bool hovered = option.state & QStyle::State_MouseOver;
        const bool selected = option.state & QStyle::State_Selected;
        const bool active = hovered || selected;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        // 背景：悬停/选中 → 蓝色圆角
        if (active) {
            const QRect bg = option.rect.adjusted(4, 1, -4, -1);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0x25, 0x63, 0xEB));
            painter->drawRoundedRect(bg, 6, 6);
        }

        // 图标：始终绘制（悬停/选中用白色，普通态深色）
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        const QSize iconSize(18, 18);
        if (!icon.isNull()) {
            const QPixmap pm = icon.pixmap(iconSize, active ? QIcon::Active : QIcon::Normal);
            const int x = option.rect.left() + 8;
            const int y = option.rect.top() + (option.rect.height() - iconSize.height()) / 2;
            painter->drawPixmap(x, y, pm);
        }

        // 文字
        const QRect textRect = option.rect.adjusted(8 + iconSize.width() + 6, 0, -6, 0);
        painter->setPen(active ? QColor(Qt::white) : QColor(0x00, 0x00, 0x00));
        painter->setFont(option.font);
        painter->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                          index.data(Qt::DisplayRole).toString());

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        Q_UNUSED(option);
        Q_UNUSED(index);
        return QSize(0, 28);
    }
};

// 全部内置图标池（供随机选取使用）
const QVector<CUINavBarItem::Icon> kAllIcons = {
    CUINavBarItem::Icon::Chat,     CUINavBarItem::Icon::Tasks,     CUINavBarItem::Icon::Agents,
    CUINavBarItem::Icon::Jobs,     CUINavBarItem::Icon::Skills,    CUINavBarItem::Icon::Workflow,
    CUINavBarItem::Icon::Grid,     CUINavBarItem::Icon::Settings,
    CUINavBarItem::Icon::Home,     CUINavBarItem::Icon::Search,    CUINavBarItem::Icon::Folder,
    CUINavBarItem::Icon::File,     CUINavBarItem::Icon::Heart,     CUINavBarItem::Icon::Bell,
    CUINavBarItem::Icon::Clock,    CUINavBarItem::Icon::Mail,      CUINavBarItem::Icon::MapPin,
    CUINavBarItem::Icon::Send,     CUINavBarItem::Icon::Zap,       CUINavBarItem::Icon::Lock,
    CUINavBarItem::Icon::Plus,     CUINavBarItem::Icon::Minus,     CUINavBarItem::Icon::Check,
    CUINavBarItem::Icon::Close,    CUINavBarItem::Icon::Info,      CUINavBarItem::Icon::Question,
    CUINavBarItem::Icon::Edit,     CUINavBarItem::Icon::Trash,     CUINavBarItem::Icon::Refresh,
    CUINavBarItem::Icon::Download, CUINavBarItem::Icon::Upload,    CUINavBarItem::Icon::Copy,
    CUINavBarItem::Icon::Calendar, CUINavBarItem::Icon::Flag,      CUINavBarItem::Icon::Camera,
    CUINavBarItem::Icon::Power,
    CUINavBarItem::Icon::Sun,      CUINavBarItem::Icon::Moon,      CUINavBarItem::Icon::Cloud,
    CUINavBarItem::Icon::Alert,    CUINavBarItem::Icon::Shield,    CUINavBarItem::Icon::Eye,
    CUINavBarItem::Icon::Key,      CUINavBarItem::Icon::Tag,       CUINavBarItem::Icon::Gift,
    CUINavBarItem::Icon::Music,    CUINavBarItem::Icon::Pause,     CUINavBarItem::Icon::Stop,
    CUINavBarItem::Icon::Battery,  CUINavBarItem::Icon::Chart,     CUINavBarItem::Icon::Card,
    CUINavBarItem::Icon::Filter,
    // A-Z 字母
    CUINavBarItem::Icon::IconA,    CUINavBarItem::Icon::IconB,     CUINavBarItem::Icon::IconC,
    CUINavBarItem::Icon::IconD,    CUINavBarItem::Icon::IconE,     CUINavBarItem::Icon::IconF,
    CUINavBarItem::Icon::IconG,    CUINavBarItem::Icon::IconH,     CUINavBarItem::Icon::IconI,
    CUINavBarItem::Icon::IconJ,    CUINavBarItem::Icon::IconK,     CUINavBarItem::Icon::IconL,
    CUINavBarItem::Icon::IconM,    CUINavBarItem::Icon::IconN,     CUINavBarItem::Icon::IconO,
    CUINavBarItem::Icon::IconP,    CUINavBarItem::Icon::IconQ,     CUINavBarItem::Icon::IconR,
    CUINavBarItem::Icon::IconS,    CUINavBarItem::Icon::IconT,     CUINavBarItem::Icon::IconU,
    CUINavBarItem::Icon::IconV,    CUINavBarItem::Icon::IconW,     CUINavBarItem::Icon::IconX,
    CUINavBarItem::Icon::IconY,    CUINavBarItem::Icon::IconZ,
};

} // namespace

QIcon CUINavBarItem::makeLetterIcon(const QChar &letter, const QColor &bg, int size)
{
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    // 圆角方块背景
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(size * 0.04, size * 0.04, size * 0.92, size * 0.92),
                      size * 0.22, size * 0.22);

    // 白色加粗字母
    p.setPen(QColor(Qt::white));
    QFont font = p.font();
    font.setBold(true);
    font.setPixelSize(int(size * 0.58));
    p.setFont(font);
    p.drawText(QRectF(0, 0, size, size), Qt::AlignCenter, QString(letter));

    p.end();
    return QIcon(pm);
}

CUINavBarItem::CUINavBarItem(QWidget *parent)
    : QWidget(parent)
{
    initUi();
    initStyle();
}

CUINavBarItem::~CUINavBarItem() = default;

int CUINavBarItem::currentIndex() const
{
    return m_buttons.indexOf(qobject_cast<QToolButton *>(m_group->checkedButton()));
}

QString CUINavBarItem::currentTitle() const
{
    const int idx = currentIndex();
    return idx >= 0 ? m_titles.value(idx) : QString();
}

int CUINavBarItem::itemCount() const
{
    return m_titles.size();
}

void CUINavBarItem::setExpanded(bool expanded)
{
    if (m_expanded == expanded) {
        return;
    }
    m_expanded = expanded;
    applyExpandedState();
    emit expandedChanged(m_expanded);
}

bool CUINavBarItem::isExpanded() const
{
    return m_expanded;
}

bool CUINavBarItem::eventFilter(QObject *obj, QEvent *event)
{
    // 鼠标单击第 0 部分图标：左键 → infoClicked，右键 → infoRightClicked
    if (obj == m_attrIcon && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (m_attrIcon->rect().contains(me->pos())) {
            if (me->button() == Qt::LeftButton) {
                emit infoClicked();
            } else if (me->button() == Qt::RightButton) {
                emit infoRightClicked();
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

void CUINavBarItem::SetInfoIcon(const QIcon &icon)
{
    if (icon.isNull()) {
        return;
    }
    m_infoIcon = icon;
    updateInfoIconSize();
}

void CUINavBarItem::updateInfoIconSize()
{
    if (!m_attrIcon || m_infoIcon.isNull()) {
        return;
    }
    // 图标始终放大到与侧边栏收起时的宽度一致（72px），收起/展开不变
    const int size = 72;
    QPixmap pm = m_infoIcon.pixmap(size, size, QIcon::Normal, QIcon::Off);
    // 强制位图尺寸精确为 72x72（不依赖 Qt 隐式缩放，避免 DPI 导致的显示偏差）
    if (pm.size() != QSize(size, size)) {
        pm = pm.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    pm.setDevicePixelRatio(1.0);
    m_attrIcon->setPixmap(pm);
}

void CUINavBarItem::SetInfoCode(const QString &code)
{
    if (m_infoCodeLabel) {
        m_infoCodeLabel->setText(code);
    }
}

QString CUINavBarItem::GetInfoCode() const
{
    return m_infoCodeLabel ? m_infoCodeLabel->text() : QString();
}

void CUINavBarItem::SetInfoName(const QString &name)
{
    if (m_infoNameLabel) {
        m_infoNameLabel->setText(name);
    }
}

QString CUINavBarItem::GetInfoName() const
{
    return m_infoNameLabel ? m_infoNameLabel->text() : QString();
}

void CUINavBarItem::SetInfoText(const QString &text)
{
    if (m_infoTextLabel) {
        m_infoTextLabel->setText(text);
    }
}

QString CUINavBarItem::GetInfoText() const
{
    return m_infoTextLabel ? m_infoTextLabel->text() : QString();
}

QTreeWidget *CUINavBarItem::tree() const
{
    return m_tree;
}

QTreeWidgetItem *CUINavBarItem::addTreeNode(const QString &title, QTreeWidgetItem *parent)
{
    auto *item = new QTreeWidgetItem;
    item->setText(0, title);
    if (parent) {
        parent->addChild(item); // 作为某个节点的下级（任意层级）
    } else if (m_tree) {
        m_tree->addTopLevelItem(item); // 作为树的顶层分组节点
    }
    return item;
}

QTreeWidgetItem *CUINavBarItem::addTreeNode(const QString &title, Icon icon, QTreeWidgetItem *parent)
{
    auto *item = addTreeNode(title, parent);
    item->setIcon(0, makeNavIcon(icon));
    return item;
}

void CUINavBarItem::clearTree()
{
    if (m_tree) {
        m_tree->clear();
    }
}

QString CUINavBarItem::AddTopBtn(const QString &title)
{
    // 图标从全部内置图标中随机选取（每次启动都重新随机）
    const Icon ic = kAllIcons.at(QRandomGenerator::global()->bounded(kAllIcons.size()));
    QToolButton *btn = createItemButton(title, ic);
    // 新按钮需要立即应用当前的展开/收起状态（图标/文字/尺寸）
    updateButtonState(btn, title, ic);
    m_topColumnLayout->addWidget(btn);

    // 生成全局唯一 ID（QUuid，保证不重复），并记录 ID 与按钮的对应关系
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_topButtonIds.insert(id, btn);
    // 左键点击该按钮时，发出带 ID 的信号
    connect(btn, &QToolButton::clicked, this,
            [this, id]() { emit topbtnClicked(id); });
    // 第 1 部分出现内容，刷新显示状态
    updateSectionVisibility();
    return id;
}

QString CUINavBarItem::GetTopBtnTitle(const QString &id) const
{
    QToolButton *btn = m_topButtonIds.value(id);
    if (!btn) {
        return QString();
    }
    // 从标题列表取原始标题（按钮文字在收起态会被清空，不能直接取 text()）
    const int idx = m_buttons.indexOf(btn);
    return idx >= 0 ? m_titles.value(idx) : QString();
}

QString CUINavBarItem::AddBotBtn(const QString &title)
{
    // 图标从全部内置图标中随机选取（每次启动都重新随机）
    const Icon ic = kAllIcons.at(QRandomGenerator::global()->bounded(kAllIcons.size()));
    QToolButton *btn = createItemButton(title, ic);
    // 新按钮需要立即应用当前的展开/收起状态（图标/文字/尺寸）
    updateButtonState(btn, title, ic);
    m_bottomColumnLayout->addWidget(btn);

    // 生成全局唯一 ID（QUuid，保证不重复），并记录 ID 与按钮的对应关系
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_botButtonIds.insert(id, btn);
    // 左键点击该按钮时，发出带 ID 的信号
    connect(btn, &QToolButton::clicked, this,
            [this, id]() { emit botbtnClicked(id); });
    // 第 3 部分出现内容，刷新显示状态
    updateSectionVisibility();
    return id;
}

QString CUINavBarItem::GetBotBtnTitle(const QString &id) const
{
    QToolButton *btn = m_botButtonIds.value(id);
    if (!btn) {
        return QString();
    }
    // 从标题列表取原始标题（按钮文字在收起态会被清空，不能直接取 text()）
    const int idx = m_buttons.indexOf(btn);
    return idx >= 0 ? m_titles.value(idx) : QString();
}

QString CUINavBarItem::AddTreeMainItem(const QString &title)
{
    if (!m_tree) {
        return QString();
    }
    // 根目录菜单：图标随机（每次启动重新随机）
    const Icon ic = kAllIcons.at(QRandomGenerator::global()->bounded(kAllIcons.size()));
    QTreeWidgetItem *item = addTreeNode(title, ic);
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_treeItemIds.insert(id, item);
    m_treeItemIdByItem.insert(item, id);
    return id;
}

QString CUINavBarItem::AddTreeChildItem(const QString &title, const QString &mainid)
{
    if (!m_tree) {
        return QString();
    }
    QTreeWidgetItem *parent = m_treeItemIds.value(mainid);
    if (!parent) {
        return QString(); // mainid 无效
    }
    // 子菜单：图标随机
    const Icon ic = kAllIcons.at(QRandomGenerator::global()->bounded(kAllIcons.size()));
    QTreeWidgetItem *item = addTreeNode(title, ic, parent);
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_treeItemIds.insert(id, item);
    m_treeItemIdByItem.insert(item, id);
    return id;
}

QString CUINavBarItem::GetTreeItemTitle(const QString &id) const
{
    QTreeWidgetItem *item = m_treeItemIds.value(id);
    return item ? item->text(0) : QString();
}

void CUINavBarItem::TreeItemExpandToDepth(int depth)
{
    if (!m_tree) {
        return;
    }
    if (depth == -1) {
        m_tree->expandAll();   // 全部展开
    } else if (depth == 0) {
        m_tree->collapseAll(); // 全部收起
    } else {
        m_tree->expandToDepth(depth); // 按深度展开
    }
}

QScrollArea *CUINavBarItem::createButtonColumn(QVBoxLayout **innerLayout)
{
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("ButtonColumn"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded); // 按钮过多时显示竖滚动条
    scroll->setMinimumHeight(0); // 没有按钮时允许完全塌缩，不占位置
    scroll->viewport()->setAutoFillBackground(false);

    auto *container = new QWidget(scroll);
    container->setObjectName(QStringLiteral("ButtonColumn"));
    container->setAutoFillBackground(false);
    auto *layout = new QVBoxLayout(container);
    // 按钮列内部保留左右 10px 内边距（主布局已去边距）
    layout->setContentsMargins(10, 0, 10, 0);
    layout->setSpacing(6);

    scroll->setWidget(container);
    *innerLayout = layout;
    return scroll;
}

QToolButton *CUINavBarItem::createItemButton(const QString &title, Icon icon)
{
    auto *btn = new QToolButton(this);
    btn->setObjectName(QStringLiteral("NavButton"));
    btn->setIcon(makeNavIcon(icon));
    btn->setIconSize(QSize(22, 22));
    btn->setToolTip(title);
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFixedSize(40, 40);
    m_group->addButton(btn);

    const int index = m_titles.size();
    m_titles.append(title);
    m_buttons.append(btn);
    m_buttonIcons.append(icon);
    if (index == 0) {
        btn->setChecked(true); // 默认选中第一项
    }

    connect(btn, &QToolButton::clicked, this,
            [this, index, title]() { emit itemClicked(index, title); });
    return btn;
}

QTreeWidget *CUINavBarItem::createTree()
{
    auto *tree = new QTreeWidget(this);
    tree->setObjectName(QStringLiteral("NavTree"));
    tree->setHeaderHidden(true);
    tree->setIndentation(14);
    tree->setUniformRowHeights(true);
    tree->setIconSize(QSize(18, 18)); // 统一图标尺寸，确保一直显示
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setFocusPolicy(Qt::NoFocus);
    tree->setCursor(Qt::PointingHandCursor);
    // 使用自定义代理绘制节点（保证图标一直显示，不受 QSS ::item 渲染 bug 影响）
    tree->setItemDelegate(new NavTreeDelegate(tree));
    // 让树控件自身的 QSS 背景生效（视口不再用系统默认底色）
    tree->viewport()->setAutoFillBackground(false);

    connect(tree, &QTreeWidget::itemClicked, this,
            [this](QTreeWidgetItem *item, int) {
                // 发出带 ID 与“是否最末级”的信号
                const QString id = m_treeItemIdByItem.value(item);
                emit treeItemClicked(id, item->childCount() == 0);
            });

    // 树内容变化（增删节点）时刷新第 2 部分的显示状态（树本身与入口图标）
    connect(tree->model(), &QAbstractItemModel::rowsInserted, this,
            [this]() { updateSectionVisibility(); });
    connect(tree->model(), &QAbstractItemModel::rowsRemoved, this,
            [this]() { updateSectionVisibility(); });
    return tree;
}

void CUINavBarItem::initUi()
{
    setObjectName(QStringLiteral("NavBar"));
    setFixedWidth(72); // 默认收起（72px：40px 按钮 + 8px 滚动条 + 边距）

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    m_layout = new QVBoxLayout(this);
    // 主布局左右无边距：第 0 部分图标可通栏显示 72px；
    // 按钮列的内边距改到 createButtonColumn 内部
    m_layout->setContentsMargins(0, 12, 0, 12);
    m_layout->setSpacing(6);

    // ---- 第 0 部分：任务属性区（图标在上，三行文本在下）----
    m_attrIcon = new QLabel(this);
    m_attrIcon->setObjectName(QStringLiteral("AttrIcon"));
    m_attrIcon->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    // 固定图标区尺寸：宽 72（与侧边栏收起宽度一致）+ 高度加高，保证完整放大显示
    m_attrIcon->setFixedSize(72, 84);
    m_attrIcon->installEventFilter(this); // 监听单击事件，触发 infoClicked
    // 展开时图标区水平居中
    m_layout->addWidget(m_attrIcon, 0, Qt::AlignHCenter);
    // 默认图标：蓝色 D 字图标（直接按 72px 原生绘制，清晰不模糊），程序可通过 SetInfoIcon 修改
    SetInfoIcon(QIcon(drawIcon(Icon::IconD, QColor(0x3B, 0x82, 0xF6), 72)));

    m_infoCodeLabel = new QLabel(this);
    m_infoCodeLabel->setObjectName(QStringLiteral("AttrText"));
    m_infoCodeLabel->setAlignment(Qt::AlignHCenter);
    m_layout->addWidget(m_infoCodeLabel);

    m_infoNameLabel = new QLabel(this);
    m_infoNameLabel->setObjectName(QStringLiteral("AttrText"));
    m_infoNameLabel->setAlignment(Qt::AlignHCenter);
    m_layout->addWidget(m_infoNameLabel);

    m_infoTextLabel = new QLabel(this);
    m_infoTextLabel->setObjectName(QStringLiteral("AttrText"));
    m_infoTextLabel->setAlignment(Qt::AlignHCenter);
    m_infoTextLabel->setWordWrap(true);
    m_layout->addWidget(m_infoTextLabel);

    // 第 0 部分文本默认为空，由外部通过 SetInfoCode/SetInfoName/SetInfoText 设置
    m_layout->addSpacing(8);

    // ---- 第 1 部分：顶部图标按钮列（可滚动，无高度限制）----
    m_topColumn = createButtonColumn(&m_topColumnLayout);
    m_layout->addWidget(m_topColumn);
    // 第 1 部分按钮由外部通过 AddTopBtn() 动态添加（本类不内置任何测试按钮）；
    // 无按钮时整列隐藏（updateSectionVisibility 统一处理）

    // 弹性占位 1：收起时与占位 2 配合，让“第 2 部分图标”垂直居中
    // 注意：Qt5 的 addStretch 返回 void，需记录其布局序号以便后续调整
    m_layout->addStretch(1);
    m_spacer1Index = m_layout->count() - 1;

    // ---- 第 2 部分：树形菜单（展开时显示）----
    m_tree = createTree();
    m_layout->addWidget(m_tree, 1); // 树占据剩余空间
    m_tree->setVisible(false);

    // 树形菜单内容由外部通过 AddTreeMainItem / AddTreeChildItem 动态添加
    // （本类不内置任何测试数据）

    // 收起时第 2 部分的入口图标：点击后展开导航栏，显示树形菜单
    // （仅当树形菜单有内容时显示；展开时该图标隐藏，树本身直接显示）
    // 用蓝色 + 更大尺寸，让它在收起窄条中更醒目
    m_treeIconButton = new QToolButton(this);
    m_treeIconButton->setObjectName(QStringLiteral("NavButton"));
    m_treeIconButton->setIcon(QIcon(drawIcon(Icon::Grid, QColor(0x3B, 0x82, 0xF6))));
    m_treeIconButton->setIconSize(QSize(26, 26));
    m_treeIconButton->setToolTip(QStringLiteral("树形菜单"));
    m_treeIconButton->setCursor(Qt::PointingHandCursor);
    m_treeIconButton->setFixedSize(40, 40);
    m_layout->addWidget(m_treeIconButton);
    connect(m_treeIconButton, &QToolButton::clicked, this, [this]() {
        setExpanded(true); // 点击后展开导航栏，树形菜单随之显示
    });

    // 弹性占位 2：与占位 1 配合，收起时让“第 2 部分图标”垂直居中
    m_layout->addStretch(1);
    m_spacer2Index = m_layout->count() - 1;

    // ---- 第 3 部分：底部图标按钮列（可滚动，无高度限制）----
    m_bottomColumn = createButtonColumn(&m_bottomColumnLayout);
    m_layout->addWidget(m_bottomColumn);
    // 第 3 部分按钮由外部通过 AddBotBtn() 动态添加（本类不内置任何测试按钮）；
    // 无按钮时整列隐藏（updateSectionVisibility 统一处理）

    // ---- 系统固定功能按钮：展开/收起 ----
    // 始终作为主布局最后一项（不参与第 3 部分菜单列的滚动）；
    // 树无数据时随内容靠上排列，树有数据（收起态占位撑开）时落在底部
    // 后续任何数据/菜单测试都不得移动或删除该按钮
    m_toggleButton = new QToolButton(this);
    m_toggleButton->setObjectName(QStringLiteral("NavButton"));
    m_toggleButton->setIcon(makeNavIcon(Icon::ChevronRight));
    m_toggleButton->setIconSize(QSize(22, 22));
    m_toggleButton->setToolTip(QStringLiteral("展开"));
    m_toggleButton->setCursor(Qt::PointingHandCursor);
    m_toggleButton->setFixedSize(40, 40);
    m_layout->addWidget(m_toggleButton); // 直接加在主布局最底部，不随菜单滚动
    connect(m_toggleButton, &QToolButton::clicked, this, [this]() {
        setExpanded(!m_expanded);
    });

    applyExpandedState();
}

void CUINavBarItem::initStyle()
{
    setStyleSheet(QStringLiteral(R"(
        #NavBar {
            background-color: #101216;
            border-right: 1px solid #1E2128;
        }
        #AttrText {
            color: #000000;
            font-size: 11px;
        }
        #ButtonColumn {
            background: transparent;
            border: none;
        }
        #NavButton {
            background-color: transparent;
            border: none;
            border-radius: 10px;
            color: #000000;
        }
        #NavButton:hover {
            background-color: #2563EB;
            color: #FFFFFF;
        }
        #NavButton:checked {
            background-color: #2563EB;
            color: #FFFFFF;
        }
        #NavButton:checked:hover {
            background-color: #2563EB;
            color: #FFFFFF;
        }
        #NavButton:pressed {
            background-color: #1A4FC4;
        }
        #NavTree {
            background-color: #F6F7F9;
            border: none;
        }
        /* 滚动条（按钮列和树通用）：蓝色 */
        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #3B82F6;
            border-radius: 4px;
            min-height: 30px;
        }
        QScrollBar::handle:vertical:hover {
            background: #4C8DF7;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
            background: transparent;
        }
        QScrollBar:horizontal {
            background: transparent;
            height: 8px;
            margin: 0;
        }
        QScrollBar::handle:horizontal {
            background: #3B82F6;
            border-radius: 4px;
            min-width: 30px;
        }
        QScrollBar::handle:horizontal:hover {
            background: #4C8DF7;
        }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0;
        }
        QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {
            background: transparent;
        }
    )"));
}

void CUINavBarItem::updateButtonState(QToolButton *btn, const QString &title, Icon icon)
{
    btn->setText(m_expanded ? title : QString());
    btn->setToolButtonStyle(m_expanded ? Qt::ToolButtonTextBesideIcon
                                       : Qt::ToolButtonIconOnly);
    btn->setToolTip(m_expanded ? QString() : title);
    // 展开态图标稍大（24px），收起态 22px
    btn->setIconSize(QSize(m_expanded ? 24 : 22, m_expanded ? 24 : 22));
    if (m_expanded) {
        btn->setMinimumWidth(0);
        btn->setMaximumWidth(QWIDGETSIZE_MAX);
        btn->setFixedHeight(40);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    } else {
        btn->setFixedSize(40, 40);
    }
    // 统一使用蓝色图标
    btn->setIcon(makeBlueIcon(icon));
}

void CUINavBarItem::updateSectionVisibility()
{
    // 树是否有内容（顶层节点数 > 0），决定第 2 部分的显示策略
    const bool hasTreeContent = m_tree && m_tree->topLevelItemCount() > 0;

    // 第 1/3 部分是否有按钮
    const bool hasTopButtons = m_topColumnLayout && m_topColumnLayout->count() > 0;
    const bool hasBottomButtons = m_bottomColumnLayout && m_bottomColumnLayout->count() > 0;

    // 第 1 部分：靠上排列，内容高度，无高度限制；无按钮时整列隐藏
    if (m_topColumn) {
        m_topColumn->setVisible(hasTopButtons);
    }
    // 第 3 部分：靠下排列，内容高度，无高度限制；无按钮时整列隐藏
    if (m_bottomColumn) {
        m_bottomColumn->setVisible(hasBottomButtons);
    }

    // 第 2 部分：树 —— 仅“展开且有内容”时显示并占满中间剩余空间
    if (m_tree) {
        const bool showTree = m_expanded && hasTreeContent;
        m_tree->setVisible(showTree);
        const int treeItemIndex = m_layout ? m_layout->indexOf(m_tree) : -1;
        if (treeItemIndex >= 0) {
            m_layout->setStretch(treeItemIndex, showTree ? 1 : 0);
        }
    }

    // 弹性占位:占位符1完全去掉(展开/收起都 stretch=0,不占空间);
    // 占位符2在树不可见时撑开(第3部分靠下,中间留作第2部分区域),树可见时归零
    const int spacer1Stretch = 0;
    const int spacer2Stretch = (m_expanded && hasTreeContent) ? 0 : 1;
    if (m_layout && m_spacer1Index >= 0) {
        m_layout->setStretch(m_spacer1Index, spacer1Stretch);
    }
    if (m_layout && m_spacer2Index >= 0) {
        m_layout->setStretch(m_spacer2Index, spacer2Stretch);
    }

    // 第 2 部分入口图标：收起且树有内容时显示
    updateTreeIconVisibility();
}

void CUINavBarItem::updateTreeIconVisibility()
{
    if (!m_treeIconButton) {
        return;
    }
    // 仅当侧边栏收起且树形菜单有内容（顶层节点数 > 0）时显示第 2 部分入口图标
    const bool hasTreeContent = m_tree && m_tree->topLevelItemCount() > 0;
    m_treeIconButton->setVisible(!m_expanded && hasTreeContent);
}

void CUINavBarItem::applyExpandedState()
{
    // 收起 72px（图标窄条，留出滚动条空间）；
    // 展开时占满容器宽度（MainWindow 按主窗口宽度的 1/4 控制容器宽度）
    if (m_expanded) {
        setMinimumWidth(0);
        setMaximumWidth(QWIDGETSIZE_MAX);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    } else {
        setFixedWidth(72);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    }

    // 第 1/2/3 部分：无内容时隐藏，有数据时才显示
    // （含弹性占位伸缩系数与第 2 部分入口图标，统一在 updateSectionVisibility 中刷新）
    updateSectionVisibility();
    // 收起时第 0 部分只保留图标，三行文本隐藏
    for (QLabel *label : {m_infoCodeLabel, m_infoNameLabel, m_infoTextLabel}) {
        if (label) {
            label->setVisible(m_expanded);
        }
    }
    // 第 0 部分图标尺寸随展开/收起切换
    updateInfoIconSize();

    // 所有图标按钮按当前状态刷新（含运行中通过 AddTopBtn 新增的按钮）
    for (int i = 0; i < m_buttons.size(); ++i) {
        updateButtonState(m_buttons.at(i), m_titles.at(i), m_buttonIcons.at(i));
    }
    if (m_toggleButton) {
        updateButtonState(m_toggleButton, QString(),
                          m_expanded ? Icon::ChevronLeft : Icon::ChevronRight);
        m_toggleButton->setToolTip(m_expanded ? QStringLiteral("收起")
                                              : QStringLiteral("展开"));
    }
}
