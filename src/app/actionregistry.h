#pragma once

#include <QHash>
#include <QString>
#include <functional>

// 全局动作表（命令模式）：快捷键、托盘菜单、第二实例的启动参数都通过动作 id 触发
class ActionRegistry {
public:
    void add(const QString& id, const QString& text, std::function<void()> run);
    bool trigger(const QString& id) const;
    QString text(const QString& id) const;

private:
    struct Action {
        QString text;
        std::function<void()> run;
    };
    QHash<QString, Action> m_actions;
};
