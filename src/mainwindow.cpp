#include "mainwindow.h"
#include "globalhelper.h"
#include "videoctl.h"
#include <QHBoxLayout>
#include <QApplication>
#include <QWindow>

const int FULLSCREEN_MOUSE_DETECT_TIME = 500; // 全屏鼠标检测时间

MainWindow::MainWindow(QWidget *parent) : QWidget(parent),
                                          m_ShadowWidth(0)
{
    InitUi();
    ConnectSig();

    m_PlayingIndex = false;
    m_FullScreenPlayIndex = false;
    m_CtrlBarAnimationTimer.setInterval(2000);
    m_FullscreenMouseDetectTimer.setInterval(FULLSCREEN_MOUSE_DETECT_TIME);
}

MainWindow::~MainWindow()
{
    qDebug() << "~MainWindow finish";
}

void MainWindow::InitUi()
{
    this->setObjectName("MainWindow");
    this->setWindowFlags(Qt::FramelessWindowHint | Qt::WindowMinimizeButtonHint);
    this->setStyleSheet(GlobalHelper::GetQssStr(":/qss/mainwid.css"));
    this->setWindowIcon(QIcon(":/Player.svg"));
    // 追踪鼠标 用于播放时隐藏鼠标
    this->setMouseTracking(true);
    this->resize(960, 720);

    m_mainVLayout = new QVBoxLayout(this);
    m_mainVLayout->setSpacing(0);
    m_mainVLayout->setContentsMargins(0, 0, 0, 0);

    m_titleBar = new Title(this);
    m_mainVLayout->addWidget(m_titleBar);

    // 主体区域容器
    QWidget *bodyWidget = new QWidget(this);
    bodyLayout = new QHBoxLayout(bodyWidget);
    bodyLayout->setSpacing(0);
    bodyLayout->setContentsMargins(0, 0, 0, 0);

    // 左侧 视频+控制区域
    QWidget *videoCtrlWidget = new QWidget(bodyWidget);
    QVBoxLayout *videoLayout = new QVBoxLayout(videoCtrlWidget);
    videoLayout->setSpacing(0);
    videoLayout->setContentsMargins(0, 0, 0, 0);

    m_videoShow = new Show(videoCtrlWidget);
    m_ctlBar = new CtrBar(videoCtrlWidget);
    m_ctlBar->setFixedHeight(50);           // 控制栏设置固定高度，内部主动设置
    videoLayout->addWidget(m_videoShow, 1); // 视频占主要空间
    videoLayout->addWidget(m_ctlBar);

    bodyLayout->addWidget(videoCtrlWidget, 1); // 左侧占大部分空间

    // 右侧播放列表
    m_playList = new PlayList(bodyWidget);
    m_playList->setObjectName("PlaylistWid");
    bodyLayout->addWidget(m_playList);

    m_mainVLayout->addWidget(bodyWidget, 1);

    m_CtrlbarAnimationShow = new QPropertyAnimation(m_ctlBar, "geometry");
    m_CtrlbarAnimationHide = new QPropertyAnimation(m_ctlBar, "geometry");
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
}

void MainWindow::enterEvent(QEvent *event)
{
    Q_UNUSED(event);
}

