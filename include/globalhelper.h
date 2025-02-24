#ifndef GLOBALHELPER_H
#define GLOBALHELPER_H

#pragma execution_character_set("utf-8")

#define MAX_SLIDER_VALUE 65536

enum ERROR_CODE
{
    NoError = 0,
    ErrorFileInvalid
};

#include <QString>
#include <QPushButton>
#include <QDebug>
#include <QStringList>

class GlobalHelper
{
public:
    GlobalHelper();
	/**
	 * 获取样式表
	 * 
	 * @param	strQssPath 样式表文件路径
	 * @return	样式表
	 * @note 	
	 */
    static QString GetQssStr(QString strQssPath);

	/**
	 * 为按钮设置显示图标
	 * 
	 * @param	btn 按钮指针
	 * @param	iconSize 图标大小
	 * @param	icon 图标字符
	 */
    static void SetIcon(QPushButton* btn, int iconSize, QChar icon);


    static void SavePlaylist(QStringList& playList);
    static void GetPlaylist(QStringList& playList);
    static void SavePlayVolume(double& nVolume);
    static void GetPlayVolume(double& nVolume);
	static void SaveLastPlayListIndex(int& LastPlayListIndex);
	static void GetLastPlayListIndex(int& LastPlayListIndex);

    static QString GetAppVersion();
};

#endif