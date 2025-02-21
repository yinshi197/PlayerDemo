#include "show.h"
#include "globalhelper.h"
#include <QHBoxLayout>
#include <QMutex>

QMutex g_show_rect_mutex;

Show::Show(QWidget *parent) : QWidget(parent),
                              m_actionGroup(this),
                              m_menu(this)
{
    InitUi();
    ConnectSig();
    m_lastFrameHeight = 0;
    m_lastFrameWidth = 0;
}

Show::~Show()
{
    videoctl->StopPlay();
    delete videoctl;
}

void Show::OnPlay(QString strFile)
{
    if(videoctl)
    {
        delete videoctl;
    }

    videoctl = new VideoCtl();
    videoctl->StartPlay(strFile, m_video->winId());
}

void Show::InitUi()
{
    this->setMinimumSize(640, 480);
    this->setStyleSheet(GlobalHelper::GetQssStr(":/qss/show.css"));
    this->setAcceptDrops(true);
    // 防止过度刷新显示
    this->setAttribute(Qt::WA_OpaquePaintEvent);
    this->setMouseTracking(true);

    m_video = new QLabel(this);
    m_video->setUpdatesEnabled(true);
    m_video->setAlignment(Qt::AlignCenter);

    QHBoxLayout *hMainLay = new QHBoxLayout(this);
    hMainLay->setContentsMargins(0, 0, 0, 0);
    hMainLay->addWidget(m_video, 1);

    m_actionGroup.addAction(u8"全屏");
    m_actionGroup.addAction(u8"暂停");
    m_actionGroup.addAction(u8"停止");

    m_menu.addActions(m_actionGroup.actions());
}

void Show::ConnectSig()
{
    connect(this, &Show::SigPlay, this, &Show::OnPlay);
}
