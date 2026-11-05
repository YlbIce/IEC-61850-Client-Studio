#include "control_dialog.h"
#include "session.h"
#include <QtWidgets>
ControlDialog::ControlDialog(QVariantMap data, QWidget* parent) : QDialog(parent) {
    auto dialog = this;
    reference_ = data["ref"].toString();
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(true);
    dialog->setWindowTitle("遥控 — " + data["ref"].toString());
    auto form = new QFormLayout(dialog);
    auto value = new QLineEdit("0");
    const auto models = QStringList{"状态只读", "直接控制 / 普通安全", "先选择后执行 / 普通安全",
                                    "直接控制 / 增强安全", "先选择后执行 / 增强安全"};
    form->addRow("服务端控制模型", new QLabel(models.value(data["model"].toInt(), "未知")));
    const QMap<int, QString> types{{MMS_BOOLEAN, "布尔（0 / 1）"},
                                   {MMS_INTEGER, "有符号整数"},
                                   {MMS_UNSIGNED, "无符号整数"},
                                   {MMS_FLOAT, "浮点数"}};
    form->addRow("控制值类型", new QLabel(types.value(data["type"].toInt(), "暂不支持的类型")));
    form->addRow("控制值", value);
    auto test = new QCheckBox("Test");
    auto interlock = new QCheckBox("联锁检查");
    auto synchro = new QCheckBox("同期检查");
    form->addRow(test);
    form->addRow(interlock);
    form->addRow(synchro);
    auto outcome = new QLabel("SBO 对象先选择，再执行；增强安全控制需等待命令终结事件。");
    outcome_ = outcome;
    outcome->setWordWrap(true);
    form->addRow(outcome);
    auto actions = new QHBoxLayout;
    form->addRow(actions);
    for (const auto& pair :
         QList<QPair<QString, QString>>{{"选择", "select"}, {"执行…", "operate"}, {"取消选择", "cancel"}}) {
        auto button = new QPushButton(pair.first);
        actions->addWidget(button);
        if (pair.second == "select")
            button->setEnabled(data["model"] == 2 || data["model"] == 4);
        connect(button, &QPushButton::clicked, dialog, [=] {
            if (pair.second == "operate" &&
                QMessageBox::question(dialog, "确认遥控",
                                      QString("对象：%1\n值：%2\n确认发送执行命令？")
                                          .arg(data["ref"].toString(), value->text())) != QMessageBox::Yes)
                return;
            emit commandRequested({{"ref", data["ref"]},
                                   {"action", pair.second},
                                   {"value", value->text()},
                                   {"test", test->isChecked()},
                                   {"interlock", interlock->isChecked()},
                                   {"synchro", synchro->isChecked()}});
        });
    }
}
void ControlDialog::handleCompleted(QString operation, QVariantMap result) {
    if (operation == "control" && result["ref"] == reference_ && result["action"] != "inspect")
        outcome_->setText("请求被接受；增强安全模式请查看命令终结事件。");
}
void ControlDialog::handleError(QString operation, QString message) {
    if (operation == "control")
        outcome_->setText(message);
}
