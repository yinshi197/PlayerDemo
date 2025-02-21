#ifndef CTLBAR_H
#define CTLBAR_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpacerItem>
#include <QTimeEdit>
#include <QWidget>
#include "customslider.h"

class CtrBar : public QWidget
{
    Q_OBJECT

public:
    explicit CtrBar(QWidget *parent = nullptr);
    ~CtrBar();

private:
    void InitUi();
    void ConnectSig();

    QVBoxLayout *mainLayout;      // 主布局
    QWidget *topWidget;
    QHBoxLayout *sliderLayout;    // 进度条布局
    CustomSlider *playbackSlider; // 播放进度条
    QWidget *volumeControlWidget; // 音量控制部件
    QHBoxLayout *volumeLayout;    // 音量布局
    QPushButton *volumeButton;    // 音量按钮
    CustomSlider *volumeSlider;   // 音量滑块

    QGridLayout *controlButtonsLayout; // 控制按钮布局
    QLabel *timeSeparatorLabel;        // 时间分隔符标签
    QTimeEdit *totalTimeDisplay;       // 总时间显示
    QPushButton *forwardButton;        // 快进按钮
    QPushButton *backwardButton;       // 快退按钮
    QPushButton *stopButton;           // 停止按钮
    QTimeEdit *currentTimeDisplay;     // 当前时间显示
    QSpacerItem *spacer;               // 占位符
    QPushButton *playPauseButton;      // 播放/暂停按钮
    QPushButton *settingsButton;       // 设置按钮
    QPushButton *playlistButton;       // 播放列表按钮

    int m_totalPlaySeconds;
    double m_lastVolumePercent;

signals:
    void SigShowOrHidePlaylist();      // 显示或隐藏信号
    void SigPlaySeek(double dPercent); // 调整播放进度
    void SigPlayVolume(double dPercent);
    void SigPlayOrPause();
    void SigStop();
    void SigForwardPlay();
    void SigBackwardPlay();
    void SigShowMenu();
    void SigShowSetting();

public slots:
    void OnVideoTotalSeconds(int nSeconds);
    void OnVideoPlaySeconds(int nSeconds);
    void OnVideopVolume(double dPercent);
    void OnPauseStat(bool bPaused);
    void OnStopFinished();

private slots:
    // void on_PlayOrPauseBtn_clicked();
    // void on_VolumeBtn_clicked();
    // void on_StopBtn_clicked();
    // void on_SettingBtn_clicked();
    void OnPlaySliderValueChanged();
    void OnVolumeSliderValueChanged();
};

#endif