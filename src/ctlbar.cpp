#include "ctlbar.h"
#include "globalhelper.h"
#include "videoctl.h"

CtrBar::CtrBar(QWidget *parent) : QWidget(parent)
{
    InitUi();
    ConnectSig();
    m_lastVolumePercent = 1.0;
}

CtrBar::~CtrBar()
{
}

void CtrBar::InitUi()
{
    // 设置窗口大小
    this->setFixedHeight(50);

    // 主布局
    mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    topWidget = new QWidget(this); // 顶部底层窗口
    topWidget->setStyleSheet("background: #202129");
    topWidget->setFixedHeight(25);

    sliderLayout = new QHBoxLayout(topWidget);
    sliderLayout->setSpacing(0);
    sliderLayout->setContentsMargins(0, 0, 0, 0);

    // 播放进度条
    playbackSlider = new CustomSlider(topWidget);
    playbackSlider->setMaximum(65536);
    playbackSlider->setFixedHeight(25);
    playbackSlider->setOrientation(Qt::Horizontal);

    sliderLayout->addWidget(playbackSlider, 1);
    sliderLayout->addSpacing(5);

    // 音量控制部分
    volumeControlWidget = new QWidget(topWidget);
    volumeControlWidget->setStyleSheet("background: #202129");

    volumeControlWidget->setFixedHeight(25);
    volumeControlWidget->setMinimumWidth(100);

    volumeLayout = new QHBoxLayout(volumeControlWidget);
    volumeLayout->setSpacing(0);
    volumeLayout->setContentsMargins(0, 0, 0, 0);

    volumeButton = new QPushButton(volumeControlWidget);
    volumeButton->setFixedSize(25, 25);
    volumeLayout->addWidget(volumeButton);
    volumeLayout->addSpacing(5);

    volumeSlider = new CustomSlider(volumeControlWidget);
    volumeSlider->setFixedSize(70, 25);
    volumeSlider->setOrientation(Qt::Horizontal);
    volumeLayout->addWidget(volumeSlider, 1);

    sliderLayout->addWidget(volumeControlWidget);

    mainLayout->addWidget(topWidget);

    // 控制按钮部分
    controlButtonsLayout = new QGridLayout();
    controlButtonsLayout->setSpacing(0);
    controlButtonsLayout->setContentsMargins(0, 0, 0, 0);

    timeSeparatorLabel = new QLabel(this);
    timeSeparatorLabel->setObjectName("TimeSplitLabel");
    timeSeparatorLabel->setFixedHeight(25);
    timeSeparatorLabel->setText("/");
    controlButtonsLayout->addWidget(timeSeparatorLabel, 0, 5, 1, 1);

    totalTimeDisplay = new QTimeEdit(this);
    totalTimeDisplay->setObjectName("VideoTotalTimeTimeEdit");
    totalTimeDisplay->setFixedHeight(25);
    totalTimeDisplay->setFrame(false);
    totalTimeDisplay->setAlignment(Qt::AlignLeading | Qt::AlignLeft | Qt::AlignVCenter);
    totalTimeDisplay->setReadOnly(true);
    totalTimeDisplay->setButtonSymbols(QAbstractSpinBox::NoButtons);
    totalTimeDisplay->setDisplayFormat("HH:mm:ss");
    controlButtonsLayout->addWidget(totalTimeDisplay, 0, 6, 1, 1);

    // 快进按钮
    forwardButton = new QPushButton(this);
    forwardButton->setFixedSize(25, 25);
    controlButtonsLayout->addWidget(forwardButton, 0, 3, 1, 1);

    // 快退按钮
    backwardButton = new QPushButton(this);
    backwardButton->setFixedSize(25, 25);
    controlButtonsLayout->addWidget(backwardButton, 0, 2, 1, 1);

    // 停止按钮
    stopButton = new QPushButton(this);
    stopButton->setFixedSize(25, 25);
    controlButtonsLayout->addWidget(stopButton, 0, 1, 1, 1);

    // 当前时间显示
    currentTimeDisplay = new QTimeEdit(this);
    currentTimeDisplay->setFixedHeight(25);
    currentTimeDisplay->setFrame(false);
    currentTimeDisplay->setAlignment(Qt::AlignRight | Qt::AlignTrailing | Qt::AlignVCenter);
    currentTimeDisplay->setReadOnly(true);
    currentTimeDisplay->setButtonSymbols(QAbstractSpinBox::NoButtons);
    currentTimeDisplay->setDisplayFormat("HH:mm:ss");
    controlButtonsLayout->addWidget(currentTimeDisplay, 0, 4, 1, 1);

    // 占位符
    spacer = new QSpacerItem(40, 25, QSizePolicy::Expanding, QSizePolicy::Minimum);
    controlButtonsLayout->addItem(spacer, 0, 7, 1, 1);

    // 播放/暂停按钮
    playPauseButton = new QPushButton(this);
    playPauseButton->setFixedSize(25, 25);
    controlButtonsLayout->addWidget(playPauseButton, 0, 0, 1, 1);

    // 设置按钮
    settingsButton = new QPushButton(this);
    settingsButton->setFixedSize(25, 25);
    controlButtonsLayout->addWidget(settingsButton, 0, 9, 1, 1);

    // 播放列表按钮
    playlistButton = new QPushButton(this);
    playlistButton->setFixedSize(25, 25);
    QFont font;
    font.setPointSize(25);
    playlistButton->setFont(font);
    playlistButton->setText("1");
    controlButtonsLayout->addWidget(playlistButton, 0, 8, 1, 1);

    // 将控制按钮布局添加到主布局
    mainLayout->addLayout(controlButtonsLayout, 1);

    this->setStyleSheet(GlobalHelper::GetQssStr(":/qss/ctrlbar.css"));

    GlobalHelper::SetIcon(playPauseButton, 15, QChar(0xf04b));
    GlobalHelper::SetIcon(stopButton, 15, QChar(0xf04d));
    GlobalHelper::SetIcon(volumeButton, 12, QChar(0xf028));
    GlobalHelper::SetIcon(playlistButton, 15, QChar(0xf036));
    GlobalHelper::SetIcon(forwardButton, 15, QChar(0xf051));
    GlobalHelper::SetIcon(backwardButton, 15, QChar(0xf048));
    GlobalHelper::SetIcon(settingsButton, 15, QChar(0xf013));

    playPauseButton->setToolTip(u8"暂停/播放");
    stopButton->setToolTip(u8"结束");
    volumeButton->setToolTip(u8"静音");
    playlistButton->setToolTip(u8"播放列表");
    forwardButton->setToolTip(u8"上一个");
    backwardButton->setToolTip(u8"下一个");
    settingsButton->setToolTip(u8"设置");

    double dPercent = -1.0;
    GlobalHelper::GetPlayVolume(dPercent);
    if (dPercent != -1.0)
    {
        emit SigPlayVolume(dPercent);
        OnVideopVolume(dPercent);
    }
}

