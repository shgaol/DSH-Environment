#include "TableView.h"

#include <QAbstractItemView>
#include <QHeaderView>

CTableView::CTableView(QWidget *parent)
    : QTableView(parent)
{
    // 基础配置：整行选中、单行选择、交替行色、禁止单元格内编辑
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setAlternatingRowColors(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    // 表头：支持点击排序，最后一列自动拉伸
    setSortingEnabled(true);
    horizontalHeader()->setStretchLastSection(true);
    horizontalHeader()->setHighlightSections(false);
    verticalHeader()->setVisible(false);
}

CTableView::~CTableView() = default;