void MainWindow::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        // 将鼠标位置转换为标题栏的本地坐标系
        const QPoint clickPos = m_titleBar->mapFromParent(event->pos());

        // 检查点击是否在标题栏范围内
        if (m_titleBar->rect().contains(clickPos))
        {
            m_bDrag = true;
            // 计算窗口左上角与鼠标全局位置的偏移
            m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_bDrag && (event->buttons() & Qt::LeftButton))
    {
        // 计算新窗口位置
        QPoint newPos = event->globalPosition().toPoint() - m_dragPosition;

        // 可选：限制窗口不移出屏幕
        newPos.setX(qMax(0, newPos.x()));
        newPos.setY(qMax(0, newPos.y()));

        move(newPos);
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_titleBar->rect().contains(m_titleBar->mapFromParent(event->pos())))
    {
        isMaximized() ? showNormal() : showMaximized();
        event->accept();
        m_titleBar->OnChangeMaxBtnStyle(isMaximized());
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_bDrag = false; // 重置拖动标志
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    m_Menu.popup(event->globalPos());
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    // qDebug() << "MainWindow event->key() = " << event->key();
    switch (event->key())
    {
    case Qt::Key_Return: // 全屏
        OnFullScreenPlay();
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
    {
        OnFullScreenPlay();
    }
    break;
    case Qt::Key_M:
        m_ctlBar->OnVolumeBtnClicked();
        break;

    case Qt::Key_S:
        if (VideoCtl::GetInstance()->m_playLoopIndex)
        {
            VideoCtl::GetInstance()->step_to_next_frame(VideoCtl::GetInstance()->m_CurVideoState);
        }
        break;
    case Qt::Key_A:
        if (VideoCtl::GetInstance()->m_playLoopIndex)
        {
            VideoCtl::GetInstance()->stream_cycle_channel(AVMEDIA_TYPE_AUDIO);
        }
        break;

    case Qt::Key_V:
        if (VideoCtl::GetInstance()->m_playLoopIndex)
        {
            VideoCtl::GetInstance()->stream_cycle_channel(AVMEDIA_TYPE_VIDEO);
        }
        break;

    case Qt::Key_C:
        if (VideoCtl::GetInstance()->m_playLoopIndex)
        {
            VideoCtl::GetInstance()->stream_cycle_channel(AVMEDIA_TYPE_AUDIO);
            VideoCtl::GetInstance()->stream_cycle_channel(AVMEDIA_TYPE_VIDEO);
            VideoCtl::GetInstance()->stream_cycle_channel(AVMEDIA_TYPE_SUBTITLE);
        }
        break;

    case Qt::Key_T:
        if (VideoCtl::GetInstance()->m_playLoopIndex)
        {
            VideoCtl::GetInstance()->stream_cycle_channel(AVMEDIA_TYPE_SUBTITLE);
        }
        break;

    case Qt::Key_W:
        if (VideoCtl::GetInstance()->m_playLoopIndex)
        {
            VideoState *cur_stream =  VideoCtl::GetInstance()->m_CurVideoState;
            if (cur_stream->show_mode == VideoState::ShowMode::SHOW_MODE_VIDEO && cur_stream->vfilter_idx < nb_vfilters - 1) {
                if (++cur_stream->vfilter_idx >= nb_vfilters)
                    cur_stream->vfilter_idx = 0;
            } else {
                cur_stream->vfilter_idx = 0;
                VideoCtl::GetInstance()->toggle_audio_display();
            }
            break;
        }
        break;

    default:
        break;
    }
}

