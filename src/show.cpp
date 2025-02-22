#include "show.h"
#include "globalhelper.h"
#include <QHBoxLayout>
#include <QMutex>

QMutex g_show_rect_mutex;

Show::Show(QWidget *parent) : QWidget(parent),
                              m_actionGroup(this),
                              m_menu(this)
{
    m_lastFrameHeight = 0;
    m_lastFrameWidth = 0;
    videoctl = new VideoCtl(this);

    InitUi();
    ConnectSig();
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

    emit SigPauseStat(false);
    videoctl->StartPlay(strFile, m_video->winId());
    
    if(videoctl->m_CurVideoState->duration > 0)
    qDebug() << "before";
    emit SigVideoTotalSeconds(videoctl->m_CurVideoState->duration);
    qDebug() << "duration = " << videoctl->m_CurVideoState->duration;
}

void Show::contextMenuEvent(QContextMenuEvent *event)
{
    m_menu.popup(event->globalPos());
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
    connect(videoctl, &VideoCtl::SigVideoTotalSeconds, this, [=](int nSeconds){
        qDebug() << "nSeconds = " << nSeconds; 
    });
}

void Show::OnPlaySeek(double dPercent)
{
    if(videoctl == nullptr || videoctl->m_CurVideoState == nullptr) return;
    int64_t ts = dPercent * videoctl->m_CurVideoState->ic->duration;
    if (videoctl->m_CurVideoState->ic->start_time != AV_NOPTS_VALUE)
        ts += videoctl->m_CurVideoState->ic->start_time;
    VideoCtl::stream_seek(videoctl->m_CurVideoState, ts, 0, 0);
}

void Show::OnPlayVolume(double dPercent)
{
    if(videoctl == nullptr || videoctl->m_CurVideoState == nullptr) return;
    int volume = dPercent * SDL_MIX_MAXVOLUME;
    videoctl->m_CurVideoState->audio.audio_volume = volume;
}

void Show::OnSeekForward()
{
    if(videoctl == nullptr || videoctl->m_CurVideoState == nullptr) return;

    double incr = 5.0;
    double pos = VideoCtl::get_master_clock(videoctl->m_CurVideoState);
    if (std::isnan(pos))
        pos = (double)(videoctl->m_CurVideoState->seek_pos / AV_TIME_BASE);
    pos += incr;
    if (videoctl->m_CurVideoState->ic->start_time != AV_NOPTS_VALUE && pos < videoctl->m_CurVideoState->ic->start_time / (double)AV_TIME_BASE)
        pos = videoctl->m_CurVideoState->ic->start_time / (double)AV_TIME_BASE;
    VideoCtl::stream_seek(videoctl->m_CurVideoState, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
}

void Show::OnSeekBack()
{
    if(videoctl == nullptr || videoctl->m_CurVideoState == nullptr) return;

    double incr = -5.0;
    double pos = VideoCtl::get_master_clock(videoctl->m_CurVideoState);
    if (std::isnan(pos))
        pos = (double)(videoctl->m_CurVideoState->seek_pos / AV_TIME_BASE);
    pos += incr;
    if (videoctl->m_CurVideoState->ic->start_time != AV_NOPTS_VALUE && pos < videoctl->m_CurVideoState->ic->start_time / (double)AV_TIME_BASE)
        pos = videoctl->m_CurVideoState->ic->start_time / (double)AV_TIME_BASE;
    VideoCtl::stream_seek(videoctl->m_CurVideoState, (int64_t)(pos * AV_TIME_BASE), (int64_t)(incr * AV_TIME_BASE), 0);
}

void Show::OnAddVolume()
{
    if(videoctl == nullptr || videoctl->m_CurVideoState == nullptr) return;

    UpdateVolume(1, SDL_VOLUME_STEP);
}

void Show::OnSubVolume()
{
    if(videoctl == nullptr || videoctl->m_CurVideoState == nullptr) return;

    UpdateVolume(-1, SDL_VOLUME_STEP);
}

void Show::OnPause()
{
    if(videoctl == nullptr || videoctl->m_CurVideoState == nullptr) return;
    VideoCtl::toggle_pause(videoctl->m_CurVideoState);
    emit SigPauseStat(videoctl->m_CurVideoState->paused);
}

void Show::OnStop()
{
    if(videoctl == nullptr || videoctl->m_CurVideoState == nullptr) return;
    videoctl->do_exit();
    delete videoctl;
}

void Show::UpdateVolume(int sign, double step)
{
    if(videoctl == nullptr || videoctl->m_CurVideoState == nullptr) return;
    double volume_level = videoctl->m_CurVideoState->audio.audio_volume ? (20 * log(videoctl->m_CurVideoState->audio.audio_volume / (double)SDL_MIX_MAXVOLUME) / log(10)) : -1000.0;
    int new_volume = lrint(SDL_MIX_MAXVOLUME * pow(10.0, (volume_level + sign * step) / 20.0));
    videoctl->m_CurVideoState->audio.audio_volume = av_clip(videoctl->m_CurVideoState->audio.audio_volume == new_volume ? (videoctl->m_CurVideoState->audio.audio_volume + sign) : new_volume, 0, SDL_MIX_MAXVOLUME);
    
    emit SigVideoVolume(videoctl->m_CurVideoState->audio.audio_volume * 1.0 / SDL_MIX_MAXVOLUME);
}
