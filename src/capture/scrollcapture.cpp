#include "scrollcapture.h"

#include "platform/platform.h"

#include <QDir>
#include <QHashFunctions>
#include <QFile>
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
constexpr int kStillMs = 250;     // 只有局部变化（动画、光标闪烁）时，静止这么久才算停稳
constexpr int kNoMotionMs = 400;  // 重放后这么久都没有滚动：已到顶/到底，或程序不响应
constexpr int kStuckMs = 3000;    // 滚动开始后迟迟停不下来时的兜底
constexpr int kWheelNotch = 120;  // 一格滚轮
constexpr int kMinReplayStep = 30;
constexpr int kFrameMargin = 3;     // 边框画在选区外，不会被截进去
constexpr int kScrollbarIgnore = 24; // 逻辑像素：右侧滚动条区域不参与匹配
constexpr int kPreviewW = 150;
constexpr int kPreviewH = 320;

const Qt::WindowFlags kOverlayFlags =
    Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool | Qt::WindowDoesNotAcceptFocus;

} // namespace

// 逐行签名，忽略右侧滚动条区域（Win11 的滚动条在滚动后还会持续淡入淡出）
ScrollCaptureSession::FrameSig ScrollCaptureSession::signature(const QImage& img) const
{
    FrameSig sig;
    if (img.depth() != 32)
        return sig;
    const int w = qMax(1, img.width() - qMin(m_ignoreRight, img.width() / 4));
    sig.hash.resize(size_t(img.height()));
    sig.uniform.resize(size_t(img.height()));
    for (int y = 0; y < img.height(); ++y) {
        const auto* line = reinterpret_cast<const quint32*>(img.constScanLine(y));
        sig.hash[size_t(y)] = qHashBits(line, size_t(w) * 4);
        bool uniform = true;
        for (int x = 1; x < w && uniform; ++x)
            uniform = line[x] == line[0];
        sig.uniform[size_t(y)] = uniform;
    }
    return sig;
}

// 两帧之间没有上下移动：有内容的行大多停在原位，只有局部区域变化
bool ScrollCaptureSession::noMotion(const FrameSig& a, const FrameSig& b)
{
    if (a.hash.size() != b.hash.size() || a.hash.empty())
        return false;
    int informative = 0, same = 0;
    for (size_t y = 0; y < a.hash.size(); ++y) {
        if (a.uniform[y] && b.uniform[y])
            continue;
        ++informative;
        if (a.hash[y] == b.hash[y])
            ++same;
    }
    return informative < 8 ? a.hash == b.hash : same >= informative * 6 / 10;
}

namespace {

// 选区边框：透明、鼠标穿透，让滚轮事件落到下面的程序
class RegionFrame : public QWidget {
public:
    RegionFrame()
        : QWidget(nullptr, kOverlayFlags | Qt::WindowTransparentForInput)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setupFloatingWindow(this, FloatingLayer::Overlay);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setPen(QPen(QColor(255, 80, 40), 2, Qt::DashLine));
        p.drawRect(rect().adjusted(1, 1, -2, -2));
    }

    void resizeEvent(QResizeEvent*) override
    {
        // 没有合成器的 X11 桌面上透明背景会画成黑色、盖住选区，用形状遮罩只保留外圈边框
        const QRegion outer(rect());
        setMask(outer.subtracted(QRegion(rect().adjusted(kFrameMargin, kFrameMargin, -kFrameMargin, -kFrameMargin))));
    }
};

} // namespace

