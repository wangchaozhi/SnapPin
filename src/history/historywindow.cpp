#include "historywindow.h"

#include "app/imageutil.h"
#include "historystore.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QImageReader>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {
constexpr int kThumb = 160;
}

HistoryWindow::HistoryWindow(HistoryStore* store, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_store(store)
    , m_list(new QListWidget(this))
{
    setWindowTitle(tr("截图历史 - SnapPin"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(760, 520);

    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(kThumb, kThumb));
    m_list->setGridSize(QSize(kThumb + 24, kThumb + 40));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setUniformItemSizes(true);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &HistoryWindow::pinCurrent);
    connect(m_list, &QListWidget::customContextMenuRequested, this, &HistoryWindow::showMenu);

    auto* pinBtn = new QPushButton(tr("贴图"), this);
    auto* copyBtn = new QPushButton(tr("复制"), this);
    auto* saveBtn = new QPushButton(tr("另存为..."), this);
    auto* deleteBtn = new QPushButton(tr("删除"), this);
    auto* clearBtn = new QPushButton(tr("清空"), this);
    auto* folderBtn = new QPushButton(tr("打开文件夹"), this);
    connect(pinBtn, &QPushButton::clicked, this, &HistoryWindow::pinCurrent);
    connect(copyBtn, &QPushButton::clicked, this, &HistoryWindow::copyCurrent);
    connect(saveBtn, &QPushButton::clicked, this, &HistoryWindow::saveCurrent);
    connect(deleteBtn, &QPushButton::clicked, this, &HistoryWindow::deleteCurrent);
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, tr("清空历史"), tr("删除全部截图历史？")) == QMessageBox::Yes)
            m_store->clear();
    });
    connect(folderBtn, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_store->directory()));
    });

    auto* buttons = new QHBoxLayout;
    for (QPushButton* b : {pinBtn, copyBtn, saveBtn, deleteBtn})
        buttons->addWidget(b);
    buttons->addStretch();
    buttons->addWidget(clearBtn);
    buttons->addWidget(folderBtn);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_list);
    layout->addLayout(buttons);

    connect(m_store, &HistoryStore::changed, this, &HistoryWindow::reload);
    reload();
}

void HistoryWindow::reload()
{
    m_list->clear();
    for (const QFileInfo& fi : m_store->entries()) {
        QImageReader reader(fi.absoluteFilePath());
        const QSize full = reader.size();
        if (full.isValid())
            reader.setScaledSize(full.scaled(kThumb, kThumb, Qt::KeepAspectRatio).boundedTo(full));
        auto* item = new QListWidgetItem(QIcon(QPixmap::fromImage(reader.read())),
                                         fi.lastModified().toString(QStringLiteral("MM-dd HH:mm:ss")), m_list);
        item->setData(Qt::UserRole, fi.absoluteFilePath());
        item->setToolTip(QStringLiteral("%1 × %2").arg(full.width()).arg(full.height()));
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
}

QString HistoryWindow::currentPath() const
{
    const QListWidgetItem* item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void HistoryWindow::pinCurrent()
{
    const QString path = currentPath();
    if (!path.isEmpty())
        emit pinRequested(QImage(path));
}

void HistoryWindow::copyCurrent()
{
    const QString path = currentPath();
    if (!path.isEmpty())
        QGuiApplication::clipboard()->setImage(QImage(path));
}

void HistoryWindow::saveCurrent()
{
    const QString path = currentPath();
    if (!path.isEmpty())
        saveImageInteractive(QImage(path), this);
}

void HistoryWindow::deleteCurrent()
{
    const QString path = currentPath();
    if (!path.isEmpty())
        m_store->remove(path);
}

void HistoryWindow::showMenu(const QPoint& pos)
{
    if (!m_list->itemAt(pos))
        return;
    QMenu menu(this);
    menu.addAction(tr("贴图"), this, &HistoryWindow::pinCurrent);
    menu.addAction(tr("复制"), this, &HistoryWindow::copyCurrent);
    menu.addAction(tr("另存为..."), this, &HistoryWindow::saveCurrent);
    menu.addSeparator();
    menu.addAction(tr("删除"), this, &HistoryWindow::deleteCurrent);
    menu.exec(m_list->viewport()->mapToGlobal(pos));
}
