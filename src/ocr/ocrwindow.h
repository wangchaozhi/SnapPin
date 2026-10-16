#pragma once

#include "textrecognizer.h"

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;

// 文字识别结果：可编辑、可复制。先显示“正在识别”，结果回来后填入
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

    QLabel* m_status;
    QPlainTextEdit* m_edit;
    QPushButton* m_copyButton;
};
