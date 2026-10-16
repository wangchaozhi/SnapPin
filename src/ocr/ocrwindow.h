#pragma once

#include "textrecognizer.h"

#include <QWidget>

class QFrame;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

// 文字识别结果：可编辑、可复制，Ctrl+F 查找、Ctrl+H（macOS ⌘⌥F）替换。先显示“正在识别”，结果回来后填入
class OcrWindow : public QWidget {
    Q_OBJECT
public:
    OcrWindow();

    void setResult(const OcrResult& result);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* e) override;
    void changeEvent(QEvent* e) override;

private:
    void copyAll();
    void setStatus(const QString& text, bool warning);
    void applyColors();
    void showFindBar(bool replace);
    void hideFindBar();
    void placeFindBar();
    bool findNext(bool backward);
    void replaceOne();
    void replaceAll();
    void updateMatches(); // 高亮所有匹配并显示数量

    QLabel* m_status;
    QPlainTextEdit* m_edit;
    QFrame* m_findBar; // 盖在文本框顶部，不占布局
    QLineEdit* m_findEdit;
    QLineEdit* m_replaceEdit;
    QList<QWidget*> m_replaceWidgets;
    QToolButton* m_caseButton;
    QLabel* m_matchLabel;
    QPushButton* m_copyButton;
    QString m_findHint;
    bool m_glass = false; // 背景是系统毛玻璃
};
