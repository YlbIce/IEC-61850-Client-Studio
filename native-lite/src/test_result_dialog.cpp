#include "test_result_dialog.h"
#include <QtWidgets>
TestResultDialog::TestResultDialog(PointModel* model, const QModelIndex& index, QString initialStatus,
                                   QWidget* parent)
    : QDialog(parent) {
    QPersistentModelIndex source(index);
    if (!source.isValid())
        return;
    auto record = model->testRecord(source.row());
    auto dialog = this;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(true);
    dialog->setWindowTitle("测试结果与失败原因");
    dialog->resize(570, 320);
    auto layout = new QFormLayout(dialog);
    auto ref = new QLabel(model->reference(source.row()));
    ref->setWordWrap(true);
    layout->addRow("对象", ref);
    auto status = new QComboBox;
    status->setObjectName("testResultStatus");
    status->addItem("未测", "untested");
    status->addItem("已测成功", "success");
    status->addItem("已测失败", "failure");
    if (record["status"] == "tested")
        status->addItem("已测待确认（旧版记录）", "tested");
    status->setCurrentIndex(status->findData(initialStatus.isEmpty() ? record["status"] : initialStatus));
    auto reason = new QPlainTextEdit(record["reason"].toString());
    reason->setObjectName("testFailureReason");
    reason->setPlaceholderText("失败时必填，例如：遥测值与现场仪表不一致；最多 2000 字。");
    reason->setMaximumHeight(120);
    auto hint = new QLabel;
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #a43333;");
    connect(reason, &QPlainTextEdit::textChanged, hint, &QLabel::clear);
    layout->addRow("测试结果", status);
    layout->addRow("失败原因 / 备注", reason);
    layout->addRow(hint);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("保存结果");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, dialog, [=] {
        if (!source.isValid()) {
            hint->setText("数据集已切换，请关闭后重新选择对象。");
            return;
        }
        auto value = reason->toPlainText().trimmed();
        if (status->currentData() == "failure" && value.isEmpty()) {
            hint->setText("请填写失败原因后保存。");
            reason->setFocus();
            return;
        }
        if (value.size() > 2000) {
            hint->setText("失败原因 / 备注最多 2000 字。");
            return;
        }
        if (model->setTestResult(source.row(), status->currentData().toString(), value))
            dialog->accept();
        else
            hint->setText("保存失败，请查看下方错误日志。");
    });
}
