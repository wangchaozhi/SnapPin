#pragma once

#include <QFileInfoList>
#include <QImage>
#include <QObject>

// 截图历史：每次截图保存为 PNG 到应用数据目录，超出上限时删除最旧的
class HistoryStore : public QObject {
    Q_OBJECT
public:
    explicit HistoryStore(QObject* parent = nullptr);

    void add(const QImage& image); // 后台线程编码写盘，完成后发出 changed()
    QFileInfoList entries() const; // 新的在前
    void remove(const QString& path);
    void clear();
    QString directory() const { return m_dir; }

    void setEnabled(bool enabled) { m_enabled = enabled; }
    void setMaxCount(int count);

signals:
    void changed();

private:
    void prune();

    QString m_dir;
    bool m_enabled = true;
    int m_maxCount = 50;
};
