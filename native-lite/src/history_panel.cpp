#include "history_panel.h"
#include "event_log.h"
#include "table_export.h"
#include <QtWidgets>

HistoryPanel::HistoryPanel(QWidget* parent) : QWidget(parent) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget;
    tabs_->setObjectName("historyTabs");
    for (const auto& title :
         {"变位遥信", "选中遥测的历史数据", "选中遥信的历史数据", "选中电度的历史数据", "链路监视"})
        tabs_->addTab(new QWidget, title);
    log_ = new EventLog;
    tabs_->addTab(log_, "通信日志");
    layout->addWidget(tabs_);
    history_ = new QWidget;
    auto content = new QVBoxLayout(history_);
    content->setContentsMargins(6, 0, 6, 4);
    auto bar = new QHBoxLayout;
    selected_ = new QLabel;
    selected_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    search_ = new QLineEdit;
    search_->setPlaceholderText("筛选引用、描述、值…");
    search_->setClearButtonEnabled(true);
    follow_ = new QCheckBox("跟随最新");
    follow_->setChecked(true);
    auto save = new QPushButton("导出当前页…");
    auto clear = new QPushButton("清空历史…");
    count_ = new QLabel;
    bar->addWidget(search_, 1);
    bar->addWidget(count_);
    bar->addWidget(follow_);
    bar->addWidget(save);
    bar->addWidget(clear);
    content->addWidget(selected_);
    content->addLayout(bar);
    model_ = new HistoryModel(this);
    filter_ = new HistoryFilter(model_, this);
    table_ = new QTableView;
    table_->setObjectName("historyTable");
    table_->setModel(filter_);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->hide();
    table_->setWordWrap(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->verticalHeader()->setDefaultSectionSize(28);
    const QList<int> widths{140, 65, 280, 160, 180, 135, 200, 200, 100, 200};
    for (int i = 0; i < widths.size(); ++i)
        table_->setColumnWidth(i, widths[i]);
    content->addWidget(table_, 1);
    layout->addWidget(history_, 1);
    auto note =
        new QLabel("已启用报告在后台持续记录，不受页面选择影响；未覆盖对象按需读取或加入监视。首次采样"
                   "不算变位，轮询可能漏掉快速变化。最多保留 5000 条，退出前请导出。");
    note->setWordWrap(true);
    content->addWidget(note);
    // A single table/proxy serves five tabs, avoiding five re-filter operations per sample.
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { updateFilter(); });
    auto debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(180);
    connect(search_, &QLineEdit::textChanged, debounce, [debounce] { debounce->start(); });
    connect(debounce, &QTimer::timeout, this, [this] { updateFilter(); });
    connect(filter_, &QAbstractItemModel::rowsInserted, this, [this] {
        count_->setText(QString("%1 条").arg(filter_->rowCount()));
        if (follow_->isChecked() && history_->isVisible())
            table_->scrollToBottom();
    });
    connect(filter_, &QAbstractItemModel::rowsRemoved, this,
            [this] { count_->setText(QString("%1 条").arg(filter_->rowCount())); });
    connect(filter_, &QAbstractItemModel::modelReset, this,
            [this] { count_->setText(QString("%1 条").arg(filter_->rowCount())); });
    connect(clear, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, "清空历史", "清空五个历史页中的全部记录？通信日志不受影响。") ==
            QMessageBox::Yes) {
            model_->clear();
            updateFilter();
        }
    });
    connect(save, &QPushButton::clicked, this, [this] { exportCurrent(); });
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table_, &QTableView::customContextMenuRequested, this, [this](QPoint pos) {
        auto index = table_->indexAt(pos);
        if (!index.isValid())
            return;
        if (!table_->selectionModel()->isSelected(index))
            table_->selectRow(index.row());
        QMenu menu;
        menu.addAction("复制选中行", this, [this] { copySelected(); });
        menu.addAction("复制引用", [this, index] {
            QApplication::clipboard()->setText(filter_->index(index.row(), 2).data().toString());
        });
        menu.addAction("导出当前页…", this, [this] { exportCurrent(); });
        menu.exec(table_->viewport()->mapToGlobal(pos));
    });
    auto copy = new QShortcut(QKeySequence::Copy, table_);
    connect(copy, &QShortcut::activated, this, [this] { copySelected(); });
    tabs_->setCurrentIndex(5);
    updateFilter();
}
void HistoryPanel::selectObject(QString device, QString reference) {
    device_ = device;
    reference_ = reference;
    updateFilter();
}
void HistoryPanel::updateFilter() {
    bool show = tabs_->currentIndex() != 5;
    history_->setVisible(show);
    // Empty tab pages must not consume the available log height.
    tabs_->setMaximumHeight(show ? tabs_->tabBar()->sizeHint().height() + 8 : QWIDGETSIZE_MAX);
    selected_->setText(reference_.isEmpty() ? "未选中对象：请在上方数据集、在线模型或监视列表选择对象。"
                                            : "选中对象：" + device_ + " | " + reference_);
    filter_->configure(tabs_->currentIndex(), device_, reference_, search_->text());
    count_->setText(QString("%1 条").arg(filter_->rowCount()));
    if (follow_->isChecked())
        table_->scrollToBottom();
}
void HistoryPanel::copySelected() {
    QStringList lines;
    auto rows = table_->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(), [](QModelIndex a, QModelIndex b) { return a.row() < b.row(); });
    for (auto row : rows) {
        QStringList fields;
        for (int c = 0; c < filter_->columnCount(); ++c)
            fields << filter_->index(row.row(), c).data().toString();
        lines << fields.join('\t');
    }
    QApplication::clipboard()->setText(lines.join('\n'));
}
void HistoryPanel::exportCurrent() {
    auto path = QFileDialog::getSaveFileName(this, "导出当前历史页", "IEC61850-history.csv", "CSV (*.csv)");
    if (path.isEmpty())
        return;
    if (!exportModelCsv(*filter_, path))
        QMessageBox::warning(this, "导出失败", "无法完整保存文件");
}
