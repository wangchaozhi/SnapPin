#pragma once

#include "textrecognizer.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// 文字识别结果：可编辑、可复制，Ctrl+F 查找、Ctrl+H（macOS ⌘⌥F）替换。先显示“正在识别”，结果回来后填入
class OcrWindow : public QWidget {
    Q_OBJECT
public:
    OcrWindow();

    void setResult(const OcrResult& result);

protected:
    void keyPressEvent(QKeyEvent* e) override;

private:
    void copyAll();
    void setStatus(const QString& text, bool warning);
    void showFindBar(bool replace);
    void hideFindBar();
    bool findNext(bool backward);
    void replaceOne();
    void replaceAll();
    void updateMatches(); // 高亮所有匹配并显示数量

    QLabel* m_status;
    QWidget* m_findBar;
    QLineEdit* m_findEdit;
    QWidget* m_replaceRow;
    QLineEdit* m_replaceEdit;
    QCheckBox* m_caseCheck;
    QLabel* m_matchLabel;
    QPlainTextEdit* m_edit;
    QPushButton* m_copyButton;
    QString m_findHint;
};
