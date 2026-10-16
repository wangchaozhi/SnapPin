#include "ocrwindow.h"

#include "platform/platform.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

OcrWindow::OcrWindow()
    : QWidget(nullptr, Qt::Window | Qt::WindowStaysOnTopHint)
    , m_status(new QLabel(this))
    , m_edit(new QPlainTextEdit(this))
    , m_copyButton(new QPushButton(tr("复制全部"), this))
{
    setWindowTitle(tr("识别文字 - SnapPin"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(520, 360);
    // 与贴图同一层，从贴图识别时结果窗口不会被贴图挡住
    setupFloatingWindow(this, FloatingLayer::Pin);

    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse); // 安装命令可以直接选中复制
    m_edit->setReadOnly(true);
    m_edit->setPlaceholderText(tr("正在识别文字…"));
    m_copyButton->setEnabled(false);
    m_copyButton->setDefault(true);
    connect(m_copyButton, &QPushButton::clicked, this, &OcrWindow::copyAll);
    auto* closeButton = new QPushButton(tr("关闭"), this);
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    buttons->addWidget(m_copyButton);
    buttons->addWidget(closeButton);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_status);
    layout->addWidget(m_edit, 1);
    layout->addLayout(buttons);
    setStatus(tr("正在识别文字…"), false);
}

void OcrWindow::setResult(const OcrResult& result)
{
    m_edit->setPlaceholderText(QString());
    if (!result.error.isEmpty()) {
        setStatus(result.error, true);
        return;
    }
    m_edit->setReadOnly(false);
    m_edit->setPlainText(result.text);
    m_copyButton->setEnabled(!result.text.isEmpty());
    if (result.text.isEmpty())
        setStatus(tr("没有识别到文字"), true);
    else
        setStatus(tr("可以直接修改，选中部分按 Ctrl+C 复制，或点“复制全部”"), false);
    m_edit->setFocus();
}

void OcrWindow::copyAll()
{
    QGuiApplication::clipboard()->setText(m_edit->toPlainText());
    m_copyButton->setText(tr("已复制"));
    QTimer::singleShot(1500, m_copyButton, [b = m_copyButton] { b->setText(tr("复制全部")); });
}

void OcrWindow::setStatus(const QString& text, bool warning)
{
    m_status->setText(text);
    m_status->setStyleSheet(warning ? QStringLiteral("color:#d9822b;") : QStringLiteral("color:gray;"));
}

void OcrWindow::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}
