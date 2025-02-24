#include "playlist.h"
#include "globalhelper.h"
#include <QGridLayout>
#include <QFileInfo>

PlayList::PlayList(QWidget *parent) : QWidget(parent)
{
    GlobalHelper::GetLastPlayListIndex(m_CurrPlayListIndex);
    
    InitUi();
    ConnectSignalSlots();
}

PlayList::~PlayList()
{
    QStringList strListPlayList;
    for (int i = 0; i < List->count(); i++)
    {
        strListPlayList.append(List->item(i)->toolTip());
    }
    GlobalHelper::SavePlaylist(strListPlayList);

    GlobalHelper::SaveLastPlayListIndex(m_CurrPlayListIndex);
}

bool PlayList::GetStatus()
{
    if (this->isHidden())
    {
        return false;
    }

    return true;
}

void PlayList::dropEvent(QDropEvent *event)
{
    QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty())
    {
        return;
    }

    for (QUrl url : urls)
    {
        QString strFileName = url.toLocalFile();

        OnAddFile(strFileName);
    }
}

void PlayList::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void PlayList::InitUi()
{
    this->setVisible(true);
    this->setMinimumSize(120, 240);
    this->setStyleSheet(GlobalHelper::GetQssStr(":/qss/playlist.css"));

    QGridLayout *gridLayout = new QGridLayout(this);
    List = new MediaList(this);

    gridLayout->setSpacing(0);
    gridLayout->setContentsMargins(0, 1, 0, 0);

    List->setFocusPolicy(Qt::NoFocus);
    List->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    List->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    gridLayout->addWidget(List, 0, 0, 1, 1);

    List->clear();

    QStringList strListPlaylist;
    GlobalHelper::GetPlaylist(strListPlaylist);

    for (QString strVideoFile : strListPlaylist)
    {
        QFileInfo fileInfo(strVideoFile);
        if (fileInfo.exists())
        {
            QListWidgetItem *pItem = new QListWidgetItem(List);
            pItem->setData(Qt::UserRole, QVariant(fileInfo.filePath()));  // 用户数据
            pItem->setText(QString("%1").arg(fileInfo.fileName()));  // 显示文本
            pItem->setToolTip(fileInfo.filePath());
            List->addItem(pItem);
        }
    }

    // 设置当前选中项为上一次关闭时的选项
    if (m_CurrPlayListIndex >= 0 && m_CurrPlayListIndex < List->count())
    {
        List->setCurrentRow(m_CurrPlayListIndex);
    }
    else if (List->count() > 0)
    {
        // 如果索引无效，默认选中第一项
        List->setCurrentRow(0);
    }
}

void PlayList::ConnectSignalSlots()
{
    connect(List, &MediaList::SigAddFile, this, &PlayList::OnAddFile);
    connect(List, &MediaList::itemDoubleClicked, this, &PlayList::OnListItemDoubleClicked);
    connect(List, &MediaList::SigAddURL, this, &PlayList::OnAddURL);
}

void PlayList::OnAddFile(QString strFileName)
{
    bool bSupportMovie = strFileName.endsWith(".mkv", Qt::CaseInsensitive) ||
        strFileName.endsWith(".rmvb", Qt::CaseInsensitive) ||
        strFileName.endsWith(".mp4", Qt::CaseInsensitive) ||
        strFileName.endsWith(".avi", Qt::CaseInsensitive) ||
        strFileName.endsWith(".flv", Qt::CaseInsensitive) ||
        strFileName.endsWith(".wmv", Qt::CaseInsensitive) ||
        strFileName.endsWith(".3gp", Qt::CaseInsensitive) ||
        strFileName.endsWith(".mp3", Qt::CaseInsensitive) ||
        strFileName.endsWith(".flac", Qt::CaseInsensitive);
    if (!bSupportMovie)
    {
        return;
    }

    QFileInfo fileInfo(strFileName);
	QList<QListWidgetItem*> listItem = List->findItems(fileInfo.fileName(), Qt::MatchExactly);
    QListWidgetItem *pItem = nullptr;
	if (listItem.isEmpty())
	{
        pItem = new QListWidgetItem(List);
        pItem->setData(Qt::UserRole, QVariant(fileInfo.filePath()));  // 用户数据
        pItem->setText(fileInfo.fileName());  // 显示文本
        pItem->setToolTip(fileInfo.filePath());
        List->addItem(pItem);
	}
    else
    {
        pItem = listItem.at(0);
    }
}


