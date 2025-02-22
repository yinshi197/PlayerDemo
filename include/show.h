#ifndef SHOW_H
#define SHOW_H

#include <QWidget>
#include <QLabel>
#include <QMimeData>
#include <QDebug>
#include <QTimer>
#include <QDragEnterEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QActionGroup>
#include <QAction>
#include "videoctl.h"

class Show : public QWidget
{
    Q_OBJECT

public:
    Show(QWidget *parent);
    ~Show();

    void OnPlay(QString strFile);
    // void OnStopFinished();

    // void OnFrameDimensionsChanged(int FrameWidth, int FrameHeight);

    QLabel *m_video = nullptr;
    VideoCtl *videoctl = nullptr;
protected:
    // void dropEvent(QDropEvent *event);
    // void dragEnterEvent(QDragEnterEvent *event);
    // void resizeEvent(QResizeEvent *event);
    // void keyReleaseEvent(QKeyEvent *event);
    // void mousePressEvent(QMouseEvent *event);
    void contextMenuEvent(QContextMenuEvent *event);

private:
    void InitUi();
    void ConnectSig();

    void UpdateVolume(int sign, double step);

    // void OnDisplayMsg(QString strMsg);
    // void OnTimerShowCursorUpdate();
    // void OnActionsTriggered(QAction *action);
    // void ChangeShow();

    int m_lastFrameWidth; ///< 记录视频宽高
    int m_lastFrameHeight;

    QTimer timerShowCur;

    QMenu m_menu;
    QActionGroup m_actionGroup;

signals:
    void SigOpenFile(QString strFileName);
    void SigPlay(QString strFile);
                                
    void SigFullScreen();
    void SigStop();
    void SigShowMenu();

    void SigSeekForward();
    void SigSeekBack();
    void SigAddVolume();
    void SigSubVolume();

    void SigPauseStat(bool bPaused);

    void SigVideoVolume(double dPercent);
    void SigVideoTotalSeconds(int nSeconds);

public slots:
    void OnPlaySeek(double dPercent);
    void OnPlayVolume(double dPercent);
    void OnSeekForward();
    void OnSeekBack();
    void OnAddVolume();
    void OnSubVolume();
    void OnPause();
    void OnStop();

};

#endif