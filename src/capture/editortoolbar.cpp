#include "editortoolbar.h"

#include "colorpalette.h"
#include "platform/platform.h"
#include "toolicons.h"

#include <QColorDialog>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QStyleHints>
#include <QToolButton>
#include <QUndoStack>

namespace {

constexpr int kIconSize = 20;
constexpr int kButtonSize = 32;

QFrame* separator(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::NoFrame);
    line->setObjectName(QStringLiteral("ToolbarSeparator"));
    line->setFixedSize(1, kIconSize);
    return line;
}

} // namespace

EditorToolbar::EditorToolbar(QUndoStack* undo, QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("EditorToolbar"));
    setCursor(Qt::ArrowCursor);
    applyTheme();
    // 跟随系统深浅色主题：换样式表，图标重绘时自己读取主题
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &EditorToolbar::applyTheme);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(5, 4, 5, 4);
    layout->setSpacing(2);

    const struct {
        ToolType type;
        ToolIcon icon;
        const char* tip;
    } tools[] = {
        {ToolType::Rect, ToolIcon::Rect, "矩形（Shift：正方形）"},
        {ToolType::Ellipse, ToolIcon::Ellipse, "椭圆（Shift：圆）"},
        {ToolType::Line, ToolIcon::Line, "直线（Shift：45° 吸附）"},
        {ToolType::Arrow, ToolIcon::Arrow, "箭头（Shift：45° 吸附）"},
        {ToolType::Pen, ToolIcon::Pen, "画笔"},
        {ToolType::Marker, ToolIcon::Marker, "荧光笔"},
        {ToolType::Mosaic, ToolIcon::Mosaic, "马赛克"},
        {ToolType::Eraser, ToolIcon::Eraser, "橡皮擦：擦掉标注，露出原图"},
        {ToolType::Text, ToolIcon::Text, "文字"},
        {ToolType::Number, ToolIcon::Number, "序号（拖动可拉出指示尾巴）"},
    };
    for (const auto& t : tools) {
        QToolButton* b = addButton(toolIcon(t.icon), QString::fromUtf8(t.tip));
        b->setCheckable(true);
        const ToolType type = t.type;
        connect(b, &QToolButton::clicked, this, [this, type] { emit toolClicked(type); });
        m_toolButtons.insert(type, b);
    }

    layout->addWidget(separator(this));

    m_colorButton = addButton(QIcon(), tr("颜色"));
    m_palette = new ColorPalette(parent);
    connect(m_colorButton, &QToolButton::clicked, this, [this] {
        if (m_palette->isVisible()) {
            m_palette->hide();
            return;
        }
        // 面板贴着工具栏弹出，左边与颜色按钮对齐
        const QPoint at = m_colorButton->mapTo(parentWidget(), QPoint(0, 0));
        m_palette->setCurrent(m_style.color);
        m_palette->popup(QRect(at.x(), geometry().top(), m_colorButton->width(), geometry().height()));
    });
    auto applyColor = [this](const QColor& c) {
        if (!c.isValid())
            return;
        m_style.color = c;
        updateStyleButtons();
        emit styleChanged(m_style);
    };
    connect(m_palette, &ColorPalette::colorPicked, this, applyColor);
    connect(m_palette, &ColorPalette::moreRequested, this, [this, applyColor] {
        applyColor(QColorDialog::getColor(m_style.color, window(), tr("选择颜色")));
    });

    m_fillButton = addButton(toolIcon(ToolIcon::Fill), tr("填充：矩形、椭圆画成实心，文字加底色"));
    m_fillButton->setCheckable(true);
    connect(m_fillButton, &QToolButton::toggled, this, [this](bool on) {
        if (m_style.fill == on)
            return;
        m_style.fill = on;
        emit styleChanged(m_style);
    });

    layout->addWidget(separator(this));

    QToolButton* undoBtn = addButton(toolIcon(ToolIcon::Undo), tr("撤销 (Ctrl+Z)"));
    QToolButton* redoBtn = addButton(toolIcon(ToolIcon::Redo), tr("重做 (Ctrl+Y)"));
    undoBtn->setEnabled(undo->canUndo());
    redoBtn->setEnabled(undo->canRedo());
    connect(undoBtn, &QToolButton::clicked, undo, &QUndoStack::undo);
    connect(redoBtn, &QToolButton::clicked, undo, &QUndoStack::redo);
    connect(undo, &QUndoStack::canUndoChanged, undoBtn, &QToolButton::setEnabled);
    connect(undo, &QUndoStack::canRedoChanged, redoBtn, &QToolButton::setEnabled);

    layout->addWidget(separator(this));

    if (scrollCaptureSupported()) {
        connect(addButton(toolIcon(ToolIcon::LongCapture), tr("长截图：滚动选区内容自动拼接 (Ctrl+L)")),
                &QToolButton::clicked, this, &EditorToolbar::longCaptureRequested);
    }
    connect(addButton(toolIcon(ToolIcon::Pin), tr("贴图 (Ctrl+T)")), &QToolButton::clicked, this, &EditorToolbar::pinRequested);
    connect(addButton(toolIcon(ToolIcon::Save), tr("保存 (Ctrl+S)")), &QToolButton::clicked, this, &EditorToolbar::saveRequested);
    // 主操作用应用图标的配色突出；取消用 Esc，不单独放按钮
    connect(addButton(toolIcon(ToolIcon::Copy), tr("复制到剪贴板 (Ctrl+C / 双击 / 回车)，Esc 取消")), &QToolButton::clicked,
            this, &EditorToolbar::copyRequested);

    updateStyleButtons();
    adjustSize();
}

