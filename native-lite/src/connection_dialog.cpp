#include "connection_dialog.h"
#include "connection_options.h"
#include <QtWidgets>
#include <exception>

ConnectionDialog::ConnectionDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle("连接 IED");
    setMinimumWidth(570);
    auto form = new QFormLayout(this);
    QSettings settings;
    host_ = new QLineEdit(settings.value("connection/host", "127.0.0.1").toString());
    port_ = new QSpinBox;
    port_->setRange(1, 65535);
    port_->setValue(settings.value("connection/port", 102).toInt());
    form->addRow("IP / 主机名", host_);
    form->addRow("MMS 端口", port_);
    mode_ = new QComboBox;
    mode_->setObjectName("connectionAddressMode");
    mode_->addItem("标准参数（与旧版相同）", "default");
    mode_->addItem("兼容尝试：不发送 AP-title / AE-qualifier", "omitApTitle");
    mode_->addItem("自定义 AP-title / AE-qualifier", "custom");
    mode_->setCurrentIndex(qMax(0, mode_->findData(settings.value("connection/addressMode", "default"))));
    form->addRow("连接模式", mode_);
    connectTimeout_ = new QSpinBox;
    requestTimeout_ = new QSpinBox;
    for (auto pair : {qMakePair(QString("connectTimeout"), connectTimeout_),
                      qMakePair(QString("requestTimeout"), requestTimeout_)}) {
        pair.second->setRange(1000, 60000);
        pair.second->setSingleStep(1000);
        pair.second->setSuffix(" ms");
        pair.second->setValue(settings.value("connection/" + pair.first, 10000).toInt());
    }
    form->addRow("连接超时", connectTimeout_);
    form->addRow("请求超时", requestTimeout_);
    auto advanced = new QGroupBox("高级地址（须与设备配置或成功握手一致）");
    auto grid = new QGridLayout(advanced);
    grid->addWidget(new QLabel("本机 Calling"), 0, 1);
    grid->addWidget(new QLabel("设备 Called"), 0, 2);
    const QStringList types{"T", "S", "P"};
    for (int i = 0; i < types.size(); ++i) {
        grid->addWidget(new QLabel(types[i] + " Selector"), i + 1, 0);
        for (int side = 0; side < 2; ++side) {
            QString key = (side ? "remote" : "local") + types[i];
            auto edit =
                new QLineEdit(settings.value("connection/" + key, i == 2 ? "00000001" : "0001").toString());
            edit->setToolTip(i == 0 ? "十六进制，最多 4 字节；留空不发送"
                                    : "十六进制，最多 16 字节；留空不发送");
            selectors_.insert(key, edit);
            grid->addWidget(edit, i + 1, side + 1);
        }
    }
    for (int side = 0; side < 2; ++side) {
        QString prefix = side ? "remote" : "local";
        for (int field = 0; field < 2; ++field) {
            QString key = prefix + (field ? "AeQualifier" : "ApTitle");
            QString fallback = field ? "12" : (side ? "1.1.1.999.1" : "1.1.1.999");
            auto edit = new QLineEdit(settings.value("connection/" + key, fallback).toString());
            edit->setEnabled(mode_->currentData() == "custom");
            connect(mode_, QOverload<int>::of(&QComboBox::currentIndexChanged), edit,
                    [this, edit] { edit->setEnabled(mode_->currentData() == "custom"); });
            selectors_.insert(key, edit);
            grid->addWidget(edit, field + 4, side + 1);
        }
    }
    grid->addWidget(new QLabel("AP-title"), 4, 0);
    grid->addWidget(new QLabel("AE-qualifier"), 5, 0);
    form->addRow(advanced);
    auto hint = new QLabel("兼容模式是可选诊断尝试，不代表 IEDScout 的实际参数。错误 5 "
                           "不能单独区分网络超时与设备握手拒绝；请结合成功/失败握手报文分析。");
    hint->setWordWrap(true);
    form->addRow(hint);
    auto error = new QLabel;
    error->setWordWrap(true);
    error->setStyleSheet("color:#b13636");
    form->addRow(error);
    auto importProfile = new QPushButton("导入连接配置…");
    form->addRow(importProfile);
    connect(importProfile, &QPushButton::clicked, this, [this, error] {
        auto path = QFileDialog::getOpenFileName(
            this, "导入连接配置", QCoreApplication::applicationDirPath() + "/profiles", "连接配置 (*.json)");
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) {
            error->setText("无法读取配置，或文件超过 64 KB");
            return;
        }
        QJsonParseError parse;
        auto document = QJsonDocument::fromJson(file.readAll(), &parse);
        if (parse.error != QJsonParseError::NoError || !document.isObject()) {
            error->setText("连接配置必须是有效 JSON 对象");
            return;
        }
        auto args = document.object().toVariantMap();
        try {
            ConnectionOptions::validate(args);
        } catch (const std::exception& e) {
            error->setText(QString::fromUtf8(e.what()));
            return;
        }
        host_->setText(args["host"].toString());
        port_->setValue(args["port"].toInt());
        mode_->setCurrentIndex(mode_->findData(args.value("addressMode", "default")));
        connectTimeout_->setValue(args.value("connectTimeout", 10000).toInt());
        requestTimeout_->setValue(args.value("requestTimeout", 10000).toInt());
        for (auto it = selectors_.begin(); it != selectors_.end(); ++it) {
            QString fallback = it.key().endsWith("ApTitle")
                                   ? (it.key().startsWith("local") ? "1.1.1.999" : "1.1.1.999.1")
                               : it.key().endsWith("AeQualifier") ? "12"
                               : it.key().endsWith('P')           ? "00000001"
                                                                  : "0001";
            it.value()->setText(args.value(it.key(), fallback).toString());
        }
        error->setText("已载入配置，请核对目标设备后点击连接。");
    });
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                        QDialogButtonBox::RestoreDefaults);
    buttons->button(QDialogButtonBox::Ok)->setText("连接");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    buttons->button(QDialogButtonBox::RestoreDefaults)->setText("恢复协议默认值");
    form->addRow(buttons);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this, [this] {
        mode_->setCurrentIndex(0);
        connectTimeout_->setValue(10000);
        requestTimeout_->setValue(10000);
        for (auto it = selectors_.begin(); it != selectors_.end(); ++it)
            it.value()->setText(it.key().endsWith("ApTitle")
                                    ? (it.key().startsWith("local") ? "1.1.1.999" : "1.1.1.999.1")
                                : it.key().endsWith("AeQualifier") ? "12"
                                : it.key().endsWith('P')           ? "00000001"
                                                                   : "0001");
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, this, [this, error] {
        auto args = arguments();
        try {
            ConnectionOptions::validate(args);
        } catch (const std::exception& e) {
            error->setText(QString::fromUtf8(e.what()));
            return;
        }
        QSettings settings;
        for (auto it = args.cbegin(); it != args.cend(); ++it)
            settings.setValue("connection/" + it.key(), it.value());
        accept();
    });
}
QVariantMap ConnectionDialog::arguments() const {
    QVariantMap args{{"host", host_->text().trimmed()},
                     {"port", port_->value()},
                     {"addressMode", mode_->currentData()},
                     {"connectTimeout", connectTimeout_->value()},
                     {"requestTimeout", requestTimeout_->value()}};
    for (auto it = selectors_.cbegin(); it != selectors_.cend(); ++it)
        args[it.key()] = it.value()->text().trimmed();
    return args;
}