void CtrBar::ConnectSig()
{
    connect(playlistButton, &QPushButton::clicked, this, &CtrBar::SigShowOrHidePlaylist);
    connect(playbackSlider, &CustomSlider::SigCustomSliderValueChanged, this, &CtrBar::OnPlaySliderValueChanged);
    connect(volumeSlider, &CustomSlider::SigCustomSliderValueChanged, this, &CtrBar::OnVolumeSliderValueChanged);
    connect(backwardButton, &QPushButton::clicked, this, &CtrBar::SigBackwardPlay);
    connect(forwardButton, &QPushButton::clicked, this, &CtrBar::SigForwardPlay);

    connect(volumeButton, &QPushButton::clicked, this, &CtrBar::OnVolumeBtnClicked);

    connect(playPauseButton, &QPushButton::clicked, this, [=](){
        if (VideoCtl::GetInstance()->m_playLoopIndex)
        {
            emit SigPlayOrPause(); 
        }
    });

    connect(stopButton, &QPushButton::clicked, this, [=](){ 
        if (VideoCtl::GetInstance()->m_playLoopIndex)
        {
            emit SigStop(); 
        } 
    });
}

void CtrBar::OnVideoTotalSeconds(int nSeconds)
{
    m_totalPlaySeconds = nSeconds;

    int thh, tmm, tss;
    thh = nSeconds / 3600;
    tmm = (nSeconds % 3600) / 60;
    tss = (nSeconds % 60);
    QTime TotalTime(thh, tmm, tss);

    totalTimeDisplay->setTime(TotalTime);
}

void CtrBar::OnVideoPlaySeconds(int nSeconds)
{
    int thh, tmm, tss;
    thh = nSeconds / 3600;
    tmm = (nSeconds % 3600) / 60;
    tss = (nSeconds % 60);
    QTime TotalTime(thh, tmm, tss);

    currentTimeDisplay->setTime(TotalTime);

    playbackSlider->setValue(nSeconds * 1.0 / m_totalPlaySeconds * MAX_SLIDER_VALUE);
}

void CtrBar::OnVideopVolume(double dPercent)
{
    volumeSlider->setValue(dPercent * MAX_SLIDER_VALUE);
    m_lastVolumePercent = dPercent;

    if (m_lastVolumePercent == 0)
    {
        GlobalHelper::SetIcon(volumeButton, 12, QChar(0xf026));
    }
    else
        GlobalHelper::SetIcon(volumeButton, 12, QChar(0xf028));

    GlobalHelper::SavePlayVolume(dPercent);
}

void CtrBar::OnPauseStat(bool bPaused)
{
    if (bPaused)
    {
        GlobalHelper::SetIcon(playPauseButton, 15, QChar(0xf04b));
        playPauseButton->setToolTip("播放");
    }
    else
    {
        GlobalHelper::SetIcon(playPauseButton, 15, QChar(0xf04c));
        playPauseButton->setToolTip("暂停");
    }
}

void CtrBar::OnStopFinished()
{
    playbackSlider->setValue(0);
    QTime StopTime(0, 0, 0);
    totalTimeDisplay->setTime(StopTime);
    currentTimeDisplay->setTime(StopTime);
    GlobalHelper::SetIcon(playPauseButton, 15, QChar(0xf04b));
    playPauseButton->setToolTip("播放");
}

void CtrBar::OnVolumeBtnClicked()
{
    if (volumeButton->text() == QChar(0xf028))
    {
        GlobalHelper::SetIcon(volumeButton, 12, QChar(0xf026));
        volumeSlider->setValue(0);
        emit SigPlayVolume(0);
    }
    else
    {
        GlobalHelper::SetIcon(volumeButton, 12, QChar(0xf028));
        volumeSlider->setValue(m_lastVolumePercent * MAX_SLIDER_VALUE);
        emit SigPlayVolume(m_lastVolumePercent);
    }
}

void CtrBar::OnPlaySliderValueChanged()
{
    double dPercent = playbackSlider->value() * 1.0 / playbackSlider->maximum();
    emit SigPlaySeek(dPercent);
    qDebug() << "PlaySilder value = " << dPercent;
}

void CtrBar::OnVolumeSliderValueChanged()
{
    double dPercent = volumeSlider->value() * 1.0 / volumeSlider->maximum();
    emit SigPlayVolume(dPercent);

    OnVideopVolume(dPercent);
}