QToolButton* EditorToolbar::addButton(const QIcon& icon, const QString& tip)
{
    auto* b = new QToolButton(this);
    b->setIcon(icon);
    b->setIconSize(QSize(kIconSize, kIconSize));
    b->setFixedSize(kButtonSize, kButtonSize);
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus); // 保持键盘焦点在遮罩上
    layout()->addWidget(b);
    return b;
}

void EditorToolbar::applyTheme()
{
    if (toolbarDarkMode()) {
        setStyleSheet(QStringLiteral(
            "#EditorToolbar{background:#2b2d31;border:1px solid #3d4047;border-radius:6px;}"
            "#ToolbarSeparator{background:#4a4d55;}"
            "QToolButton{border:none;border-radius:5px;background:transparent;}"
            "QToolButton:hover{background:#3a3d44;}"
            "QToolButton:pressed{background:#45484f;}"
            "QToolButton:checked{background:#4a4d55;}"));
    } else {
        setStyleSheet(QStringLiteral(
            "#EditorToolbar{background:#fbfbfb;border:1px solid #c6c6c6;border-radius:6px;}"
            "#ToolbarSeparator{background:#dcdcdc;}"
            "QToolButton{border:none;border-radius:5px;background:transparent;}"
            "QToolButton:hover{background:#ececec;}"
            "QToolButton:pressed{background:#e0e0e0;}"
            "QToolButton:checked{background:#eceaf7;}"));
    }
    update();
}

void EditorToolbar::setCurrentTool(ToolType type)
{
    for (auto it = m_toolButtons.cbegin(); it != m_toolButtons.cend(); ++it)
        it.value()->setChecked(it.key() == type);
}

void EditorToolbar::setStyle(const Style& style)
{
    m_style = style;
    updateStyleButtons();
}

void EditorToolbar::hideEvent(QHideEvent* e)
{
    m_palette->hide();
    QFrame::hideEvent(e);
}

void EditorToolbar::updateStyleButtons()
{
    const QSignalBlocker block(m_fillButton); // 同步显示，不当作用户操作
    m_fillButton->setChecked(m_style.fill);
    m_colorButton->setIcon(colorIcon(m_style.color));
}
