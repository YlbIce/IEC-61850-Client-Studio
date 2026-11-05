#include "monitor_panel.h"
#include <QtWidgets>

MonitorPanel::MonitorPanel(QWidget* parent) : QWidget(parent) {
    auto layout = new QVBoxLayout(this);
    auto hint = new QLabel(
        "实时显示主值、质量、设备时标和 dU；展开对象查看正在监视的属性。优先接收报告，未覆盖"
        "对象每秒轮询最多 4 个，切换页面仍继续。\n读取失败时保留最后成功值，状态列显示异常；首次读取只建"
        "立基线，变化记录到下方日志。");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    tree_ = new QTreeWidget;
    tree_->setObjectName("monitorTable");
    tree_->setHeaderLabels({"对象 / 属性引用", "数据名称 (dU)", "当前值", "质量 (q)", "设备时标 (t)",
                            "监视状态", "最近成功读取（本机）"});
    tree_->setUniformRowHeights(true);
    tree_->setAlternatingRowColors(true);
    tree_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tree_->header()->setStretchLastSection(false);
    const QList<int> widths{310, 170, 190, 150, 220, 180, 210};
    for (int i = 0; i < widths.size(); ++i)
        tree_->setColumnWidth(i, widths[i]);
    layout->addWidget(tree_, 1);
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (!item)
            return;
        while (item->parent())
            item = item->parent();
        emit objectSelected(item->text(0));
    });
    auto buttons = new QHBoxLayout;
    auto remove = new QPushButton("移除选中监视");
    auto clear = new QPushButton("清空监视");
    buttons->addWidget(remove);
    buttons->addWidget(clear);
    buttons->addStretch();
    layout->addLayout(buttons);
    auto removeCurrent = [this] {
        auto item = tree_->currentItem();
        if (!item)
            return;
        while (item->parent())
            item = item->parent();
        emit removeRequested(item->text(0));
    };
    connect(remove, &QPushButton::clicked, this, removeCurrent);
    connect(clear, &QPushButton::clicked, this, &MonitorPanel::clearRequested);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, [this, removeCurrent](QPoint pos) {
        auto item = tree_->itemAt(pos);
        if (!item)
            return;
        tree_->setCurrentItem(item);
        QMenu menu;
        menu.addAction("复制引用", [item] { QApplication::clipboard()->setText(item->text(0)); });
        menu.addAction("复制当前值", [item] { QApplication::clipboard()->setText(item->text(2)); });
        menu.addAction("移除监视", removeCurrent);
        menu.exec(tree_->viewport()->mapToGlobal(pos));
    });
}
void MonitorPanel::setReferences(const QStringList& references) {
    for (const auto& ref : objects_.keys()) {
        if (!references.contains(ref))
            delete objects_.take(ref);
    }
    for (const auto& ref : references) {
        if (!objects_.contains(ref)) {
            auto item = new QTreeWidgetItem(tree_, {ref});
            item->setText(5, "等待读取");
            objects_.insert(ref, item);
        }
    }
}
void MonitorPanel::updateSnapshot(QString reference, QVariantMap snapshot, QString error) {
    auto item = objects_.value(reference);
    if (!item)
        return;
    item->setText(5, error.isEmpty()
                         ? (snapshot.value("source") == "MMS 报告" ? "正常 · 报告更新" : "正常 · 轮询中")
                         : "异常 · 值可能过期");
    item->setToolTip(5, error);
    item->setForeground(5, QColor(error.isEmpty() ? "#23764c" : "#b13636"));
    if (!error.isEmpty())
        return;
    const auto rows = snapshot.value("rows").toList();
    const QStringList fields{"name", "value", "quality", "time"};
    for (int col = 0; col < fields.size(); ++col) {
        QStringList values;
        for (const auto& row : rows) {
            const auto data = row.toMap();
            QString value = data.value(fields[col]).toString();
            if (rows.size() > 1)
                value = data.value("fc").toString() + ": " + value;
            values << value;
        }
        item->setText(col + 1, values.join(" | "));
        item->setToolTip(col + 1, values.join("\n"));
    }
    item->setText(6, snapshot.value("receivedAt").toString());
    const auto attrs = snapshot.value("attributes").toMap();
    QHash<QString, QTreeWidgetItem*> children;
    for (int i = 0; i < item->childCount(); ++i)
        children.insert(item->child(i)->text(0), item->child(i));
    for (const auto& key : children.keys())
        if (!attrs.contains(key))
            delete children.take(key);
    for (auto it = attrs.cbegin(); it != attrs.cend(); ++it) {
        auto child = children.value(it.key());
        if (!child)
            child = new QTreeWidgetItem(item, {it.key()});
        child->setText(2, it.value().toString());
        child->setToolTip(2, it.value().toString());
    }
}
