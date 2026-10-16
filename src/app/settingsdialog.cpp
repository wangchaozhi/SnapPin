#include "settingsdialog.h"

#include "actionregistry.h"
#include "appicon.h"
#include "imageutil.h"
#include "platform/platform.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QStandardPaths>
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

    auto* appearance = new QGroupBox(tr("外观"), page);
    auto* appearanceForm = new QFormLayout(appearance);
    m_iconPalette = new QComboBox(appearance);
    m_iconPalette->setIconSize(QSize(24, 24));
    for (const IconPalette& c : iconPalettes())
        m_iconPalette->addItem(makeAppIcon(c.id), c.name, c.id);
    m_iconPalette->setCurrentIndex(qMax(0, m_iconPalette->findData(m_settings.iconPalette)));
    m_iconPalette->setToolTip(tr("托盘和窗口图标的配色；程序文件本身的图标固定为默认配色"));
    appearanceForm->addRow(tr("图标配色："), m_iconPalette);
    layout->addWidget(appearance);

    auto* capture = new QGroupBox(tr("截图"), page);
    auto* captureForm = new QFormLayout(capture);
    m_backend = new QComboBox(capture);
    m_backend->addItem(tr("自动（DXGI 优先，失败时用 GDI）"), QStringLiteral("auto"));
    m_backend->addItem(tr("GDI（兼容性最好）"), QStringLiteral("gdi"));
    m_backend->setCurrentIndex(qMax(0, m_backend->findData(m_settings.captureBackend)));
#ifdef Q_OS_WIN
    captureForm->addRow(tr("截屏方式："), m_backend);
#else
    m_backend->hide(); // 其他平台只有一种截屏方式
#endif
#ifdef Q_OS_WIN
    m_cursor = new QCheckBox(tr("截图时包含鼠标指针"), capture);
#else
    m_cursor = new QCheckBox(tr("截图时包含鼠标指针（当前平台暂不支持）"), capture);
    m_cursor->setEnabled(false);
#endif
    m_cursor->setChecked(m_settings.captureCursor);
    captureForm->addRow(m_cursor);
    layout->addWidget(capture);

    auto* save = new QGroupBox(tr("保存"), page);
    auto* saveForm = new QFormLayout(save);
    m_pattern = new QLineEdit(m_settings.fileNamePattern, save);
    m_pattern->setToolTip(tr("$ 与 $ 之间按日期时间格式替换：yyyy 年、MM 月、dd 日、HH 时、mm 分、ss 秒、zzz 毫秒"));
    auto* preview = new QLabel(save);
    preview->setStyleSheet(QStringLiteral("color:gray;"));
    auto updatePreview = [this, preview] {
        preview->setText(tr("示例：%1.%2").arg(expandFileNamePattern(m_pattern->text(), QDateTime::currentDateTime()),
                                              m_format->currentData().toString()));
    };
    saveForm->addRow(tr("文件名："), m_pattern);
    m_format = new QComboBox(save);
    m_format->addItem(QStringLiteral("PNG"), QStringLiteral("png"));
    m_format->addItem(QStringLiteral("JPG"), QStringLiteral("jpg"));
    m_format->setCurrentIndex(qMax(0, m_format->findData(m_settings.saveFormat)));
    saveForm->addRow(tr("默认格式："), m_format);
    saveForm->addRow(QString(), preview);
    m_quality = new QSpinBox(save);
    m_quality->setRange(1, 100);
    m_quality->setValue(m_settings.jpgQuality);
    saveForm->addRow(tr("JPG 质量："), m_quality);
    auto updateQuality = [this] { m_quality->setEnabled(m_format->currentData().toString() == QLatin1String("jpg")); };
    connect(m_pattern, &QLineEdit::textChanged, this, updatePreview);
    connect(m_format, &QComboBox::currentIndexChanged, this, [updatePreview, updateQuality] {
        updatePreview();
        updateQuality();
    });
    updatePreview();
    updateQuality();

    m_quickDir = new QLineEdit(m_settings.quickSaveDir, save);
    m_quickDir->setPlaceholderText(QDir::toNativeSeparators(
        QDir(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)).filePath(QStringLiteral("SnapPin"))));
    auto* browse = new QPushButton(tr("浏览…"), save);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("快速保存目录"), m_quickDir->text());
        if (!dir.isEmpty())
            m_quickDir->setText(QDir::toNativeSeparators(dir));
    });
    auto* quickRow = new QHBoxLayout;
    quickRow->addWidget(m_quickDir);
    quickRow->addWidget(browse);
    saveForm->addRow(tr("快速保存到："), quickRow);
    layout->addWidget(save);

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
    s.historyEnabled = m_history->isChecked();
    s.historyMax = m_historyMax->value();
    s.autoUpdate = m_autoUpdate->isChecked();
    s.captureCursor = m_cursor->isChecked();
    s.iconPalette = m_iconPalette->currentData().toString();
    s.fileNamePattern = m_pattern->text().trimmed().isEmpty() ? AppSettings().fileNamePattern : m_pattern->text().trimmed();
    s.saveFormat = m_format->currentData().toString();
    s.jpgQuality = m_quality->value();
    s.quickSaveDir = m_quickDir->text().trimmed();
    return s;
}

bool SettingsDialog::autostart() const
{
    return m_autostart->isChecked();
}
