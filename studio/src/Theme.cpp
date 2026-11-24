#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPixmap>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QStyleHints>

namespace Theme
{

    namespace
    {

        QPalette darkPalette()
        {
            QPalette p;
            auto set = [&](QPalette::ColorRole role, const QColor &c) { p.setColor(QPalette::All, role, c); };
            set(QPalette::Window, window);
            set(QPalette::WindowText, text);
            set(QPalette::Base, editor);
            set(QPalette::AlternateBase, panel);
            set(QPalette::ToolTipBase, raised);
            set(QPalette::ToolTipText, text);
            set(QPalette::PlaceholderText, textFaint);
            set(QPalette::Text, text);
            set(QPalette::Button, raised);
            set(QPalette::ButtonText, text);
            set(QPalette::BrightText, error);
            set(QPalette::Link, accent);
            set(QPalette::LinkVisited, synKeyword);
            set(QPalette::Highlight, selection);
            set(QPalette::HighlightedText, text);
            set(QPalette::Accent, accent);
            set(QPalette::Light, hover);
            set(QPalette::Midlight, raised);
            set(QPalette::Mid, border);
            set(QPalette::Dark, window);
            set(QPalette::Shadow, QColor(0x0a0b0d));

            for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
                p.setColor(QPalette::Disabled, role, textFaint);
            p.setColor(QPalette::Disabled, QPalette::Button, panel);
            p.setColor(QPalette::Disabled, QPalette::Base, window);
            p.setColor(QPalette::Inactive, QPalette::Highlight, selection);
            return p;
        }

