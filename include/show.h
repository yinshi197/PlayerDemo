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

    bool Init();

    QLabel *m_video = nullptr;
protected:
    void dropEvent(QDropEvent *event);
    void dragEnterEvent(QDragEnterEvent *event);
    void resizeEvent(QResizeEvent *event);
    void mousePressEvent(QMouseEvent *event);
    void contextMenuEvent(QContextMenuEvent *event);

    void keyReleaseEvent(QKeyEvent *event);

private:
    void InitUi();
    bool ConnectSignalSlots();
    void ChangeShow();

    int m_lastFrameWidth; ///< 记录视频宽高
    int m_lastFrameHeight;

    QTimer timerShowCur;

    QMenu m_menu;
    QActionGroup m_actionGroup;

signals:
    void SigOpenFile(QString strFileName);
    void SigPlay(QString strFile);
                                
    void SigFullScreen();
    void SigPlayOrPause();
    void SigStop();
    void SigShowMenu();

    void SigSeekForward();
    void SigSeekBack();
    void SigAddVolume();
    void SigSubVolume();

public slots:
    void OnPlay(QString strFile);
    void OnStopFinished();

    void OnFrameDimensionsChanged(int FrameWidth, int FrameHeight);

private slots:
    void OnDisplayMsg(QString strMsg);
    void OnTimerShowCursorUpdate();
    void OnActionsTriggered(QAction *action);
};

#endif