ScrollCaptureSession::ScrollCaptureSession(IScreenCapturer* capturer, IWheelThrottle* throttle, QScreen* screen,
                                           const QRect& globalRect, QObject* parent)
    : QObject(parent)
    , m_capturer(capturer)
    , m_throttle(throttle)
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

    // 接管选区内的滚轮：滚得再快也按“滚一步 → 停稳 → 拼接”的节奏回放
    m_throttleActive = m_throttle->start(m_screen, m_physRect);
    debugLog(QStringLiteral("start: wheel throttle %1").arg(m_throttleActive ? QStringLiteral("active") : QStringLiteral("unavailable")));
    // 没有接管时快速滚动会接不上；原因是缺权限的话提示用户授权，授权后 tick() 里自动接管
    m_permissionRow->setVisible(!m_throttleActive && m_throttle->needsPermission());
    m_panel->adjustSize();

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
    setupFloatingWindow(panel, FloatingLayer::Overlay);
    panel->setObjectName(QStringLiteral("ScrollPanel"));
    panel->setStyleSheet(QStringLiteral(
        "#ScrollPanel{background:#fafafa;border:1px solid #b8b8b8;}"
        "QPushButton{padding:4px 8px;}"));

    auto* hint = new QLabel(tr("在选区内上下滚动鼠标滚轮\n新内容会自动拼接，滚多快都行"), panel);
    hint->setAlignment(Qt::AlignCenter);
    m_preview = new QLabel(panel);
    m_preview->setFixedSize(kPreviewW, kPreviewH);
    m_preview->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    m_preview->setStyleSheet(QStringLiteral("background:#e8e8e8;"));
    m_status = new QLabel(panel);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setWordWrap(true);

    m_permissionRow = new QWidget(panel);
    auto* permissionText = new QLabel(tr("未开启“辅助功能”权限，\n快速滚动可能接不上"), m_permissionRow);
    permissionText->setAlignment(Qt::AlignCenter);
    permissionText->setStyleSheet(QStringLiteral("color:#c0392b;"));
    auto* grant = new QPushButton(tr("去授权"), m_permissionRow);
    grant->setFocusPolicy(Qt::NoFocus);
    connect(grant, &QPushButton::clicked, this, [this] { m_throttle->openPermissionSettings(); });
    auto* permissionLayout = new QVBoxLayout(m_permissionRow);
    permissionLayout->setContentsMargins(0, 0, 0, 0);
    permissionLayout->addWidget(permissionText);
    permissionLayout->addWidget(grant);
    m_permissionRow->hide();

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
    layout->addWidget(m_permissionRow);
    layout->addLayout(row1);
    layout->addLayout(row2);
    return panel;
}

void ScrollCaptureSession::tick()
{
    if (!m_screen)
        return;
    if (m_permissionRow->isVisible() && !m_throttle->needsPermission()) {
        // 用户在长截图进行中授予了权限：现在接管滚动
        m_throttleActive = m_throttle->start(m_screen, m_physRect);
        debugLog(QStringLiteral("permission granted: wheel throttle %1").arg(m_throttleActive ? QStringLiteral("active") : QStringLiteral("unavailable")));
        m_permissionRow->hide();
        m_panel->adjustSize();
    }
    const QPixmap shot = m_capturer->grab(m_screen);
    const QRect r = m_physRect & QRect(QPoint(0, 0), shot.size());
    if (r.isEmpty())
        return;
    const QImage frame = shot.copy(r).toImage();
    const FrameSig sig = signature(frame);
    const bool identical = !sig.hash.empty() && sig.hash == m_lastSig.hash;
    const bool still = identical || noMotion(sig, m_lastSig);
    m_lastSig = sig;

    if (!still) { // 内容在上下移动
        m_settled = false;
        m_stillClock.invalidate();
        m_motionSinceReplay = true;
        return;
    }
    if (!m_stillClock.isValid())
        m_stillClock.start();

    // 平滑滚动时程序先平移旧内容、稍后才重绘新露出的部分，这期间的帧是脏的。
    // 连续两帧完全相同立即算停稳；只有局部变化（动画、光标闪烁）时静止一会儿再算
    if (!m_settled && (identical || m_stillClock.elapsed() >= kStillMs)) {
        m_settled = true;
        const auto result = m_stitcher.add(frame);
        debugLog(QStringLiteral("settled (%1) -> result %2 scroll %3 total %4")
                     .arg(identical ? QStringLiteral("identical") : QStringLiteral("still"))
                     .arg(int(result)).arg(m_stitcher.lastScroll()).arg(m_stitcher.resultHeight()), &frame);
        if (m_awaitingResponse && m_motionSinceReplay) { // 上一步滚动已完成
            m_awaitingResponse = false;
            const bool ok = result == ScrollStitcher::Result::Appended || result == ScrollStitcher::Result::Moved;
            if (ok && !m_recovering)
                adaptReplayStep();
            if (ok)
                m_recovering = false;
            else if (result == ScrollStitcher::Result::NoMatch)
                recoverFromNoMatch();
        }
        if (result == ScrollStitcher::Result::Full)
            m_timer.stop();
        if (result != ScrollStitcher::Result::Unchanged)
            updatePanel(result);
    }
    replayNextStep();
}

