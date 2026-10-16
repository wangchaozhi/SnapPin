#include "ocrwindow.h"

#include "platform/platform.h"

#include <QAction>
#include <QClipboard>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

OcrWindow::OcrWindow()
    : QWidget(nullptr, Qt::Window | Qt::WindowStaysOnTopHint)
    , m_status(new QLabel(this))
    , m_edit(new QPlainTextEdit(this))
    , m_findBar(new QFrame(this))
    , m_findEdit(new QLineEdit(m_findBar))
    , m_replaceEdit(new QLineEdit(m_findBar))
    , m_caseButton(new QToolButton(m_findBar))
    , m_matchLabel(new QLabel(m_findEdit))
    , m_copyButton(new QPushButton(tr("复制全部"), this))
{
    setWindowTitle(tr("识别文字 - SnapPin"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(520, 360);
    // 与贴图同一层，从贴图识别时结果窗口不会被贴图挡住
    setupFloatingWindow(this, FloatingLayer::Pin);
    m_glass = setupGlassBackground(this);

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

    // 查找 / 替换：浮在文本框右上角的面板。Ctrl+F、Ctrl+H 打开，Esc 收起。回车查找下一个，Shift+回车查找上一个
    m_findBar->setObjectName(QStringLiteral("findPanel"));
    m_findEdit->setPlaceholderText(tr("查找"));
    m_replaceEdit->setPlaceholderText(tr("替换为"));
    for (QLineEdit* e : {m_findEdit, m_replaceEdit})
        e->setFixedWidth(220);
    m_matchLabel->setStyleSheet(QStringLiteral("color:gray;")); // 匹配数显示在查找框里的右侧
    m_matchLabel->setAttribute(Qt::WA_TransparentForMouseEvents);

    auto toolButton = [this](const QString& text, const QString& tip) {
        auto* b = new QToolButton(m_findBar);
        b->setText(text);
        b->setToolTip(tip);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };
    m_caseButton->setText(QStringLiteral("Aa"));
    m_caseButton->setToolTip(tr("区分大小写"));
    m_caseButton->setCheckable(true);
    m_caseButton->setAutoRaise(true);
    m_caseButton->setFocusPolicy(Qt::NoFocus);
    QToolButton* prevButton = toolButton(QStringLiteral("↑"), tr("上一个（Shift+回车）"));
    QToolButton* nextButton = toolButton(QStringLiteral("↓"), tr("下一个（回车）"));
    QToolButton* closeFindButton = toolButton(QStringLiteral("✕"), tr("关闭（Esc）"));
    QToolButton* replaceButton = toolButton(tr("替换"), tr("替换当前（回车）"));
    QToolButton* replaceAllButton = toolButton(tr("全部"), tr("全部替换"));
    // 图标按钮统一成方形，替换按钮各占两格，上下两行对齐
    const int side = m_findEdit->sizeHint().height();
    for (QToolButton* b : {m_caseButton, prevButton, nextButton, closeFindButton})
        b->setFixedSize(side, side);
    for (QToolButton* b : {replaceButton, replaceAllButton}) {
        b->setFixedHeight(side);
        b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    connect(prevButton, &QToolButton::clicked, this, [this] { findNext(true); });
    connect(nextButton, &QToolButton::clicked, this, [this] { findNext(false); });
    connect(closeFindButton, &QToolButton::clicked, this, &OcrWindow::hideFindBar);
    connect(replaceButton, &QToolButton::clicked, this, &OcrWindow::replaceOne);
    connect(replaceAllButton, &QToolButton::clicked, this, &OcrWindow::replaceAll);
    connect(m_findEdit, &QLineEdit::returnPressed, this,
            [this] { findNext(QGuiApplication::keyboardModifiers() & Qt::ShiftModifier); });
    connect(m_replaceEdit, &QLineEdit::returnPressed, this, &OcrWindow::replaceOne);
    connect(m_findEdit, &QLineEdit::textChanged, this, [this] {
        // 边输入边定位：从当前选区开头重新找，找不到时停在原处
        QTextCursor c = m_edit->textCursor();
        c.setPosition(c.selectionStart());
        m_edit->setTextCursor(c);
        if (!m_findEdit->text().isEmpty())
            findNext(false);
        updateMatches();
    });
    connect(m_caseButton, &QToolButton::toggled, this, &OcrWindow::updateMatches);
    connect(m_edit, &QPlainTextEdit::textChanged, this, [this] {
        if (m_findBar->isVisible())
            updateMatches();
    });

    auto* findLayout = new QGridLayout(m_findBar);
    findLayout->setContentsMargins(6, 6, 6, 6);
    findLayout->setSpacing(4);
    findLayout->addWidget(m_findEdit, 0, 0);
    findLayout->addWidget(m_caseButton, 0, 1);
    findLayout->addWidget(prevButton, 0, 2);
    findLayout->addWidget(nextButton, 0, 3);
    findLayout->addWidget(closeFindButton, 0, 4);
    findLayout->addWidget(m_replaceEdit, 1, 0);
    findLayout->addWidget(replaceButton, 1, 1, 1, 2);
    findLayout->addWidget(replaceAllButton, 1, 3, 1, 2);
    m_replaceWidgets = {m_replaceEdit, replaceButton, replaceAllButton}; // 替换一行只在 Ctrl+H 时显示
    m_findBar->hide();
    m_edit->installEventFilter(this); // 文本框大小变了，面板跟着贴在右上角

    auto* findAction = new QAction(this);
    findAction->setShortcut(QKeySequence::Find);
    connect(findAction, &QAction::triggered, this, [this] { showFindBar(false); });
    auto* replaceAction = new QAction(this);
#ifdef Q_OS_MACOS
    replaceAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F)); // ⌘H 是系统的“隐藏”
#else
    replaceAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));
