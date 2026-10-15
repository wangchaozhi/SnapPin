#include "actionregistry.h"

void ActionRegistry::add(const QString& id, const QString& text, std::function<void()> run)
{
    m_actions.insert(id, {text, std::move(run)});
}

bool ActionRegistry::trigger(const QString& id) const
{
    const auto it = m_actions.constFind(id);
    if (it == m_actions.cend())
        return false;
    it->run();
    return true;
}

QString ActionRegistry::text(const QString& id) const
{
    return m_actions.value(id).text;
}
