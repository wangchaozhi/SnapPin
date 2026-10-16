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
    void saveSecrets() const; // 点“确定”后把 API Key 写进系统钥匙串

private:
    QWidget* createGeneralPage(const QString& historyDir);
    QWidget* createHotkeyPage(const ActionRegistry& actions);
    QWidget* createTranslatePage();

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
    QComboBox* m_translateEngine = nullptr;
    QLineEdit* m_openAiUrl = nullptr;
    QLineEdit* m_openAiModel = nullptr;
    QLineEdit* m_openAiKey = nullptr;
    QString m_savedKey;
};
