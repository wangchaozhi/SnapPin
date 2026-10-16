#pragma once

#include "annotation/tools.h"

#include <QFrame>
#include <QHash>

class ColorPalette;
class QToolButton;
class QUndoStack;

class EditorToolbar : public QFrame {
    Q_OBJECT
public:
    EditorToolbar(QUndoStack* undo, QWidget* parent);

    void setCurrentTool(ToolType type);
    Style style() const { return m_style; }
    void setStyle(const Style& style); // 外部改动（滚轮调粗细、选中图形）同步到按钮，不发出 styleChanged

signals:
    void toolClicked(ToolType type); // 再次点击当前工具表示取消选择
    void styleChanged(const Style& style);
    void pinRequested();
    void longCaptureRequested();
    void saveRequested();
    void copyRequested();

protected:
    void hideEvent(QHideEvent* e) override;

private:
    QToolButton* addButton(const QIcon& icon, const QString& tip);
    void updateStyleButtons();

    QHash<ToolType, QToolButton*> m_toolButtons;
    QToolButton* m_colorButton = nullptr;
    QToolButton* m_widthButton = nullptr;
    QToolButton* m_fillButton = nullptr;
    ColorPalette* m_palette = nullptr;
    Style m_style;
};