void MainWindow::ConnectSig()
{
    connect(m_titleBar, &Title::sigMinBtnClicked, this, [=]()
            { this->showMinimized(); });

    connect(m_titleBar, &Title::sigMaxBtnClicked, this, [=]()
            {
        if (isMaximized())
        {
            showNormal();
            emit sigShowMax(false);
            m_titleBar->OnChangeMaxBtnStyle(false);
        }
        else
        {
            showMaximized();
            emit sigShowMax(true);
            m_titleBar->OnChangeMaxBtnStyle(true);
        } });

    connect(m_titleBar, &Title::sigFullScreenBtnClicked, this, &MainWindow::OnFullScreenPlay);

    connect(m_titleBar, &Title::sigCloseBtnClicked, this, [=]()
            { this->close(); });

    connect(m_titleBar, &Title::sigShowMenu, this, [=]()
            { m_Menu.exec(cursor().pos()); });

    connect(m_titleBar, &Title::sigOpenFile, m_playList, &PlayList::OnAddFileAndPlay);

    connect(m_playList, &PlayList::sigPlay, m_videoShow, &Show::SigPlay); // 更新完标题栏再进行解码播放，因为解码播放会阻塞主线程

    connect(m_ctlBar, &CtrBar::SigShowOrHidePlaylist, this, &MainWindow::OnShowOrHidePlaylist);
    connect(m_ctlBar, &CtrBar::SigPlaySeek, VideoCtl::GetInstance(), &VideoCtl::OnPlaySeek);
    connect(m_ctlBar, &CtrBar::SigPlayVolume, VideoCtl::GetInstance(), &VideoCtl::OnPlayVolume);
    connect(m_ctlBar, &CtrBar::SigPlayOrPause, VideoCtl::GetInstance(), &VideoCtl::OnPause);
    connect(m_ctlBar, &CtrBar::SigStop, VideoCtl::GetInstance(), &VideoCtl::OnStop);

    connect(m_ctlBar, &CtrBar::SigForwardPlay, m_playList, &PlayList::OnForwardPlay);
    connect(m_ctlBar, &CtrBar::SigBackwardPlay, m_playList, &PlayList::OnBackwardPlay);

    connect(this, &MainWindow::SigSeekForward, VideoCtl::GetInstance(), &VideoCtl::OnSeekForward);
    connect(this, &MainWindow::SigSeekBack, VideoCtl::GetInstance(), &VideoCtl::OnSeekBack);
    connect(this, &MainWindow::SigAddVolume, VideoCtl::GetInstance(), &VideoCtl::OnAddVolume);
    connect(this, &MainWindow::SigSubVolume, VideoCtl::GetInstance(), &VideoCtl::OnSubVolume);
    connect(this, &MainWindow::SigPlayOrPause, VideoCtl::GetInstance(), &VideoCtl::OnPause);

    // Qt::DirectConnection 和 Qt::QueuedConnection 的主要区别在于槽函数的执行时机和线程关系。
    // Qt::DirectConnection 适用于同一线程中的信号与槽连接，而 Qt::QueuedConnection 适用于跨线程的信号与槽连接。
    connect(VideoCtl::GetInstance(), &VideoCtl::SigStartPlay, m_titleBar, &Title::OnPlay, Qt::DirectConnection);
    connect(VideoCtl::GetInstance(), &VideoCtl::SigVideoTotalSeconds, m_ctlBar, &CtrBar::OnVideoTotalSeconds);
    connect(VideoCtl::GetInstance(), &VideoCtl::SigVideoPlaySeconds, m_ctlBar, &CtrBar::OnVideoPlaySeconds);
    connect(VideoCtl::GetInstance(), &VideoCtl::SigVideoVolume, m_ctlBar, &CtrBar::OnVideopVolume);
    connect(VideoCtl::GetInstance(), &VideoCtl::SigPauseStat, m_ctlBar, &CtrBar::OnPauseStat, Qt::QueuedConnection);
    connect(VideoCtl::GetInstance(), &VideoCtl::SigVideoTotalSeconds, m_ctlBar, &CtrBar::OnVideoTotalSeconds);
    connect(VideoCtl::GetInstance(), &VideoCtl::SigVideoPlaySeconds, m_ctlBar, &CtrBar::OnVideoPlaySeconds);
    connect(VideoCtl::GetInstance(), &VideoCtl::SigVideoVolume, m_ctlBar, &CtrBar::OnVideopVolume);
    connect(VideoCtl::GetInstance(), &VideoCtl::SigStopFinished, m_ctlBar, &CtrBar::OnStopFinished, Qt::QueuedConnection);
    connect(VideoCtl::GetInstance(), &VideoCtl::SigStopAndNext, m_playList, &PlayList::OnForwardPlay, Qt::DirectConnection);
    connect(VideoCtl::GetInstance(), &VideoCtl::SigFrameDimensionsChanged, m_videoShow, &Show::OnFrameDimensionsChanged, Qt::QueuedConnection);

    connect(m_videoShow, &Show::SigStop, VideoCtl::GetInstance(), &VideoCtl::OnStop);
    connect(m_videoShow, &Show::SigOpenFile, m_playList, &PlayList::OnAddFileAndPlay);

    connect(m_videoShow, &Show::SigFullScreen, this, &MainWindow::OnFullScreenPlay);
    connect(m_videoShow, &Show::SigSeekForward, VideoCtl::GetInstance(), &VideoCtl::OnSeekForward);
    connect(m_videoShow, &Show::SigSeekBack, VideoCtl::GetInstance(), &VideoCtl::OnSeekBack);
    connect(m_videoShow, &Show::SigAddVolume, VideoCtl::GetInstance(), &VideoCtl::OnAddVolume);
    connect(m_videoShow, &Show::SigSubVolume, VideoCtl::GetInstance(), &VideoCtl::OnSubVolume);
    connect(m_videoShow, &Show::SigPlayOrPause, VideoCtl::GetInstance(), &VideoCtl::OnPause);
}

