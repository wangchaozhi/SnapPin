#include "editortoolbar.h"

#include <QColorDialog>
#include <QHBoxLayout>
#include <QPainter>
#include <QToolButton>
#include <QUndoStack>

namespace {

struct WidthLevel {
    qreal width;
    const char* label;
};
constexpr WidthLevel kWidths[] = {{2, "细"}, {4, "中"}, {8, "粗"}};

QFrame* separator(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setFrameShape(QFrame::VLine);
    line->setFixedWidth(1);
    line->setStyleSheet(QStringLiteral("background:#d0d0d0;"));
    return line;
}

} // namespace

EditorToolbar::EditorToolbar(QUndoStack* undo, QWidget* parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("EditorToolbar"));
    setCursor(Qt::ArrowCursor);
    setStyleSheet(QStringLiteral(
        "#EditorToolbar{background:#fafafa;border:1px solid #b8b8b8;border-radius:4px;}"
        "QToolButton{border:none;border-radius:3px;min-width:28px;min-height:28px;font-size:15px;color:#333;}"
        "QToolButton:hover{background:#e6e6e6;}"
        "QToolButton:checked{background:#cfe8ff;}"
        "QToolButton:disabled{color:#bbb;}"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 3, 4, 3);
    layout->setSpacing(2);

    const struct {
        ToolType type;
        const char* text;
        const char* tip;
    } tools[] = {
        {ToolType::Rect, "▭", "矩形"},
        {ToolType::Ellipse, "◯", "椭圆"},
        {ToolType::Arrow, "➚", "箭头"},
        {ToolType::Pen, "✎", "画笔"},
        {ToolType::Marker, "▌", "荧光笔"},
        {ToolType::Mosaic, "▦", "马赛克"},
        {ToolType::Text, "T", "文字"},
    };
    for (const auto& t : tools) {
        QToolButton* b = addButton(QString::fromUtf8(t.text), QString::fromUtf8(t.tip));
        b->setCheckable(true);
        const ToolType type = t.type;
        connect(b, &QToolButton::clicked, this, [this, type] { emit toolClicked(type); });
        m_toolButtons.insert(type, b);
    }

    layout->addWidget(separator(this));

    m_colorButton = addButton(QString(), tr("颜色"));
    connect(m_colorButton, &QToolButton::clicked, this, [this] {
        const QColor c = QColorDialog::getColor(m_style.color, window(), tr("选择颜色"));
        if (!c.isValid())
            return;
        m_style.color = c;
        updateStyleButtons();
        emit styleChanged(m_style);
    });

    m_widthButton = addButton(QString(), tr("粗细（点击切换）"));
    connect(m_widthButton, &QToolButton::clicked, this, [this] {
        int idx = 0;
        for (int i = 0; i < int(std::size(kWidths)); ++i) {
            if (qFuzzyCompare(kWidths[i].width, m_style.width))
                idx = i;
        }
        m_style.width = kWidths[(idx + 1) % std::size(kWidths)].width;
        updateStyleButtons();
        emit styleChanged(m_style);
    });

    layout->addWidget(separator(this));

    QToolButton* undoBtn = addButton(QStringLiteral("↶"), tr("撤销 (Ctrl+Z)"));
    QToolButton* redoBtn = addButton(QStringLiteral("↷"), tr("重做 (Ctrl+Y)"));
    undoBtn->setEnabled(undo->canUndo());
    redoBtn->setEnabled(undo->canRedo());
    connect(undoBtn, &QToolButton::clicked, undo, &QUndoStack::undo);
    connect(redoBtn, &QToolButton::clicked, undo, &QUndoStack::redo);
    connect(undo, &QUndoStack::canUndoChanged, undoBtn, &QToolButton::setEnabled);
    connect(undo, &QUndoStack::canRedoChanged, redoBtn, &QToolButton::setEnabled);

    layout->addWidget(separator(this));

    connect(addButton(QStringLiteral("⇕"), tr("长截图：滚动选区内容自动拼接 (Ctrl+L)")), &QToolButton::clicked, this,
            &EditorToolbar::longCaptureRequested);
    connect(addButton(QStringLiteral("📌"), tr("贴图 (Ctrl+T)")), &QToolButton::clicked, this, &EditorToolbar::pinRequested);
    connect(addButton(QStringLiteral("💾"), tr("保存 (Ctrl+S)")), &QToolButton::clicked, this, &EditorToolbar::saveRequested);
    connect(addButton(QStringLiteral("✕"), tr("取消 (Esc)")), &QToolButton::clicked, this, &EditorToolbar::cancelRequested);
    connect(addButton(QStringLiteral("✓"), tr("复制到剪贴板 (Ctrl+C / 双击 / 回车)")), &QToolButton::clicked, this, &EditorToolbar::copyRequested);

    updateStyleButtons();
    adjustSize();
}

QToolButton* EditorToolbar::addButton(const QString& text, const QString& tip)
{
    auto* b = new QToolButton(this);
    b->setText(text);
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

void EditorToolbar::updateStyleButtons()
{
    QPixmap swatch(16, 16);
    swatch.fill(Qt::transparent);
    {
        QPainter p(&swatch);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(0, 0, 0, 90), 1));
        p.setBrush(m_style.color);
        p.drawRoundedRect(QRectF(0.5, 0.5, 15, 15), 3, 3);
    }
    m_colorButton->setIcon(QIcon(swatch));

    for (const auto& w : kWidths) {
        if (qFuzzyCompare(w.width, m_style.width))
            m_widthButton->setText(QString::fromUtf8(w.label));
    }
}
