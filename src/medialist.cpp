#include "medialist.h"
#include <QFileDialog>
#include <QSettings>
#include <QStandardPaths>


MediaList::MediaList(QWidget *parent) :
    QListWidget(parent),
    m_ActAdd(this), m_ActRemove(this), m_ActClearList(this)
{
    Init();
}

MediaList::~MediaList()
{
}

void MediaList::Init()
{
    m_ActAdd.setText(u8"添加");
    m_Menu.addAction(&m_ActAdd);

    m_ActRemove.setText(u8"移除所有选项");
    QMenu *removeMenu = m_Menu.addMenu(u8"移除");
    removeMenu->addAction(&m_ActRemove);

    m_ActClearList.setText(u8"清空列表");
    m_Menu.addAction(&m_ActClearList);

    connect(&m_ActAdd, &QAction::triggered, this, &MediaList::AddFile);
    connect(&m_ActRemove, &QAction::triggered, this, &MediaList::RemoveFile);
    connect(&m_ActClearList, &QAction::triggered, this, &QListWidget::clear);
    
}

void MediaList::contextMenuEvent(QContextMenuEvent *event)
{
    m_Menu.popup(event->globalPos());    //exec会阻塞主线程
}

void MediaList::AddFile()
{
    QString cfgPath = "HKEY_CURRENT_USER\\Software\\MediaPlayer";
    QSettings settings(cfgPath, QSettings::NativeFormat);
    QString lastPath = settings.value("openfile_path").toString();  // 从注册表获取路径

    if (lastPath.isEmpty())
    {
        lastPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);  //获取默认的文档路径
    }

    //可以同时打开多个文件
    QStringList filelist = QFileDialog::getOpenFileNames(
        this,
        u8"选择要播放的文件",
        lastPath,
        u8"视频文件 (*.flv *.rmvb *.avi *.mp4 *.mkv *.ts);; 所有文件 (*.*);; ");

    if (filelist.isEmpty())
    {
        return;
    }

    int end = filelist[0].lastIndexOf("/");
    QString tmppath = filelist[0].left(end + 1);
    settings.setValue("openfile_path", tmppath);  // 将当前打开的路径写入到注册表

    for(QString strFileName : filelist)
    {
        emit SigAddFile(strFileName);
    }
}

void MediaList::RemoveFile()
{
    takeItem(currentRow());
}
