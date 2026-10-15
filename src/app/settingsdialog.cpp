#include "settingsdialog.h"

#include "actionregistry.h"
#include "platform/platform.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(const AppSettings& settings, const ActionRegistry& actions, const QString& historyDir,
                               QWidget* parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(tr("设置 - SnapPin"));
    setMinimumWidth(460);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(createGeneralPage(historyDir), tr("常规"));
    tabs->addTab(createHotkeyPage(actions), tr("快捷键"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
}

QWidget* SettingsDialog::createGeneralPage(const QString& historyDir)
{
    auto* page = new QWidget(this);
    auto* layout = new QVBoxLayout(page);

    auto* startup = new QGroupBox(tr("启动"), page);
    auto* startupLayout = new QVBoxLayout(startup);
    m_autostart = new QCheckBox(tr("开机时自动启动"), startup);
    m_autostart->setEnabled(autostartSupported());
    m_autostart->setChecked(autostartEnabled());
    startupLayout->addWidget(m_autostart);
    m_autoUpdate = new QCheckBox(tr("自动检查更新（当前版本 %1）").arg(QStringLiteral(APP_VERSION)), startup);
    m_autoUpdate->setChecked(m_settings.autoUpdate);
    startupLayout->addWidget(m_autoUpdate);
    layout->addWidget(startup);

    auto* capture = new QGroupBox(tr("截图"), page);
    auto* captureForm = new QFormLayout(capture);
    m_backend = new QComboBox(capture);
    m_backend->addItem(tr("自动（DXGI 优先，失败时用 GDI）"), QStringLiteral("auto"));
    m_backend->addItem(tr("GDI（兼容性最好）"), QStringLiteral("gdi"));
    m_backend->setCurrentIndex(qMax(0, m_backend->findData(m_settings.captureBackend)));
#ifdef Q_OS_WIN
    captureForm->addRow(tr("截屏方式："), m_backend);
    m_uia = new QCheckBox(tr("识别窗口内的界面元素（UI Automation）"), capture);
#else
    m_backend->hide(); // 其他平台只有一种截屏方式
#ifdef Q_OS_MACOS
    m_uia = new QCheckBox(tr("识别窗口内的界面元素（需要辅助功能权限）"), capture);
#else
    m_uia = new QCheckBox(tr("识别窗口内的界面元素（Linux 暂不支持）"), capture);
    m_uia->setEnabled(false);
#endif
#endif
    m_uia->setChecked(m_settings.uiAutomation);
    m_uia->setToolTip(tr("可选中按钮、输入框等控件。复杂程序的识别结果会稍晚出现"));
    captureForm->addRow(m_uia);
    layout->addWidget(capture);

    auto* history = new QGroupBox(tr("截图历史"), page);
    auto* historyForm = new QFormLayout(history);
    m_history = new QCheckBox(tr("保存每次截图到历史"), history);
    m_history->setChecked(m_settings.historyEnabled);
    historyForm->addRow(m_history);
    m_historyMax = new QSpinBox(history);
    m_historyMax->setRange(1, 1000);
    m_historyMax->setValue(m_settings.historyMax);
    m_historyMax->setSuffix(tr(" 张"));
    historyForm->addRow(tr("最多保留："), m_historyMax);
    auto* openDir = new QPushButton(tr("打开历史文件夹"), history);
    connect(openDir, &QPushButton::clicked, this, [historyDir] { QDesktopServices::openUrl(QUrl::fromLocalFile(historyDir)); });
    historyForm->addRow(openDir);
    connect(m_history, &QCheckBox::toggled, m_historyMax, &QWidget::setEnabled);
    m_historyMax->setEnabled(m_history->isChecked());
    layout->addWidget(history);

    layout->addStretch();
    return page;
}

QWidget* SettingsDialog::createHotkeyPage(const ActionRegistry& actions)
{
    auto* page = new QWidget(this);
    auto* form = new QFormLayout(page);
    for (const auto& d : AppSettings::hotkeyDefaults()) {
        auto* edit = new QKeySequenceEdit(QKeySequence(m_settings.hotkeys.value(d.action)), page);
        edit->setMaximumSequenceLength(1);
        edit->setClearButtonEnabled(true);
        m_hotkeyEdits.insert(d.action, edit);
        form->addRow(actions.text(d.action) + QStringLiteral("："), edit);
    }
    auto* hint = new QLabel(tr("点击输入框后按下新的组合键；清空表示不使用快捷键。"), page);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:gray;"));
    form->addRow(hint);
    return page;
}

AppSettings SettingsDialog::settings() const
{
    AppSettings s = m_settings;
    for (auto it = m_hotkeyEdits.cbegin(); it != m_hotkeyEdits.cend(); ++it)
        s.hotkeys[it.key()] = it.value()->keySequence().toString(QKeySequence::PortableText);
    s.captureBackend = m_backend->currentData().toString();
    s.uiAutomation = m_uia->isChecked();
    s.historyEnabled = m_history->isChecked();
    s.historyMax = m_historyMax->value();
    s.autoUpdate = m_autoUpdate->isChecked();
    return s;
}

bool SettingsDialog::autostart() const
{
    return m_autostart->isChecked();
}
