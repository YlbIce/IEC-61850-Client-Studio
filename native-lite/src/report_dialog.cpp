#include "report_dialog.h"
#include "session.h"
#include <QtWidgets>
namespace {
QSpinBox* number(QWidget* parent, int maximum, int value) {
    auto box = new QSpinBox(parent);
    box->setRange(0, maximum);
    box->setValue(value);
    return box;
}
QWidget* bits(const QStringList& labels, const QList<int>& masks, int value, QList<QCheckBox*>& boxes) {
    auto panel = new QWidget;
    auto layout = new QGridLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    for (int i = 0; i < labels.size(); ++i) {
        auto box = new QCheckBox(labels[i]);
        box->setChecked(value & masks[i]);
        box->setProperty("mask", masks[i]);
        boxes << box;
        layout->addWidget(box, i / 3, i % 3);
    }
    return panel;
}
int checkedMask(const QList<QCheckBox*>& boxes) {
    int result = 0;
    for (auto box : boxes)
        if (box->isChecked())
            result |= box->property("mask").toInt();
    return result;
}
} // namespace
ReportDialog::ReportDialog(QVariantMap data, const QStringList& datasets, QWidget* parent) : QDialog(parent) {
    auto dialog = this;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("报告参数 — " + data["ref"].toString());
    dialog->setMinimumWidth(680);
    dialog->setModal(true);
    auto form = new QFormLayout(dialog);
    auto rptId = new QLineEdit(data["rptId"].toString());
    auto dataset = new QComboBox;
    dataset->setEditable(true);
    dataset->addItems(datasets);
    dataset->setCurrentText(data["dataset"].toString());
    auto buf = number(dialog, INT_MAX, data["bufTm"].toInt());
    auto period = number(dialog, INT_MAX, data["intgPd"].toInt());
    auto enabled = new QCheckBox("启用报告");
    enabled->setChecked(data["enabled"].toBool());
    auto reserve = new QCheckBox("预留 URCB");
    reserve->setChecked(data["reserve"].toBool());
    reserve->setEnabled(!data["buffered"].toBool());
    auto resvTms = number(dialog, 32767, data["resvTms"].toInt());
    resvTms->setEnabled(data["hasResvTms"].toBool());
    auto gi = new QCheckBox("使能后触发一次总召 GI");
    auto purge = new QCheckBox("清空 BRCB 缓冲区（丢弃未发送报告）");
    purge->setEnabled(data["buffered"].toBool());
    form->addRow("RptID", rptId);
    form->addRow("数据集", dataset);
    form->addRow("BufTm (ms)", buf);
    form->addRow("IntgPd (ms)", period);
    QList<QCheckBox*> triggers, options;
    form->addRow("触发条件", bits({"数据变化", "质量变化", "数据更新", "完整性", "总召 GI"},
                                  {TRG_OPT_DATA_CHANGED, TRG_OPT_QUALITY_CHANGED, TRG_OPT_DATA_UPDATE,
                                   TRG_OPT_INTEGRITY, TRG_OPT_GI},
                                  data["trgOps"].toInt(), triggers));
    form->addRow("可选字段",
                 bits({"序号", "时标", "原因", "数据集", "数据引用", "缓冲溢出", "EntryID", "ConfRev"},
                      {RPT_OPT_SEQ_NUM, RPT_OPT_TIME_STAMP, RPT_OPT_REASON_FOR_INCLUSION, RPT_OPT_DATA_SET,
                       RPT_OPT_DATA_REFERENCE, RPT_OPT_BUFFER_OVERFLOW, RPT_OPT_ENTRY_ID, RPT_OPT_CONF_REV},
                      data["optFlds"].toInt(), options));
    form->addRow(reserve);
    form->addRow("ResvTms (s)", resvTms);
    form->addRow(enabled);
    form->addRow(gi);
    form->addRow(purge);
    form->addRow(new QLabel("应用顺序：停用 → 写参数 → 使能 → GI → 回读。写入失败时请重新读取实际参数。"));
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close);
    form->addRow(buttons);
    buttons->button(QDialogButtonBox::Apply)->setText("应用并回读");
    buttons->button(QDialogButtonBox::Close)->setText("关闭");
    buttons->button(QDialogButtonBox::Apply)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Close)->setAutoDefault(false);
    buttons->button(QDialogButtonBox::Apply)->setDefault(true);
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked, dialog, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, dialog, [=] {
        QVariantMap args = data;
        args["rptId"] = rptId->text();
        args["dataset"] = dataset->currentText();
        args["bufTm"] = buf->value();
        args["intgPd"] = period->value();
        args["trgOps"] = checkedMask(triggers);
        args["optFlds"] = checkedMask(options);
        args["enabled"] = enabled->isChecked();
        args["reserve"] = reserve->isChecked();
        args["resvTms"] = resvTms->value();
        args["gi"] = gi->isChecked();
        args["purge"] = purge->isChecked();
        emit applyRequested(args);
    });
}