void MainWindow::OnCtrlBarAnimationTimeOut()
{
    QApplication::setOverrideCursor(Qt::BlankCursor);
}

void MainWindow::OnFullscreenMouseDetectTimeOut()
{
    if (m_FullScreenPlayIndex)
    {
        if (m_CtrlBarAnimationShow.contains(cursor().pos()))
        {
            // 判断鼠标是否在控制面板上面
            if (m_ctlBar->geometry().contains(cursor().pos()))
            {
                // 继续显示
                m_FullScreenPlayIndex = true;
            }
            else
            {
                // 需要显示
                m_ctlBar->raise();

                m_CtrlbarAnimationShow->start();
                m_CtrlbarAnimationHide->stop();
                CtrlBarHideTimer.stop();
            }
        }
        else
        {
            if (m_FullscreenCtrlBarShow)
            {
                // 需要隐藏
                m_FullscreenCtrlBarShow = false;
                CtrlBarHideTimer.singleShot(2000, this, &MainWindow::OnCtrlBarHideTimeOut);
            }
        }
    }
}

void MainWindow::OnCtrlBarHideTimeOut()
{
    if (m_FullScreenPlayIndex)
    {
        m_CtrlbarAnimationHide->start();
    }
}

void MainWindow::OnFullScreenPlay()
{
    if (m_FullScreenPlayIndex == false)
    {
        m_FullScreenPlayIndex = true;
        m_ActFullscreen.setChecked(true);
        m_videoShow->setWindowFlag(Qt::Window);
        QScreen *pCurScreen = screen();
        m_videoShow->windowHandle()->setScreen(pCurScreen);

        m_videoShow->showFullScreen();

        QRect ScreenRect = pCurScreen->geometry();
        int CtrBarHeight = m_ctlBar->height();
        int nX = m_videoShow->x();
        m_CtrlBarAnimationShow = QRect(nX, ScreenRect.height() - CtrBarHeight, ScreenRect.width(), CtrBarHeight);
        m_CtrlBarAnimationHide = QRect(nX, ScreenRect.height(), ScreenRect.width(), CtrBarHeight);

        m_CtrlbarAnimationShow->setStartValue(m_CtrlBarAnimationHide);
        m_CtrlbarAnimationShow->setEndValue(m_CtrlBarAnimationShow);
        m_CtrlbarAnimationShow->setDuration(1000);

        m_CtrlbarAnimationHide->setStartValue(m_CtrlBarAnimationShow);
        m_CtrlbarAnimationHide->setEndValue(m_CtrlBarAnimationHide);
        m_CtrlbarAnimationHide->setDuration(1000);

        m_ctlBar->setWindowFlags(Qt::FramelessWindowHint | Qt::Window);
        m_ctlBar->windowHandle()->setScreen(pCurScreen);
        m_ctlBar->raise();
        m_ctlBar->setWindowOpacity(0.5);
        m_ctlBar->showNormal();
        m_ctlBar->windowHandle()->setScreen(pCurScreen);

        m_CtrlbarAnimationShow->start();
        m_FullscreenCtrlBarShow = true;
        m_FullscreenMouseDetectTimer.start();

        m_videoShow->setFocus();
    }
    else
    {
        m_FullScreenPlayIndex = false;
        m_ActFullscreen.setChecked(false);

        m_CtrlbarAnimationShow->stop();
        m_CtrlbarAnimationHide->stop();
        m_ctlBar->setWindowOpacity(1);
        m_ctlBar->setWindowFlags(Qt::SubWindow);

        m_videoShow->setWindowFlags(Qt::SubWindow);

        m_ctlBar->showNormal();
        m_videoShow->showNormal();

        m_FullscreenMouseDetectTimer.stop();
        this->setFocus();
    }
}

void MainWindow::OnShowOrHidePlaylist()
{
    bool isVisible = m_playList->isVisible();
    m_playList->setVisible(!isVisible);
}