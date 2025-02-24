#include "show.h"
#include "globalhelper.h"
#include <QHBoxLayout>
#include <QMutex>
#include <QFileInfo>

QMutex g_show_rect_mutex;

Show::Show(QWidget *parent) : QWidget(parent),
                              m_actionGroup(this),
                              m_menu(this)
{
    m_lastFrameHeight = 0;
    m_lastFrameWidth = 0;

    InitUi();
    Init();
}

Show::~Show()
{
}

bool Show::Init()
{
    if (ConnectSignalSlots() == false)
    {
        return false;
    }

    return true;
}

void Show::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (mimeData->hasUrls())
    {
        QList<QUrl> urls = mimeData->urls();
        foreach (const QUrl &url, urls)
        {
            QString filePath = url.toLocalFile();
            QFileInfo fileInfo(filePath);

            QStringList allowedExtensions = {"3gp", "wmv", "flv", "rmvb", "mp4", "avi", "mkv", "mov", "mp3", "wav", "flac"}; // 允许的扩展名

            // 再次验证（防止拖放过程中文件被篡改）
            if (fileInfo.isFile() &&
                allowedExtensions.contains(fileInfo.suffix().toLower()))
            {
                qDebug() << "已接受音视频文件:" << filePath;
                emit SigOpenFile(filePath);
            }
        }
        event->acceptProposedAction();
    }
}

void Show::dragEnterEvent(QDragEnterEvent *event)
{
    const QMimeData *mimeData = event->mimeData();

    if (mimeData->hasUrls())
    { // 检查是否有拖入的URL（文件/链接）
        QList<QUrl> urls = mimeData->urls();
        QStringList allowedExtensions = {"3gp", "wmv", "flv", "rmvb", "mp4", "avi", "mkv", "mov", "mp3", "wav", "flac"}; // 允许的扩展名

        for (const QUrl &url : urls)
        {
            QString filePath = url.toLocalFile(); // 转换为本地文件路径
            QFileInfo fileInfo(filePath);

            // 检查是否为文件且扩展名合法
            if (!fileInfo.isFile() ||
                !allowedExtensions.contains(fileInfo.suffix().toLower()))
            {
                return; // 拒绝拖放
            }
        }

        event->acceptProposedAction(); // 全部文件合法，接受拖放
    }
}

void Show::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);

    //ChangeShow();
}

void Show::mousePressEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::RightButton)
    {
        emit SigShowMenu();
    }

    QWidget::mousePressEvent(event);
}

void Show::contextMenuEvent(QContextMenuEvent *event)
{
    m_menu.popup(event->globalPos());
}

void Show::keyReleaseEvent(QKeyEvent *event)
{
    qDebug() << "Show event->key() = " << event->key();
    switch (event->key())
    {
    case Qt::Key_Return: // 全屏
        emit SigFullScreen();
        break;
    case Qt::Key_Left: // 后退5s
        emit SigSeekBack();
        break;
    case Qt::Key_Right: // 前进5s
        qDebug() << "前进5s";
        emit SigSeekForward();
        break;
    case Qt::Key_Up: // 增加10音量
        emit SigAddVolume();
        break;
    case Qt::Key_Down: // 减少10音量
        emit SigSubVolume();
        break;
    case Qt::Key_Space: // 减少10音量
        emit SigPlayOrPause();
        break;
    case Qt::Key_Escape:
        emit SigFullScreen();
        break;
    default:
        break;
    }
}

void Show::InitUi()
{
    this->resize(640, 480);
    this->setStyleSheet(GlobalHelper::GetQssStr(":/qss/show.css"));
    this->setAcceptDrops(true);
    // 防止过度刷新显示
    this->setAttribute(Qt::WA_OpaquePaintEvent);
    this->setMouseTracking(true);

    QVBoxLayout *pVlayout = new QVBoxLayout(this);
    pVlayout->setSpacing(0);
    pVlayout->setContentsMargins(0, 0, 0, 0);

    m_video = new QLabel(this);
    m_video->resize(this->width(), this->height());
    m_video->setGeometry(QRect(0, 0, 640, 480));
    m_video->setUpdatesEnabled(false);
    m_video->setAlignment(Qt::AlignCenter);
    //m_video->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    pVlayout->addWidget(m_video);

    m_actionGroup.addAction(u8"全屏");
    m_actionGroup.addAction(u8"暂停");
    m_actionGroup.addAction(u8"停止");

    m_menu.addActions(m_actionGroup.actions());
}

bool Show::ConnectSignalSlots()
{
    QList<bool> listRet;
    bool bRet;

    bRet = connect(this, &Show::SigPlay, this, &Show::OnPlay);
    listRet.append(bRet);

    timerShowCur.setInterval(2000);
    bRet = connect(&timerShowCur, &QTimer::timeout, this, &Show::OnTimerShowCursorUpdate);
    listRet.append(bRet);

    connect(&m_actionGroup, &QActionGroup::triggered, this, &Show::OnActionsTriggered);

    for (bool bReturn : listRet)
    {
        if (bReturn == false)
        {
            return false;
        }
    }

    return true;
}

void Show::ChangeShow()
{
    QMutexLocker locker(&g_show_rect_mutex); // 自动管理锁

    if (m_lastFrameWidth == 0 && m_lastFrameHeight == 0)
    {
        m_video->setGeometry(0, 0, width(), height());
        qDebug() << "0 0 " << width() << " " << height() << '\n';
        return;
    }
    float videoAspectRatio = static_cast<float>(m_lastFrameWidth) / static_cast<float>(m_lastFrameHeight);
    int containerWidth = this->width();
    int containerHeight = this->height();
    float containerAspectRatio = static_cast<float>(containerWidth) / static_cast<float>(containerHeight);

    int displayWidth, displayHeight, offsetX, offsetY;

    if (videoAspectRatio > containerAspectRatio)
    {
        displayWidth = containerWidth;
        displayHeight = static_cast<int>(containerWidth / videoAspectRatio);
        offsetX = 0;
        offsetY = (containerHeight - displayHeight) / 2;
    }
    else
    {
        displayHeight = containerHeight;
        displayWidth = static_cast<int>(containerHeight * videoAspectRatio);
        offsetX = (containerWidth - displayWidth) / 2;
        offsetY = 0;
    }
    m_video->setGeometry(offsetX, offsetY, displayWidth, displayHeight);

}

void Show::OnPlay(QString strFile)
{
    VideoCtl::GetInstance()->StartPlay(strFile, m_video->winId());
}

void Show::OnStopFinished()
{
    update();
}

void Show::OnFrameDimensionsChanged(int FrameWidth, int FrameHeight)
{
    qDebug() << "Show::OnFrameDimensionsChanged" << FrameWidth << ", " << FrameHeight;
    m_lastFrameWidth = FrameWidth;
    m_lastFrameHeight = FrameHeight;

    //ChangeShow();
}

void Show::OnDisplayMsg(QString strMsg)
{
    qDebug() << "Show::OnDisplayMsg " << strMsg;
}

void Show::OnTimerShowCursorUpdate()
{
    qDebug() << "Show::OnTimerShowCursorUpdate()";
    this->setCursor(Qt::BlankCursor);
}

void Show::OnActionsTriggered(QAction *action)
{
    QString strAction = action->text();
    if (strAction == "全屏")
    {
        emit SigFullScreen();
    }
    else if (strAction == "停止")
    {
        emit SigStop();
    }
    else if (strAction == "暂停" || strAction == "播放")
    {
        emit SigPlayOrPause();
    }
}
