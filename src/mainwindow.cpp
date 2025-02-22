#include "mainwindow.h"
#include "globalhelper.h"
#include "videoctl.h"
#include <QHBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QWidget(parent)
{
    InitUi();
    ConnectSig();
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
    this->setMinimumSize(640, 480);

    m_mainVLayout = new QVBoxLayout(this);
    m_mainVLayout->setSpacing(0);
    m_mainVLayout->setContentsMargins(0, 0, 0, 0);
    
    m_titleBar = new Title(this);
    m_mainVLayout->addWidget(m_titleBar);

    // 主体区域容器
    QWidget *bodyWidget = new QWidget(this);
    QHBoxLayout *bodyLayout = new QHBoxLayout(bodyWidget);
    bodyLayout->setSpacing(0);
    bodyLayout->setContentsMargins(0, 0, 0, 0);

    // 左侧 视频+控制区域
    QWidget *videoCtrlWidget = new QWidget(bodyWidget);
    QVBoxLayout *videoLayout = new QVBoxLayout(videoCtrlWidget);
    videoLayout->setSpacing(0);
    videoLayout->setContentsMargins(0, 0, 0, 0);

    m_videoShow = new Show(videoCtrlWidget);
    m_ctlBar = new CtrBar(videoCtrlWidget);
    m_ctlBar->setFixedHeight(50);   //控制栏设置固定高度，内部主动设置
    videoLayout->addWidget(m_videoShow, 1);     //视频占主要空间
    videoLayout->addWidget(m_ctlBar);

    bodyLayout->addWidget(videoCtrlWidget, 1);  //左侧占大部分空间

    // 右侧播放列表
    m_playList = new PlayList(bodyWidget);
    m_playList->setObjectName("PlaylistWid");
    bodyLayout->addWidget(m_playList);

    m_mainVLayout->addWidget(bodyWidget, 1);
}

void MainWindow::mousePressEvent(QMouseEvent *event)
{
    if(event->button() == Qt::LeftButton)
    {
        // 将鼠标位置转换为标题栏的本地坐标系
        const QPoint clickPos = m_titleBar->mapFromParent(event->pos());
        
        // 检查点击是否在标题栏范围内
        if(m_titleBar->rect().contains(clickPos))
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
    if(m_bDrag && (event->buttons() & Qt::LeftButton))
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
    if(m_titleBar->rect().contains(m_titleBar->mapFromParent(event->pos())))
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
    if(event->button() == Qt::LeftButton)
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
    qDebug() << "MainWindow::keyEvent:" << event->key();
	switch (event->key())
	{
	case Qt::Key_Return://全屏
        OnFullScreenPlay();
		break;
    case Qt::Key_Left://后退5s
        emit SigSeekBack();
        break;
    case Qt::Key_Right://前进5s
        qDebug() << "前进5s";
        emit SigSeekForward();
        break;
    case Qt::Key_Up://增加10音量
        emit SigAddVolume();
        break;
    case Qt::Key_Down://减少10音量
        emit SigSubVolume();
        break;
    case Qt::Key_Space://减少10音量
        emit SigPlayOrPause();
        break;
    case Qt::Key_Escape:
        {
            if (isMaximized() || isFullScreen())
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
            }
        }
        break;    
	default:
		break;
	}
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if(m_videoShow && m_videoShow->videoctl)
    {
        m_videoShow->videoctl->StopPlay();
    }

    event->accept();
}

void MainWindow::ConnectSig()
{
    connect(m_titleBar, &Title::sigMinBtnClicked, this, [=](){
        this->showMinimized();
    });

    connect(m_titleBar, &Title::sigMaxBtnClicked, this, [=](){
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
        }
    });

    connect(m_titleBar, &Title::sigFullScreenBtnClicked, this, &MainWindow::OnFullScreenPlay);

    connect(m_titleBar, &Title::sigCloseBtnClicked, this, [=](){
        this->close();
    });

    connect(m_titleBar, &Title::sigShowMenu, this, [=](){
        m_Menu.exec(cursor().pos());
    });

    connect(m_titleBar, &Title::sigOpenFile, m_playList, &PlayList::OnAddFileAndPlay);

    connect(m_playList, &PlayList::sigPlay, m_titleBar, &Title::OnPlay);
    connect(m_playList, &PlayList::sigPlay, m_videoShow, &Show::SigPlay);   //更新完标题栏再进行解码播放，因为解码播放会阻塞主线程
    
    connect(m_ctlBar, &CtrBar::SigShowOrHidePlaylist, this, &MainWindow::OnShowOrHidePlaylist);
    connect(m_ctlBar, &CtrBar::SigPlaySeek, m_videoShow, &Show::OnPlaySeek);
    connect(m_ctlBar, &CtrBar::SigPlayVolume, m_videoShow, &Show::OnPlayVolume);
    connect(m_ctlBar, &CtrBar::SigPlayOrPause, m_videoShow, &Show::OnPause);
    //connect(m_ctlBar, &CtrBar::SigStop, m_videoShow, &Show::OnStop);  //存在问题，需要处理资源释放

    connect(m_videoShow, &Show::SigPauseStat, m_ctlBar, &CtrBar::OnPauseStat);

    connect(this, &MainWindow::SigSeekForward, m_videoShow, &Show::OnSeekForward);
    connect(this, &MainWindow::SigSeekBack, m_videoShow, &Show::OnSeekBack);
    connect(this, &MainWindow::SigAddVolume, m_videoShow, &Show::OnAddVolume);
    connect(this, &MainWindow::SigSubVolume, m_videoShow, &Show::OnSubVolume);

    connect(m_videoShow, &Show::SigVideoVolume, m_ctlBar, &CtrBar::OnVideopVolume);

    connect(m_videoShow, &Show::SigVideoTotalSeconds, m_ctlBar, &CtrBar::OnVideoTotalSeconds);
}

void MainWindow::togglePlaylist(bool visible)
{

}

void MainWindow::OnShowOrHidePlaylist()
{
    bool isVisible = m_playList->isVisible();
    m_playList->setVisible(!isVisible);
}

void MainWindow::OnFullScreenPlay()
{
    bool isFull = this->isFullScreen();
    if(isFull)
    {
        this->showNormal();
        this->m_playList->setVisible(true);
    }
    else 
    {
        this->showFullScreen();
        this->m_playList->setVisible(false);
    }
}