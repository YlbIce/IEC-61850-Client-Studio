#include "striped_log_view.h"
#include <QPainter>
#include <QTextBlock>

StripedLogView::StripedLogView(QWidget* parent) : QPlainTextEdit(parent) {}
void StripedLogView::paintEvent(QPaintEvent* event) {
    QList<QTextEdit::ExtraSelection> stripes;
    auto block = firstVisibleBlock();
    while (block.isValid()) {
        const auto rect = blockBoundingGeometry(block).translated(contentOffset());
        if (rect.top() > viewport()->height())
            break;
        QTextEdit::ExtraSelection stripe;
        stripe.cursor = QTextCursor(block);
        stripe.format.setBackground(QColor(block.blockNumber() % 2 ? "#e7eff8" : "#ffffff"));
        stripe.format.setProperty(QTextFormat::FullWidthSelection, true);
        stripes.append(stripe);
        block = block.next();
    }
    const auto previous = extraSelections();
    bool changed = previous.size() != stripes.size();
    for (int i = 0; !changed && i < stripes.size(); ++i) {
        changed = previous[i].cursor != stripes[i].cursor || previous[i].format != stripes[i].format;
    }
    if (changed)
        setExtraSelections(stripes);
    QPlainTextEdit::paintEvent(event);
}
