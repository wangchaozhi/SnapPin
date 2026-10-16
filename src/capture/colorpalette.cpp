#include "colorpalette.h"

#include "toolicons.h"

#include <QEvent>
#include <QGridLayout>
#include <QGuiApplication>
#include <QMouseEvent>
#include <QPushButton>
#include <QStyleHints>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

const QRgb kPresets[] = {
    0xFF3B30, 0xFF9500, 0xFFCC00, 0x34C759, 0x32ADE6, 0x007AFF,
    0xAF52DE, 0xFF2D55, 0xA2845E, 0x000000, 0x8E8E93, 0xFFFFFF,
};
constexpr int kColumns = 6;

} // namespace

ColorPalette::ColorPalette(QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("ColorPalette"));
    setCursor(Qt::ArrowCursor);
    applyTheme();
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &ColorPalette::applyTheme);
    hide();
    // 点击遮罩的其他地方时收起
    parent->installEventFilter(this);
}

void ColorPalette::applyTheme()
{
    // 和工具栏一样跟随系统深浅色主题
    if (toolbarDarkMode()) {
        setStyleSheet(QStringLiteral(
            "#ColorPalette{background:#2b2d31;border:1px solid #3d4047;border-radius:6px;}"
            "QToolButton{border:none;border-radius:5px;background:transparent;}"
            "QToolButton:hover{background:#3a3d44;}"
            "QToolButton:checked{background:#4a4d55;}"
            "QPushButton{border:none;border-radius:4px;padding:4px 8px;color:#e4e4e7;text-align:left;}"
            "QPushButton:hover{background:#3a3d44;}"));
    } else {
        setStyleSheet(QStringLiteral(
            "#ColorPalette{background:#fbfbfb;border:1px solid #c6c6c6;border-radius:6px;}"
            "QToolButton{border:none;border-radius:5px;background:transparent;}"
            "QToolButton:hover{background:#ececec;}"
            "QToolButton:checked{background:#eceaf7;}"
            "QPushButton{border:none;border-radius:4px;padding:4px 8px;color:#333;text-align:left;}"
            "QPushButton:hover{background:#ececec;}"));
    }
    update();
}

void ColorPalette::setHost(QWidget* host)
{
    if (host == parentWidget())
        return;
    hide();
    parentWidget()->removeEventFilter(this);
    setParent(host);
    host->installEventFilter(this);
}

void ColorPalette::setCurrent(const QColor& color)
{
    m_current = color;
}

void ColorPalette::rebuild()
{
    qDeleteAll(findChildren<QWidget*>(Qt::FindDirectChildrenOnly));
    delete layout();

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(6, 6, 6, 6);
    outer->setSpacing(4);
    auto* grid = new QGridLayout;
    grid->setSpacing(2);
    int i = 0;
    for (const QRgb rgb : kPresets) {
        const QColor c = QColor::fromRgb(rgb);
        auto* b = new QToolButton(this);
        b->setIcon(colorIcon(c));
        b->setIconSize(QSize(20, 20));
        b->setFixedSize(30, 30);
        b->setCheckable(true);
        b->setChecked(c.rgb() == m_current.rgb());
        b->setToolTip(c.name().toUpper());
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QToolButton::clicked, this, [this, c] {
            hide();
            emit colorPicked(c);
        });
        grid->addWidget(b, i / kColumns, i % kColumns);
        ++i;
    }
    outer->addLayout(grid);

    auto* more = new QPushButton(tr("更多颜色…"), this);
    more->setFocusPolicy(Qt::NoFocus);
    connect(more, &QPushButton::clicked, this, [this] {
        hide();
        emit moreRequested();
    });
    outer->addWidget(more);
}

void ColorPalette::popup(const QRect& anchor)
{
    rebuild();
    adjustSize();
    const QWidget* host = parentWidget();
    const int gap = 4;
    int x = qBound(0, anchor.left(), qMax(0, host->width() - width()));
    int y = anchor.bottom() + 1 + gap;
    if (y + height() > host->height())
        y = anchor.top() - height() - gap;
    move(x, qMax(0, y));
    show();
    raise();
}

bool ColorPalette::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == parentWidget() && e->type() == QEvent::MouseButtonPress && isVisible()) {
        const auto* me = static_cast<QMouseEvent*>(e);
        if (!geometry().contains(me->position().toPoint()))
            hide();
    }
    return QFrame::eventFilter(obj, e);
}