#endif
    connect(replaceAction, &QAction::triggered, this, [this] { showFindBar(true); });
    addAction(findAction);
    addAction(replaceAction);
    m_findHint = tr("%1 查找，%2 替换").arg(findAction->shortcut().toString(QKeySequence::NativeText),
                                          replaceAction->shortcut().toString(QKeySequence::NativeText));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_status);
    layout->addWidget(m_edit, 1);
    layout->addLayout(buttons);
    setStatus(tr("正在识别文字…"), false);
    applyColors();
}

// 毛玻璃背景下文本框和查找面板只留一层淡淡的底色，透出后面的玻璃；跟随系统深浅色切换
void OcrWindow::applyColors()
{
    const QPalette pal = QGuiApplication::palette();
    QColor panel = pal.color(QPalette::Window);
    if (m_glass) {
        QPalette editPal = m_edit->palette();
        QColor base = pal.color(QPalette::Base);
        base.setAlpha(90);
        editPal.setColor(QPalette::Base, base);
        m_edit->setPalette(editPal);
        m_edit->setFrameShape(QFrame::NoFrame);
        panel.setAlpha(225);
    }
    m_findBar->setStyleSheet(QStringLiteral("#findPanel { background: %1; border: 1px solid %2; border-radius: 8px; }")
                                 .arg(panel.name(QColor::HexArgb), pal.color(QPalette::Mid).name()));
}

void OcrWindow::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::ApplicationPaletteChange)
        applyColors();
    QWidget::changeEvent(e);
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
        setStatus(tr("可以直接修改，选中部分按 Ctrl+C 复制，或点“复制全部”；") + m_findHint, false);
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

void OcrWindow::showFindBar(bool replace)
{
    // 选中了一行以内的文字就拿来当查找内容
    const QString selected = m_edit->textCursor().selectedText();
    if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator))
        m_findEdit->setText(selected);
    for (QWidget* w : std::as_const(m_replaceWidgets))
        w->setVisible(replace);
    m_findBar->show();
    m_findBar->raise();
    placeFindBar();
    QLineEdit* target = replace && !m_findEdit->text().isEmpty() ? m_replaceEdit : m_findEdit;
    target->setFocus();
    target->selectAll();
    updateMatches();
}

void OcrWindow::hideFindBar()
{
    m_findBar->hide();
    m_edit->setExtraSelections({});
    m_edit->setFocus();
}

bool OcrWindow::findNext(bool backward)
{
    const QString text = m_findEdit->text();
    if (text.isEmpty())
        return false;
    QTextDocument::FindFlags flags;
    if (backward)
        flags |= QTextDocument::FindBackward;
    if (m_caseButton->isChecked())
        flags |= QTextDocument::FindCaseSensitively;
    QTextDocument* doc = m_edit->document();
    QTextCursor found = doc->find(text, m_edit->textCursor(), flags);
    if (found.isNull()) { // 到头了从另一端接着找
        QTextCursor start(doc);
        if (backward)
            start.movePosition(QTextCursor::End);
        found = doc->find(text, start, flags);
    }
    if (found.isNull())
        return false;
    m_edit->setTextCursor(found);
    updateMatches();
    return true;
}