        QString writeIcons()
        {
            QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/theme";
            QDir().mkpath(dir);

            auto draw = [&](const QString &name, const QColor &color, auto &&paint) {
                for (int scale : {1, 2})
                {
                    QPixmap pm(16 * scale, 16 * scale);
                    pm.fill(Qt::transparent);
                    QPainter p(&pm);
                    p.setRenderHint(QPainter::Antialiasing);
                    p.scale(scale, scale);
                    p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    p.setBrush(Qt::NoBrush);
                    paint(p);
                    p.end();
                    pm.save(dir + "/" + name + (scale == 2 ? "@2x.png" : ".png"));
                }
            };
            auto chevron = [](QPainter &p, QPointF a, QPointF b, QPointF c) {
                QPainterPath path(a);
                path.lineTo(b);
                path.lineTo(c);
                p.drawPath(path);
            };

            draw("arrow-down", textDim, [&](QPainter &p) { chevron(p, {4, 6}, {8, 10}, {12, 6}); });
            draw("arrow-up", textDim, [&](QPainter &p) { chevron(p, {4, 10}, {8, 6}, {12, 10}); });
            draw("arrow-down-disabled", textFaint, [&](QPainter &p) { chevron(p, {4, 6}, {8, 10}, {12, 6}); });
            draw("arrow-up-disabled", textFaint, [&](QPainter &p) { chevron(p, {4, 10}, {8, 6}, {12, 10}); });
            draw("close", textDim, [](QPainter &p) {
                p.drawLine(QPointF(5, 5), QPointF(11, 11));
                p.drawLine(QPointF(11, 5), QPointF(5, 11));
            });
            draw("close-hover", text, [](QPainter &p) {
                p.drawLine(QPointF(5, 5), QPointF(11, 11));
                p.drawLine(QPointF(11, 5), QPointF(5, 11));
            });
            draw("check", accent, [&](QPainter &p) {
                p.setPen(QPen(accent, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                chevron(p, {3.5, 8.5}, {6.5, 11.5}, {12.5, 4.5});
            });
            return dir;
        }

        QString styleSheet()
        {
            QString qss = R"(
QMainWindow, QDialog { background: @window; }
QToolTip { background: @raised; color: @text; border: 1px solid @border; padding: 4px 6px; }

QMenuBar { background: @panel; color: @text; border-bottom: 1px solid @border; }
QMenuBar::item { padding: 5px 10px; background: transparent; }
QMenuBar::item:selected { background: @hover; border-radius: 4px; }
QMenu { background: @panel; color: @text; border: 1px solid @border; padding: 4px; }
QMenu::item { padding: 6px 28px 6px 20px; border-radius: 4px; }
QMenu::item:selected { background: @selection; }
QMenu::item:disabled { color: @faint; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }

QToolBar { background: @panel; border: none; border-bottom: 1px solid @border; spacing: 4px; padding: 4px; }
QToolBar::separator { width: 1px; background: @border; margin: 4px 6px; }
QToolButton { background: transparent; color: @text; border: 1px solid transparent; border-radius: 5px; padding: 5px 10px; }
QToolButton:hover { background: @hover; border-color: @border; }
QToolButton:pressed { background: @selection; }
QToolButton:disabled { color: @faint; }

QDockWidget { color: @dim; titlebar-close-icon: none; }
QDockWidget::title { background: @panel; padding: 6px 8px; border-bottom: 1px solid @border; text-align: left; }

QTabWidget::pane { border: none; background: @editor; }
QTabBar { background: @panel; }
QTabBar::tab { background: @panel; color: @dim; padding: 7px 14px; border: none; border-right: 1px solid @border; border-top: 2px solid transparent; }
QTabBar::tab:selected { background: @editor; color: @text; border-top: 2px solid @accent; }
QTabBar::tab:hover:!selected { background: @hover; color: @text; }

QStatusBar { background: @panel; color: @dim; border-top: 1px solid @border; }
QStatusBar QLabel { color: @dim; padding: 0 8px; }

QPlainTextEdit, QTextEdit, QListWidget, QTreeView, QListView { background: @editor; color: @text; border: none; selection-background-color: @selection; }
QTreeView::item, QListWidget::item { padding: 3px 4px; }
QTreeView::item:hover, QListWidget::item:hover { background: @currentLine; }
QTreeView::item:selected, QListWidget::item:selected { background: @selection; color: @text; }
QListWidget { border: 1px solid @border; border-radius: 6px; padding: 4px; }
QHeaderView::section { background: @panel; color: @dim; border: none; padding: 4px; }

QLineEdit, QSpinBox, QComboBox, QFontComboBox { background: @raised; color: @text; border: 1px solid @border; border-radius: 5px; padding: 5px 8px; selection-background-color: @selection; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: @accent; }
QLineEdit:disabled { color: @faint; background: @panel; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: @panel; color: @text; border: 1px solid @border; selection-background-color: @selection; outline: none; }
QSpinBox::up-button, QSpinBox::down-button { background: @raised; border: none; width: 18px; }

QPushButton { background: @raised; color: @text; border: 1px solid @border; border-radius: 5px; padding: 6px 16px; }
QPushButton:hover { background: @hover; }
QPushButton:pressed { background: @selection; }
QPushButton:default, QPushButton#primary { background: @accent; color: #0d1020; border-color: @accent; font-weight: bold; }
QPushButton:default:hover, QPushButton#primary:hover { background: #8aa3ff; }
QPushButton:disabled { color: @faint; background: @panel; }

QGroupBox { color: @dim; border: 1px solid @border; border-radius: 6px; margin-top: 14px; padding: 12px 8px 8px 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
QLabel { color: @text; background: transparent; }

QScrollBar:vertical { background: @editor; width: 11px; margin: 0; }
QScrollBar:horizontal { background: @editor; height: 11px; margin: 0; }
QScrollBar::handle { background: @hover; border-radius: 4px; margin: 2px; }
QScrollBar::handle:hover { background: #3c404a; }
QScrollBar::handle:vertical { min-height: 28px; }
QScrollBar::handle:horizontal { min-width: 28px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
QAbstractScrollArea::corner { background: @editor; }

QComboBox::down-arrow { image: url(@icons/arrow-down.png); width: 12px; height: 12px; }
QComboBox::down-arrow:disabled { image: url(@icons/arrow-down-disabled.png); }
QSpinBox { padding-right: 20px; }
QSpinBox::up-button { subcontrol-origin: border; subcontrol-position: top right; }
QSpinBox::down-button { subcontrol-origin: border; subcontrol-position: bottom right; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover { background: @hover; }
QSpinBox::up-arrow { image: url(@icons/arrow-up.png); width: 10px; height: 10px; }
QSpinBox::down-arrow { image: url(@icons/arrow-down.png); width: 10px; height: 10px; }
QSpinBox::up-arrow:disabled { image: url(@icons/arrow-up-disabled.png); }
QSpinBox::down-arrow:disabled { image: url(@icons/arrow-down-disabled.png); }

QTabBar::close-button { image: url(@icons/close.png); subcontrol-position: right; border-radius: 3px; margin: 2px; }
QTabBar::close-button:hover { image: url(@icons/close-hover.png); background: @hover; }

QMenu::indicator { width: 14px; height: 14px; left: 4px; }
QMenu::indicator:checked { image: url(@icons/check.png); }
QCheckBox { color: @text; spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator { width: 14px; height: 14px; background: @raised; border: 1px solid @border; border-radius: 3px; }
QCheckBox::indicator:checked { image: url(@icons/check.png); border-color: @accent; }
QProgressBar { background: @raised; color: @text; border: 1px solid @border; border-radius: 4px; text-align: center; }
QProgressBar::chunk { background: @accent; border-radius: 3px; }

QSplitter::handle, QMainWindow::separator { background: @border; width: 1px; height: 1px; }
QMessageBox, QInputDialog, QFileDialog { background: @window; }
)";
            const std::pair<const char *, QColor> vars[] = {
                {"@window", window},   {"@editor", editor},       {"@panel", panel},
                {"@raised", raised},   {"@hover", hover},         {"@border", border},
                {"@currentLine", currentLine}, {"@text", text},   {"@dim", textDim},
                {"@faint", textFaint}, {"@accent", accent},       {"@selection", selection},
            };
            for (const auto &[name, color] : vars)
                qss.replace(name, hex(color));
            qss.replace("@icons", writeIcons());
            return qss;
        }

    }

    void apply(QApplication &app)
    {
        QApplication::setStyle(QStyleFactory::create("Fusion"));
        app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
        QApplication::setPalette(darkPalette());
        app.setStyleSheet(styleSheet());
    }

}
