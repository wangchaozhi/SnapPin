#include "editortoolbar.h"

#include "platform/platform.h"
#include "toolicons.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QToolButton>
#include <QUndoStack>

namespace {

constexpr qreal kWidths[] = {2, 4, 8}; // 粗细按钮循环的三档：细、中、粗

constexpr int kIconSize = 20;
constexpr int kButtonSize = 32;

QFrame* separator(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedSize(1, kIconSize);
    line->setStyleSheet(QStringLiteral("background:#dcdcdc;"));
    return line;
}

} // namespace

EditorToolbar::EditorToolbar(QUndoStack* undo, QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("EditorToolbar"));
    setCursor(Qt::ArrowCursor);
    setStyleSheet(QStringLiteral(
        "#EditorToolbar{background:#fbfbfb;border:1px solid #c6c6c6;border-radius:6px;}"
        "QToolButton{border:none;border-radius:5px;background:transparent;}"
        "QToolButton:hover{background:#ececec;}"
        "QToolButton:pressed{background:#e0e0e0;}"
        "QToolButton:checked{background:#e3eeff;}"));

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
    connect(m_colorButton, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_style.color, window(), tr("选择颜色"));
        if (!c.isValid())
            return;
        m_style.color = c;
        updateStyleButtons();
        emit styleChanged(m_style);
    });

    m_widthButton = addButton(QIcon(), QString());
    connect(m_widthButton, &QToolButton::clicked, this, [this] {
        // 在细 / 中 / 粗三档之间循环；滚轮调出的中间值切到下一个更粗的档
        qreal next = kWidths[0];
        for (const qreal w : kWidths) {
            if (w > m_style.width + 0.01) {
                next = w;
                break;
            }
        }
        m_style.width = next;
        updateStyleButtons();
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
    connect(addButton(toolIcon(ToolIcon::Cancel), tr("取消 (Esc)")), &QToolButton::clicked, this, &EditorToolbar::cancelRequested);
    // 主操作用主题色突出
    connect(addButton(toolIcon(ToolIcon::Done, true), tr("复制到剪贴板 (Ctrl+C / 双击 / 回车)")), &QToolButton::clicked, this,
            &EditorToolbar::copyRequested);

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

void EditorToolbar::updateStyleButtons()
{
    m_colorButton->setIcon(colorIcon(m_style.color));
    m_widthButton->setIcon(widthIcon(m_style.width));
    m_widthButton->setToolTip(tr("粗细：%1（点击在细 / 中 / 粗之间切换，绘制时滚动鼠标滚轮微调）").arg(m_style.width));
}
