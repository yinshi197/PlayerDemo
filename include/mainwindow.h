#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <QDockWidget>
#include <QVBoxLayout>
#include <QMenuBar>
#include <QPropertyAnimation>
#include "title.h"
#include "playlist.h"
#include "show.h"
#include "ctlbar.h"

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void InitUi();

protected:
    void paintEvent(QPaintEvent *event);
    void enterEvent(QEvent *event);
    void leaveEvent(QEvent *event);
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent* event);

    //按键事件
    void keyReleaseEvent(QKeyEvent *event);
private:
    void ConnectSig();

    QVBoxLayout *m_mainVLayout = nullptr;
    QHBoxLayout *bodyLayout = nullptr;
    Title *m_titleBar = nullptr;
    Show *m_videoShow = nullptr;
    CtrBar *m_ctlBar = nullptr;
    PlayList *m_playList = nullptr;
    QMenu m_Menu;

    const int m_ShadowWidth; //阴影宽度

    bool m_FullScreenPlayIndex; //全屏播放标志

    QPropertyAnimation *m_CtrlbarAnimationShow; //全屏时控制面板浮动显示
    QPropertyAnimation *m_CtrlbarAnimationHide; //全屏时控制面板隐藏
    QRect m_CtrlBarAnimationShow;//控制面板显示区域
    QRect m_CtrlBarAnimationHide;//控制面板隐藏区域

    QTimer m_CtrlBarAnimationTimer;
    QTimer m_FullscreenMouseDetectTimer;//全屏时鼠标位置监测时钟
    bool m_FullscreenCtrlBarShow;
    QTimer CtrlBarHideTimer;

    QAction m_ActFullscreen;

    bool m_PlayingIndex; //正在播放

    // 窗口拖动相关
    bool m_bDrag = false;       // 拖动标志
    QPoint m_dragPosition;      // 拖动起始偏移量

private slots:
    void OnCtrlBarAnimationTimeOut();
    void OnFullscreenMouseDetectTimeOut();
    void OnCtrlBarHideTimeOut();
    void OnFullScreenPlay();
    void OnShowOrHidePlaylist();
    //void OnShowMenu();
    //void OpenFile();
    //void OnShowSettingWid();

signals:
    void sigShowMax(bool isMax);
    void SigSeekForward();
    void SigSeekBack();
    void SigAddVolume();
    void SigSubVolume();
    void SigPlayOrPause();
    void SigOpenFile(QString strFilename);
};

#endif  //MAINWINDOW_H