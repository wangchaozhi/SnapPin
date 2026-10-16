#pragma once

#include "shapes.h"

#include <QObject>
#include <QUndoStack>
#include <memory>
#include <vector>

// 标注数据模型：图形列表 + 撤销栈 + 当前选中的图形。渲染与交互由 OverlayWindow / ITool 负责
class AnnotationDoc : public QObject {
    Q_OBJECT
public:
    explicit AnnotationDoc(QObject* parent = nullptr);

    void add(std::unique_ptr<Shape> shape); // 以命令形式入栈，可撤销
    void remove(Shape* shape);
    // 图形已被实时拖动了 delta，这里只把这次移动记入撤销栈
    void commitMove(Shape* shape, const QPointF& delta);

    // 修改图形的颜色 / 粗细。连续修改同一个图形（如滚轮调粗细）合并为一步撤销
    void restyle(Shape* shape, const Style& style);
    int nextNumber() const; // 下一个序号标记的数字

    Shape* shapeAt(const QPointF& pos) const; // 最上层命中的图形
    Shape* selected() const { return m_selected; }
    void setSelected(Shape* shape);

    void paint(QPainter& p, const RenderContext& ctx) const;
    bool isEmpty() const { return m_shapes.empty(); }
    QUndoStack* undoStack() { return &m_undo; }

signals:
    void changed();
    void selectionChanged(Shape* shape);

private:
    friend class AddShapeCommand;
    friend class RemoveShapeCommand;
    friend class MoveShapeCommand;
    friend class RestyleCommand;

    std::shared_ptr<Shape> find(Shape* shape) const;
    void insertAt(size_t index, const std::shared_ptr<Shape>& shape);
    size_t erase(const std::shared_ptr<Shape>& shape); // 返回原位置

    std::vector<std::shared_ptr<Shape>> m_shapes;
    Shape* m_selected = nullptr;
    QUndoStack m_undo;
};
