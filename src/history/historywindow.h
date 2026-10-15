#pragma once

#include <QWidget>

class HistoryStore;
class QListWidget;
class QListWidgetItem;

// 截图历史浏览：双击贴图，右键更多操作
class HistoryWindow : public QWidget {
    Q_OBJECT
public:
    explicit HistoryWindow(HistoryStore* store, QWidget* parent = nullptr);

signals:
    void pinRequested(const QImage& image);

private:
    void reload();
    QString currentPath() const;
    void pinCurrent();
    void copyCurrent();
    void saveCurrent();
    void deleteCurrent();
    void showMenu(const QPoint& pos);

    HistoryStore* m_store;
    QListWidget* m_list;
};
