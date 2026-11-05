#include "studio_style.h"
#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QStyleFactory>

static void initializeStudioResources() {
    Q_INIT_RESOURCE(studio_assets);
}
void StudioStyle::apply() {
    if (qApp->property("studioStyleApplied").toBool())
        return;
    qApp->setProperty("studioStyleApplied", true);
    initializeStudioResources();
    qApp->setStyle(QStyleFactory::create("Fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#f3f6fa"));
    palette.setColor(QPalette::WindowText, QColor("#26364b"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor("#f6f8fb"));
    palette.setColor(QPalette::Text, QColor("#26364b"));
    palette.setColor(QPalette::Button, QColor("#f5f7fa"));
    palette.setColor(QPalette::ButtonText, QColor("#26364b"));
    palette.setColor(QPalette::Highlight, QColor("#dceafb"));
    palette.setColor(QPalette::HighlightedText, QColor("#174d91"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#94a0b0"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#94a0b0"));
    qApp->setPalette(palette);
    qApp->setStyleSheet(QString::fromUtf8(R"STYLE(
QMainWindow { background: #eef2f7; }
QMenuBar { background: #202e42; color: #e5edf7; padding: 3px 8px; border: none; }
QMenuBar::item { padding: 5px 12px; background: transparent; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: #3b506c; border-radius: 3px; }
QMenu { background: #ffffff; color: #26364b; border: 1px solid #c6d2e1; padding: 5px; }
QMenu::item { padding: 7px 28px 7px 25px; border-radius: 3px; }
QMenu::item:selected { background: #e5effc; color: #174d91; }
QMenu::item:disabled { color: #9aa7b8; }
QMenu::separator { height: 1px; background: #e3e9f1; margin: 4px 10px; }
QToolBar { background: #293c55; border: none; border-bottom: 1px solid #425773; padding: 9px 12px; spacing: 7px; }
QToolBar::separator { background: #4a5c73; width: 1px; margin: 4px 8px; }
QToolBar QToolButton { color: #edf3fb; background: #334b69; border: 1px solid #536b88; border-radius: 4px; padding: 7px 11px; }
QToolBar QToolButton:hover { background: #426184; border-color: #90b6e2; }
QToolBar QToolButton:pressed { background: #1f3651; }
QToolBar QToolButton:disabled { color: #8ea0b5; border-color: #425771; background: #2d4059; }
QLabel#brandIcon { background: #4d8ee0; color: white; border-radius: 7px; font-size: 18px; font-weight: 700; }
QLabel#brandName { color: #f3f7fc; font-size: 12px; font-weight: 700; padding: 0 12px 0 8px; }
QLabel#connectionBadge { color: #d6e1ef; background: #35475d; border: 1px solid #576a81; border-radius: 10px; padding: 4px 10px; }
QLabel#connectionBadge[connected="true"] { color: #b7efda; background: #244f4b; border-color: #448478; }
QTabWidget::pane { background: #f3f6fa; border: 1px solid #d3dce8; top: -1px; }
QTabBar::tab { background: #e8edf4; color: #64758b; padding: 10px 25px; border: 1px solid #d3dce8; border-bottom: none; margin-right: 2px; }
QTabBar::tab:selected { background: #ffffff; color: #245c9f; border-top: 3px solid #4c87cf; padding-top: 8px; font-weight: 600; }
QTabBar::tab:hover:!selected { background: #dfe8f4; color: #2c527f; }
QWidget#sectionPanel { background: #ffffff; border: 1px solid #d6dfeb; }
QLabel#sectionTitle { background: #f6f8fb; color: #354c69; font-weight: 600; padding: 10px 12px; border: none; border-bottom: 1px solid #dce4ee; }
QTableView, QTreeView, QListView { background: #ffffff; alternate-background-color: #f6f8fb; border: 1px solid #d7dfeb; outline: none; selection-background-color: #dceafb; selection-color: #174d91; }
QTableView { gridline-color: #e5ebf2; }
QTableView::item { padding-left: 7px; padding-right: 7px; }
QTableView::item:selected, QTreeView::item:selected, QListView::item:selected { background: #dceafb; color: #174d91; }
QTreeView::item, QListView::item { min-height: 30px; padding: 2px 8px; }
QTreeView::item:hover:!selected, QListView::item:hover:!selected { background: #f0f5fc; }
QHeaderView { background: #edf2f8; }
QHeaderView::section { background: #edf2f8; color: #536983; padding: 9px 8px; font-weight: 600; border: none; border-right: 1px solid #dce4ee; border-bottom: 1px solid #d3ddea; }
QTableCornerButton::section { background: #edf2f8; border: none; border-bottom: 1px solid #d3ddea; }
QSplitter::handle { background: #eef2f7; }
QSplitter::handle:hover { background: #bad1ed; }
QDockWidget { color: #536983; font-weight: 600; }
QDockWidget::title { background: #e8eef6; padding: 8px 12px; border-top: 1px solid #d2dce9; }
QPlainTextEdit { background: #fbfcfe; color: #52657d; border: 1px solid #dce4ef; padding: 7px; selection-background-color: #dceafb; }
QStatusBar { background: #e8eef6; color: #56708e; border-top: 1px solid #d1dce9; padding: 3px 8px; }
QStatusBar::item { border: none; }
QDialog { background: #f6f8fb; }
QDialog QLabel { color: #425873; }
QLineEdit, QSpinBox, QComboBox { background: white; color: #26364b; border: 1px solid #bdcbdc; border-radius: 3px; padding: 6px 8px; min-height: 20px; selection-background-color: #dceafb; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border: 1px solid #4c87cf; }
QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { background: #edf1f6; color: #8d9aad; border-color: #d9e1eb; }
QSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; width: 20px; background: #e8eef6; border-left: 1px solid #bdcbdc; border-bottom: 1px solid #bdcbdc; }
QSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; width: 20px; background: #e8eef6; border-left: 1px solid #bdcbdc; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: #d6e6fa; }
QSpinBox::up-arrow, QSpinBox::down-arrow { width: 8px; height: 8px; }
QSpinBox::up-arrow { image: url(:/lite/up.xpm); }
QSpinBox::down-arrow { image: url(:/lite/down.xpm); }
QPushButton { background: #ffffff; color: #345779; border: 1px solid #b6c7dc; border-radius: 4px; padding: 7px 16px; min-height: 20px; }
QPushButton:hover { background: #eaf2fc; border-color: #72a0d6; }
QPushButton:pressed { background: #d6e6fa; }
QPushButton:default, QPushButton[primary="true"] { background: #3675c4; color: #ffffff; border-color: #2e69b4; }
QPushButton:default:hover, QPushButton[primary="true"]:hover { background: #2867b5; }
QPushButton:disabled { background: #eef2f7; color: #99a5b5; border-color: #d8e0eb; }
QCheckBox { spacing: 8px; padding: 4px 0; color: #425873; }
QToolTip { background: #263b55; color: #ffffff; border: 1px solid #58718f; padding: 6px; }
)STYLE"));
}

QIcon StudioStyle::icon(const QString& name) {
    QPixmap pixmap(40, 40);
    pixmap.fill(Qt::transparent);
    pixmap.setDevicePixelRatio(2);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(name == "connect"      ? QColor("#8be0bd")
                  : name == "disconnect" ? QColor("#f0aaaa")
                                         : QColor("#d5e6fb"),
                  1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (name == "connect") {
        p.drawRoundedRect(QRectF(3, 3, 14, 9), 1, 1);
        p.drawLine(10, 12, 10, 16);
        p.drawLine(5, 16, 15, 16);
    } else if (name == "disconnect") {
        p.drawRoundedRect(QRectF(4, 4, 12, 12), 1, 1);
        p.drawLine(7, 7, 13, 13);
        p.drawLine(13, 7, 7, 13);
    } else {
        p.drawArc(QRectF(4, 4, 12, 12), 35 * 16, 285 * 16);
        p.drawLine(16, 3, 16, 8);
        p.drawLine(16, 8, 11, 7);
    }
    return QIcon(pixmap);
}
