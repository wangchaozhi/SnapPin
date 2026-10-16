#pragma once

#include "appsettings.h"

#include <QDialog>
#include <QMap>

class ActionRegistry;
class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLineEdit;
class QSpinBox;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(const AppSettings& settings, const ActionRegistry& actions, const QString& historyDir,
                   QWidget* parent = nullptr);

    AppSettings settings() const;
    bool autostart() const;

private:
    QWidget* createGeneralPage(const QString& historyDir);
    QWidget* createHotkeyPage(const ActionRegistry& actions);

    AppSettings m_settings;
    QCheckBox* m_autostart = nullptr;
    QCheckBox* m_autoUpdate = nullptr;
    QComboBox* m_backend = nullptr;
    QCheckBox* m_history = nullptr;
    QSpinBox* m_historyMax = nullptr;
    QCheckBox* m_cursor = nullptr;
    QComboBox* m_iconPalette = nullptr;
    QLineEdit* m_pattern = nullptr;
    QComboBox* m_format = nullptr;
    QSpinBox* m_quality = nullptr;
    QLineEdit* m_quickDir = nullptr;
    QMap<QString, QKeySequenceEdit*> m_hotkeyEdits;
};
