#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <QWidget>
#include <QListWidgetItem>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QMimeData>
#include "medialist.h"

class PlayList : public QWidget
{
    Q_OBJECT

public:
    PlayList(QWidget *parent = nullptr);
    ~PlayList();

    bool GetStatus();

    /* 在这里定义dock的初始大小 */
    QSize sizeHint() const
    {
        return QSize(150, 900);
    }

protected:
    void dropEvent(QDropEvent *event);  //放下事件
    void dragEnterEvent(QDragEnterEvent *event);    //拖动事件

private:
    void InitUi();
    void ConnectSignalSlots();

    MediaList *List = nullptr;
    int m_CurrPlayListIndex;

public slots:
    void OnAddFile(QString strFileName);
    void OnAddFileAndPlay(QString strFileName);
    void OnBackwardPlay();
    void OnForwardPlay();
    void OnAddURL(QString url);

private slots:
    void OnListItemDoubleClicked(QListWidgetItem *item);

signals:
    void sigUpdateUi();	//< 界面排布更新
    void sigPlay(QString strFile); //< 播放文件
};

#endif