void OcrWindow::replaceOne()
{
    if (m_edit->isReadOnly() || m_findEdit->text().isEmpty())
        return;
    // 当前选中的正好是查找内容才替换，否则先定位到下一个
    QTextCursor c = m_edit->textCursor();
    const Qt::CaseSensitivity cs = m_caseButton->isChecked() ? Qt::CaseSensitive : Qt::CaseInsensitive;
    if (c.hasSelection() && c.selectedText().compare(m_findEdit->text(), cs) == 0) {
        c.insertText(m_replaceEdit->text());
        m_edit->setTextCursor(c);
    }
    findNext(false);
}

void OcrWindow::replaceAll()
{
    const QString text = m_findEdit->text();
    if (m_edit->isReadOnly() || text.isEmpty())
        return;
    QTextDocument::FindFlags flags;
    if (m_caseButton->isChecked())
        flags |= QTextDocument::FindCaseSensitively;
    QTextDocument* doc = m_edit->document();
    QTextCursor edit(doc);
    edit.beginEditBlock(); // 一次撤销就能恢复
    int count = 0;
    for (QTextCursor c = doc->find(text, 0, flags); !c.isNull(); c = doc->find(text, c, flags)) {
        c.insertText(m_replaceEdit->text());
        ++count;
    }
    edit.endEditBlock();
    updateMatches();
    m_matchLabel->setText(tr("已替换 %1 处").arg(count));
}

void OcrWindow::updateMatches()
{
    QList<QTextEdit::ExtraSelection> selections;
    const QString text = m_findEdit->text();
    int current = 0;
    if (m_findBar->isVisible() && !text.isEmpty()) {
        QTextDocument::FindFlags flags;
        if (m_caseButton->isChecked())
            flags |= QTextDocument::FindCaseSensitively;
        const QTextCursor cursor = m_edit->textCursor();
        QTextCharFormat format;
        format.setBackground(QColor(255, 210, 0, 110));
        QTextDocument* doc = m_edit->document();
        for (QTextCursor c = doc->find(text, 0, flags); !c.isNull(); c = doc->find(text, c, flags)) {
            selections.append({c, format});
            if (c.selectionStart() == cursor.selectionStart() && c.selectionEnd() == cursor.selectionEnd())
                current = int(selections.size());
        }
        if (selections.isEmpty())
            m_matchLabel->setText(tr("无结果"));
        else if (current > 0)
            m_matchLabel->setText(QStringLiteral("%1/%2").arg(current).arg(selections.size()));
        else
            m_matchLabel->setText(QString::number(selections.size()));
    } else {
        m_matchLabel->clear();
    }
    m_edit->setExtraSelections(selections);
    m_matchLabel->adjustSize();
    m_matchLabel->move(m_findEdit->width() - m_matchLabel->width() - 6,
                       (m_findEdit->height() - m_matchLabel->height()) / 2);
    m_findEdit->setTextMargins(0, 0, m_matchLabel->text().isEmpty() ? 0 : m_matchLabel->width() + 4, 0);
}

void OcrWindow::placeFindBar()
{
    if (!m_findBar->isVisible())
        return;
    m_findBar->adjustSize();
    const QRect r = m_edit->geometry();
    const QScrollBar* bar = m_edit->verticalScrollBar();
    const int scrollWidth = bar->isVisible() ? bar->width() : 0;
    m_findBar->move(r.right() - scrollWidth - m_findBar->width() - 6, r.top() + 6);
}

bool OcrWindow::eventFilter(QObject* watched, QEvent* e)
{
    if (watched == m_edit && (e->type() == QEvent::Resize || e->type() == QEvent::Move))
        placeFindBar();
    return QWidget::eventFilter(watched, e);
}

void OcrWindow::keyPressEvent(QKeyEvent* e)
{
    if (e->key() == Qt::Key_Escape) {
        if (m_findBar->isVisible())
            hideFindBar();
        else
            close();
        return;
    }
    QWidget::keyPressEvent(e);
}
