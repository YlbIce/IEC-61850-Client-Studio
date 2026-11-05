#include "event_log.h"
#include "striped_log_view.h"
#include <QtWidgets>

EventLog::EventLog(QWidget* parent) : QWidget(parent) {
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto bar = new QHBoxLayout;
    bar->setContentsMargins(8, 4, 8, 4);
    bar->setSpacing(8);
    category_ = new QComboBox;
    category_->addItems({"全部日志", "通信", "属性变化", "测试记录", "错误"});
    category_->setObjectName("logCategory");
    search_ = new QLineEdit;
    search_->setPlaceholderText("筛选对象引用、属性、内容…");
    search_->setClearButtonEnabled(true);
    search_->setObjectName("logSearch");
    follow_ = new QCheckBox("跟随最新");
    follow_->setChecked(true);
    auto clear = new QPushButton("清空");
    auto save = new QPushButton("导出筛选结果…");
    bar->addWidget(category_);
    bar->addWidget(search_, 1);
    bar->addWidget(follow_);
    bar->addWidget(clear);
    bar->addWidget(save);
    layout->addLayout(bar);
    text_ = new StripedLogView;
    text_->setObjectName("eventLogText");
    text_->setReadOnly(true);
    text_->setMaximumBlockCount(3000);
    text_->setLineWrapMode(QPlainTextEdit::NoWrap);
    layout->addWidget(text_);
    connect(category_, &QComboBox::currentTextChanged, this, [this] { rebuild(); });
    auto debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(180);
    connect(search_, &QLineEdit::textChanged, debounce, [debounce] { debounce->start(); });
    connect(debounce, &QTimer::timeout, this, &EventLog::rebuild);
    connect(clear, &QPushButton::clicked, this, [this] {
        entries_.clear();
        text_->clear();
    });
    connect(save, &QPushButton::clicked, this, [this] {
        auto path =
            QFileDialog::getSaveFileName(this, "导出当前筛选日志", "IEC61850-log.csv", "CSV 文件 (*.csv)");
        if (!path.isEmpty() && !exportCsv(path))
            QMessageBox::warning(this, "导出失败", "无法写入文件，请检查路径或权限。");
    });
}
QString EventLog::format(const QVariantMap& e) const {
    QString message = e["message"].toString();
    if (e["category"] == "属性变化")
        message = e["attribute"].toString() + "  " + e["before"].toString() + " → " + e["after"].toString() +
                  "  |  设备时标: " + e["deviceTime"].toString();
    message.replace('\r', "\\r");
    message.replace('\n', "\\n");
    return e["time"].toString() + "  [" + e["category"].toString() + "]  " + message;
}
bool EventLog::matches(const QVariantMap& e) const {
    return (category_->currentIndex() == 0 || category_->currentText() == e["category"].toString()) &&
           format(e).contains(search_->text().trimmed(), Qt::CaseInsensitive);
}
void EventLog::insert(QVariantMap e) {
    e["time"] = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz t");
    for (auto it = e.begin(); it != e.end(); ++it)
        if (it.value().toString().size() > 8192)
            it.value() = it.value().toString().left(8192) + "…[截断]";
    entries_ << e;
    if (entries_.size() > 3000) {
        entries_.remove(0, 500);
        rebuild();
        return;
    }
    if (!matches(e))
        return;
    int position = text_->verticalScrollBar()->value();
    text_->appendPlainText(format(e));
    if (follow_->isChecked())
        text_->verticalScrollBar()->setValue(text_->verticalScrollBar()->maximum());
    else
        text_->verticalScrollBar()->setValue(position);
}
void EventLog::append(QString category, QString message) {
    insert({{"category", category}, {"message", message}});
}
void EventLog::change(QString reference, QString attribute, QString before, QString after,
                      QString deviceTime) {
    insert({{"category", "属性变化"},
            {"reference", reference},
            {"attribute", attribute},
            {"before", before},
            {"after", after},
            {"deviceTime", deviceTime}});
}
QString EventLog::visibleText() const {
    return text_->toPlainText();
}
void EventLog::rebuild() {
    int position = text_->verticalScrollBar()->value();
    QStringList lines;
    for (const auto& e : entries_)
        if (matches(e))
            lines << format(e);
    text_->setPlainText(lines.join('\n'));
    text_->verticalScrollBar()->setValue(follow_->isChecked() ? text_->verticalScrollBar()->maximum()
                                                              : position);
}
bool EventLog::exportCsv(const QString& path) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    const QStringList fields{"time",   "category", "reference",  "attribute",
                             "before", "after",    "deviceTime", "message"};
    QByteArray csv("\xef\xbb\xbf");
    csv += "记录时间,类别,对象引用,属性,旧值,新值,设备时标,消息\r\n";
    for (const auto& e : entries_)
        if (matches(e)) {
            QStringList cells;
            for (const auto& field : fields) {
                QString value = e[field].toString();
                // Spreadsheet viewers must treat captured strings as data, never formulas.
                if (!value.isEmpty() && QString("=+-@\t\r").contains(value.front()))
                    value.prepend('\'');
                value.replace('"', "\"\"");
                cells << '"' + value + '"';
            }
            csv += (cells.join(',') + "\r\n").toUtf8();
        }
    return file.write(csv) == csv.size() && file.commit();
}
