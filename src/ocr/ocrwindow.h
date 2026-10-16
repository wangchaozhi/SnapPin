#pragma once

#include "platform/platform.h"
#include "textrecognizer.h"

#include <QWidget>

class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSplitter;
class QToolButton;
class Translator;

// 文字识别结果：可编辑、可复制，Ctrl+F 查找、Ctrl+H（macOS ⌘⌥F）替换。先显示“正在识别”，结果回来后填入。
// 给了 translator 时可以翻译，译文显示在原文右边（窗口窄时在下面）
class OcrWindow : public QWidget {
    Q_OBJECT
public:
    explicit OcrWindow(Translator* translator = nullptr);

    void setResult(const OcrResult& result);

protected:
    void showEvent(QShowEvent* e) override;
    void paintEvent(QPaintEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;
    void changeEvent(QEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void copyAll();
    void translate();
    void setStatus(const QString& text, bool warning);
    void applyColors();
    Qt::Edges resizeEdges(const QPoint& pos) const; // 无边框窗口：鼠标所在的可缩放边缘
    void showFindBar(bool replace);
    void hideFindBar();
    void placeFindBar();
    bool findNext(bool backward);
    void replaceOne();
    void replaceAll();
    void updateMatches(); // 高亮所有匹配并显示数量

    TitleBarMode m_titleMode = TitleBarMode::Native;
    QWidget* m_titleBar = nullptr; // 窗口自己画的标题行（macOS、Linux）；否则为空
    QLabel* m_titleLabel = nullptr;
    QLabel* m_status;
    QPlainTextEdit* m_edit;
    QFrame* m_findBar; // 盖在文本框顶部，不占布局
    QLineEdit* m_findEdit;
    QLineEdit* m_replaceEdit;
    QList<QWidget*> m_replaceWidgets;
    QToolButton* m_caseButton;
    QLabel* m_matchLabel;
    QPushButton* m_copyButton;
    Translator* m_translator;
    QSplitter* m_splitter;
    QHBoxLayout* m_buttons = nullptr;
    QPlainTextEdit* m_translation = nullptr; // 第一次翻译时创建
    QComboBox* m_targetCombo = nullptr;
    QPushButton* m_translateButton = nullptr;
    QPushButton* m_copyTranslationButton = nullptr;
    int m_translateRequest = 0; // 只采用最近一次翻译的结果
    QString m_findHint;
    bool m_glass = false; // 背景是系统毛玻璃
};
