#include "scrollcapture.h"

#include "platform/platform.h"

#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>
#include <cstring>

namespace {

constexpr int kIntervalMs = 50;
constexpr int kFrameMargin = 3;     // 边框画在选区外，不会被截进去
constexpr int kScrollbarIgnore = 24; // 逻辑像素：右侧滚动条区域不参与匹配
constexpr int kPreviewW = 150;
constexpr int kPreviewH = 320;

const Qt::WindowFlags kOverlayFlags =
    Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus;

// 比较两帧，忽略右侧滚动条区域（Win11 的滚动条在滚动后还会持续淡入淡出）
bool sameIgnoringRight(const QImage& a, const QImage& b, int ignoreRight)
{
    if (a.size() != b.size() || a.format() != b.format() || a.depth() != 32)
        return false;
    const int w = qMax(1, a.width() - qMin(ignoreRight, a.width() / 4));
    for (int y = 0; y < a.height(); ++y) {
        if (std::memcmp(a.constScanLine(y), b.constScanLine(y), size_t(w) * 4) != 0)
            return false;
    }
    return true;
}

// 选区边框：透明、鼠标穿透，让滚轮事件落到下面的程序
class RegionFrame : public QWidget {
public:
    RegionFrame()
        : QWidget(nullptr, kOverlayFlags | Qt::WindowTransparentForInput)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setPen(QPen(QColor(255, 80, 40), 2, Qt::DashLine));
        p.drawRect(rect().adjusted(1, 1, -2, -2));
    }
};

} // namespace

ScrollCaptureSession::ScrollCaptureSession(IScreenCapturer* capturer, QScreen* screen, const QRect& globalRect,
                                           QObject* parent)
    : QObject(parent)
    , m_capturer(capturer)
    , m_screen(screen)
    , m_globalRect(globalRect)
    , m_dpr(screen->devicePixelRatio())
    , m_ignoreRight(qRound(kScrollbarIgnore * screen->devicePixelRatio()))
    , m_stitcher(m_ignoreRight)
{
    const QRect local = globalRect.translated(-screen->geometry().topLeft());
    m_physRect = QRectF(local.x() * m_dpr, local.y() * m_dpr, local.width() * m_dpr, local.height() * m_dpr).toRect();

    m_timer.setInterval(kIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &ScrollCaptureSession::tick);
    connect(screen, &QObject::destroyed, this, &ScrollCaptureSession::close);
}

ScrollCaptureSession::~ScrollCaptureSession()
{
    delete m_frame;
    delete m_panel;
}

void ScrollCaptureSession::start()
{
    m_frame = new RegionFrame;
    m_frame->setGeometry(m_globalRect.adjusted(-kFrameMargin, -kFrameMargin, kFrameMargin, kFrameMargin));
    m_frame->show();

    m_panel = createPanel();
    m_panel->adjustSize();
    // 面板放在选区右侧，放不下依次尝试左侧、选区内右上角
    const QRect screenRect = m_screen->geometry();
    const QSize ps = m_panel->size();
    QPoint pos(m_globalRect.right() + kFrameMargin + 8, m_globalRect.top());
    if (pos.x() + ps.width() > screenRect.right())
        pos.setX(m_globalRect.left() - kFrameMargin - 8 - ps.width());
    if (pos.x() < screenRect.left())
        pos.setX(m_globalRect.right() - ps.width() - 8);
    pos.setY(qBound(screenRect.top(), pos.y(), screenRect.bottom() - ps.height()));
    m_panel->move(pos);
    m_panel->show();

    // 等遮罩从屏幕上消失后再开始截取
    QTimer::singleShot(200, this, [this] {
        tick();
        m_timer.start();
    });
}