void PlayList::OnAddFileAndPlay(QString strFileName)
{
    bool bSupportMovie = strFileName.endsWith(".mkv", Qt::CaseInsensitive) ||
        strFileName.endsWith(".rmvb", Qt::CaseInsensitive) ||
        strFileName.endsWith(".mp4", Qt::CaseInsensitive) ||
        strFileName.endsWith(".avi", Qt::CaseInsensitive) ||
        strFileName.endsWith(".flv", Qt::CaseInsensitive) ||
        strFileName.endsWith(".wmv", Qt::CaseInsensitive) ||
        strFileName.endsWith(".3gp", Qt::CaseInsensitive) ||
        strFileName.endsWith(".mp3", Qt::CaseInsensitive) ||
        strFileName.endsWith(".flac", Qt::CaseInsensitive);
    if (!bSupportMovie)
    {
        return;
    }

    QFileInfo fileInfo(strFileName);
    QList<QListWidgetItem*> listItem = List->findItems(fileInfo.fileName(), Qt::MatchExactly);
    QListWidgetItem *pItem = nullptr;
    if (listItem.isEmpty())
    {
        pItem = new QListWidgetItem(List);
        pItem->setData(Qt::UserRole, QVariant(fileInfo.filePath()));  // 用户数据
        pItem->setText(fileInfo.fileName());  // 显示文本
        pItem->setToolTip(fileInfo.filePath());
        List->addItem(pItem);
    }
    else
    {
        pItem = listItem.at(0);
    }
    OnListItemDoubleClicked(pItem);
}

void PlayList::OnBackwardPlay()
{
    if (m_CurrPlayListIndex == 0)
    {
        m_CurrPlayListIndex = List->count() - 1;
        OnListItemDoubleClicked(List->item(m_CurrPlayListIndex));
        List->setCurrentRow(m_CurrPlayListIndex);
    }
    else
    {
        m_CurrPlayListIndex--;
        OnListItemDoubleClicked(List->item(m_CurrPlayListIndex));
        List->setCurrentRow(m_CurrPlayListIndex);
    }
}

void PlayList::OnForwardPlay()
{
    if (m_CurrPlayListIndex == List->count() - 1)
    {
        m_CurrPlayListIndex = 0;
        OnListItemDoubleClicked(List->item(m_CurrPlayListIndex));
        List->setCurrentRow(m_CurrPlayListIndex);
    }
    else
    {
        m_CurrPlayListIndex++;
        List->setCurrentRow(m_CurrPlayListIndex);
        OnListItemDoubleClicked(List->item(m_CurrPlayListIndex));
    }
}

void PlayList::OnAddURL(QString url)
{
	QList<QListWidgetItem*> listItem = List->findItems(url, Qt::MatchExactly);
    QListWidgetItem *pItem = nullptr;
	if (listItem.isEmpty())
	{
        pItem = new QListWidgetItem(List);
        pItem->setData(Qt::UserRole, QVariant(url));  // 用户数据
        pItem->setText(url);  // 显示文本
        pItem->setToolTip(url);
        List->addItem(pItem);
	}
    else
    {
        pItem = listItem.at(0);
    }
}

void PlayList::OnListItemDoubleClicked(QListWidgetItem *item)
{
    m_CurrPlayListIndex = List->row(item);
    List->setCurrentRow(m_CurrPlayListIndex);
    emit sigPlay(item->data(Qt::UserRole).toString());
}
