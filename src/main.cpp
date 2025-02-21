#include "mainwindow.h"

#include <QApplication>
#include <QFontDatabase>
#pragma comment(lib, "user32.lib")

#undef main
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    //使用第三方字库，用来作为UI图片 ://res/fa-solid-900.ttf
    QFontDatabase::addApplicationFont(":/fontawesome-webfont.ttf");

    MainWindow w;
    w.show();
    return a.exec();
}