#pragma once
#include <QPlainTextEdit>

// Only visible blocks receive selections, keeping repaint work independent of log size.
class StripedLogView : public QPlainTextEdit {
public:
    explicit StripedLogView(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* event) override;
};