void ScrollCaptureSession::replayNextStep()
{
    if (!m_throttleActive)
        return;
    if (m_awaitingResponse) {
        const qint64 ms = m_sinceReplay.elapsed();
        if (!m_motionSinceReplay && ms >= kNoMotionMs) {
            // 完全没滚动：已到顶 / 到底，同方向剩下的滚动没有意义
            debugLog(QStringLiteral("no motion after replay, dropping %1").arg(m_throttle->pendingDelta()));
            m_throttle->clear();
            m_awaitingResponse = false;
        } else if (ms >= kStuckMs) {
            debugLog(QStringLiteral("replay stuck"));
            m_awaitingResponse = false;
        } else {
            updateStatus();
            return; // 上一步还在滚动或尚未停稳
        }
    }
    if (!m_settled) // 画面停稳并拼接后才放出下一步
        return;
    const int pending = m_throttle->pendingDelta();
    if (pending != 0) {
        m_lastReplayDelta = qMin(qAbs(pending), m_replayStep);
        m_lastReplaySigned = pending > 0 ? m_lastReplayDelta : -m_lastReplayDelta;
        if (m_throttle->replay(m_replayStep))
            beginAwaiting();
        debugLog(QStringLiteral("replay %1 of %2 (step %3)").arg(m_lastReplaySigned).arg(pending).arg(m_replayStep));
    }
    updateStatus();
}

void ScrollCaptureSession::beginAwaiting()
{
    m_awaitingResponse = true;
    m_motionSinceReplay = false;
    m_sinceReplay.restart();
}

void ScrollCaptureSession::recoverFromNoMatch()
{
    // 这一步滚得太远、和已拼接内容对不上：退回这一步，步长减半后重新分步滚过去
    if (m_recovering || m_lastReplaySigned == 0 || m_replayStep <= kMinReplayStep) {
        // 退回去也对不上，或步长已是最小：停止自动回放，提示用户往回滚
        debugLog(QStringLiteral("give up, dropping %1").arg(m_throttle->pendingDelta()));
        m_throttle->clear();
        m_recovering = false;
        return;
    }
    m_throttle->requeue(m_lastReplaySigned);
    m_replayStep = qMax(kMinReplayStep, qAbs(m_lastReplaySigned) / 2);
    m_stepCeiling = m_replayStep; // 之后不再放大，避免反复失败
    if (m_throttle->inject(-m_lastReplaySigned)) {
        m_recovering = true;
        beginAwaiting();
        debugLog(QStringLiteral("no match: scroll back %1, step now %2").arg(-m_lastReplaySigned).arg(m_replayStep));
    }
    m_lastReplaySigned = 0;
}

void ScrollCaptureSession::debugLog(const QString& line, const QImage* frame)
{
    // 调试：设置环境变量 SNAPPIN_SCROLL_DEBUG=<目录> 后记录每一步并保存停稳的帧
    static const QString dir = qEnvironmentVariable("SNAPPIN_SCROLL_DEBUG");
    if (dir.isEmpty())
        return;
    static QElapsedTimer clock;
    static int frameNo = 0;
    if (!clock.isValid())
        clock.start();
    QString text = QStringLiteral("%1 ms  %2").arg(clock.elapsed(), 6).arg(line);
    if (frame) {
        const QString name = QStringLiteral("f%1.png").arg(frameNo++, 3, 10, QLatin1Char('0'));
        frame->save(QDir(dir).filePath(name));
        text += QStringLiteral("  [%1]").arg(name);
    }
    QFile f(QDir(dir).filePath(QStringLiteral("scroll.log")));
    if (f.open(QIODevice::Append | QIODevice::Text))
        f.write(text.toUtf8() + '\n');
}

void ScrollCaptureSession::adaptReplayStep()
{
    // 让每一步的滚动量不超过可视高度的 40%，保证前后两帧有足够重叠
    const int px = qAbs(m_stitcher.lastScroll());
    const int viewH = m_stitcher.currentView().height();
    if (px <= 0 || m_lastReplayDelta <= 0 || viewH <= 0)
        return;
    const double pxPerUnit = double(px) / m_lastReplayDelta;
    m_replayStep = qBound(kMinReplayStep, int(viewH * 0.4 / pxPerUnit), m_stepCeiling);
}

void ScrollCaptureSession::updateStatus()
{
    QString text = m_statusBase;
    const int pending = m_throttleActive ? qAbs(m_throttle->pendingDelta()) : 0;
    if (pending > 0)
        text += tr("\n正在跟上滚动，剩余 %1 格").arg((pending + kWheelNotch - 1) / kWheelNotch);
    else if (m_awaitingResponse)
        text += tr("\n正在跟上滚动…"); // 最后一步还在滚动中
    if (text != m_status->text())
        m_status->setText(text);
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

    m_statusBase = tr("已拼接 %1 px").arg(totalH);
    if (r == ScrollStitcher::Result::NoMatch)
        m_statusBase += tr("\n未能接上，请往回滚一点");
    else if (r == ScrollStitcher::Result::Full)
        m_statusBase += tr("\n已达到最大高度");
    updateStatus();
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
    m_throttle->stop();
    m_throttleActive = false;
    if (m_frame)
        m_frame->hide();
    if (m_panel)
        m_panel->hide();
    emit closed();
}
