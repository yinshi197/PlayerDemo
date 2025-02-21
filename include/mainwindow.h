#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <QDockWidget>
#include <QVBoxLayout>
#include <QMenuBar>
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

    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent* event);

    void closeEvent(QCloseEvent *event) override; // 重写关闭事件
private:
    void ConnectSig();

    QVBoxLayout *m_mainVLayout = nullptr;
    Title *m_titleBar = nullptr;

    Show *m_videoShow = nullptr;
    CtrBar *m_ctlBar = nullptr;

    PlayList *m_PlayList = nullptr;
    QMenu m_Menu;

    // 播放列表相关
    //QWidget *m_playlistPanel;    // 播放列表容器
    PlayList *m_playList;        // 播放列表内容
    bool m_bPlaylistVisible;     // 播放列表可见状态

    // 窗口拖动相关
    bool m_bDrag = false;       // 拖动标志
    QPoint m_dragPosition;      // 拖动起始偏移量

private slots:
    void OnFullScreenPlay();
    void togglePlaylist(bool visible);
    void OnShowOrHidePlaylist();

signals:
    void sigShowMax(bool isMax);
};

#endif  //MAINWINDOW_H