#ifndef MEDIALIST_H
#define MEDIALIST_H

#include <QListWidget>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent> 

class MediaList : public QListWidget
{
    Q_OBJECT

public:
    MediaList(QWidget *parent = 0);
    ~MediaList();
    void Init();

protected:
    void contextMenuEvent(QContextMenuEvent* event);   

private:
    void AddFile();
    void RemoveFile();
    void AddURL();

    QMenu m_Menu;
    QAction m_ActAdd;
    QAction m_ActAddURL;
    QAction m_ActRemove;
    QAction m_ActClearList;

signals:
    void SigAddFile(QString strFileName);
    void SigAddURL(QString url);
};

#endif