QWidget* ScrollCaptureSession::createPanel()
{
    auto* panel = new QWidget(nullptr, kOverlayFlags);
    panel->setAttribute(Qt::WA_ShowWithoutActivating); // 不抢走目标程序的焦点
    panel->setObjectName(QStringLiteral("ScrollPanel"));
    panel->setStyleSheet(QStringLiteral(
        "#ScrollPanel{background:#fafafa;border:1px solid #b8b8b8;}"
        "QPushButton{padding:4px 8px;}"));

    auto* hint = new QLabel(tr("在选区内上下滚动鼠标滚轮\n新内容会自动拼接"), panel);
    hint->setAlignment(Qt::AlignCenter);
    m_preview = new QLabel(panel);
    m_preview->setFixedSize(kPreviewW, kPreviewH);
    m_preview->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    m_preview->setStyleSheet(QStringLiteral("background:#e8e8e8;"));
    m_status = new QLabel(panel);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setWordWrap(true);

    auto* copy = new QPushButton(tr("完成"), panel);
    copy->setToolTip(tr("复制到剪贴板"));
    auto* pin = new QPushButton(tr("贴图"), panel);
    auto* save = new QPushButton(tr("保存"), panel);
    auto* cancel = new QPushButton(tr("取消"), panel);
    for (QPushButton* b : {copy, pin, save, cancel})
        b->setFocusPolicy(Qt::NoFocus);
    connect(copy, &QPushButton::clicked, this, [this] { finish(FinishAction::Copy); });
    connect(pin, &QPushButton::clicked, this, [this] { finish(FinishAction::Pin); });
    connect(save, &QPushButton::clicked, this, [this] { finish(FinishAction::Save); });
    connect(cancel, &QPushButton::clicked, this, &ScrollCaptureSession::close);

    auto* row1 = new QHBoxLayout;
    row1->addWidget(copy);
    row1->addWidget(pin);
    auto* row2 = new QHBoxLayout;
    row2->addWidget(save);
    row2->addWidget(cancel);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(hint);
    layout->addWidget(m_preview);
    layout->addWidget(m_status);
    layout->addLayout(row1);
    layout->addLayout(row2);
    return panel;
}

void ScrollCaptureSession::tick()
{
    if (!m_screen)
        return;
    const QPixmap shot = m_capturer->grab(m_screen);
    const QRect r = m_physRect & QRect(QPoint(0, 0), shot.size());
    if (r.isEmpty())
        return;
    const QImage frame = shot.copy(r).toImage();
    // 平滑滚动过程中，程序先整体平移旧内容、稍后才重绘新露出的部分，
    // 这时截到的帧底部是脏的。连续两次截到相同画面（滚动已停稳）才拿去拼接
    if (!sameIgnoringRight(frame, m_pending, m_ignoreRight)) {
        m_pending = frame;
        m_pendingUsed = false;
        return;
    }
    if (m_pendingUsed)
        return;
    m_pendingUsed = true;
    const auto result = m_stitcher.add(frame);
    static const QString dumpDir = qEnvironmentVariable("SNAPPIN_SCROLL_DUMP"); // 调试：保存每一帧
    if (!dumpDir.isEmpty()) {
        static int n = 0;
        frame.save(QStringLiteral("%1/f%2_r%3_dy%4.png").arg(dumpDir).arg(n++, 3, 10, QLatin1Char('0'))
                       .arg(int(result)).arg(m_stitcher.lastScroll()));
    }
    if (result == ScrollStitcher::Result::Full)
        m_timer.stop();
    if (result != ScrollStitcher::Result::Unchanged)
        updatePanel(result);
}

void ScrollCaptureSession::updatePanel(ScrollStitcher::Result r)
{
    // 预览以当前画面为中心，向上滚时看到顶部新接的内容，向下滚时看到底部
    const int totalH = m_stitcher.resultHeight();
    const int width = m_physRect.width();
    const int visibleH = qMin(totalH, width * kPreviewH / kPreviewW);
    const QRect view = m_stitcher.currentView();
    const int y = qBound(0, view.center().y() - visibleH / 2, totalH - visibleH);
    QImage part = m_stitcher.resultPart(y, visibleH);
    {
        // 标出当前画面所在位置
        QPainter p(&part);
        p.setPen(QPen(QColor(255, 80, 40), qMax(2, width / kPreviewW * 2)));
        p.drawRect(view.translated(0, -y).adjusted(1, 1, -2, -2));
    }
    m_preview->setPixmap(QPixmap::fromImage(part.scaled(m_preview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));

    QString status = tr("已拼接 %1 px").arg(totalH);
    if (r == ScrollStitcher::Result::NoMatch)
        status += tr("\n未能接上，请慢一点滚动");
    else if (r == ScrollStitcher::Result::Full)
        status += tr("\n已达到最大高度");
    m_status->setText(status);
}

void ScrollCaptureSession::finish(FinishAction action)
{
    m_timer.stop();
    QImage img = m_stitcher.result();
    if (img.isNull()) {
        close();
        return;
    }
    img.setDevicePixelRatio(m_dpr);
    const QRect rect(m_globalRect.topLeft(), (QSizeF(img.size()) / m_dpr).toSize());
    close();
    emit finished(img, rect, action);
}

void ScrollCaptureSession::close()
{
    if (m_closed)
        return;
    m_closed = true;
    m_timer.stop();
    if (m_frame)
        m_frame->hide();
    if (m_panel)
        m_panel->hide();
    emit closed();
}
