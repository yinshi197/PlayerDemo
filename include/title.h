#ifndef TITLE_H
#define TITLE_H

#include <QWidget>
#include <QMouseEvent>
#include <QMenu>
#include <QActionGroup>
#include <QAction>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include "globalhelper.h"

class Title : public QWidget
{
    Q_OBJECT

public:
    Title(QWidget *parent = nullptr);
    ~Title();

    QGridLayout *m_gridLayout = nullptr;
    QLabel *m_Logo = nullptr;
    QPushButton *m_MenuBtn = nullptr;
    QLabel *m_FileNameLab = nullptr;
    QPushButton *m_MinBtn = nullptr;
    QPushButton *m_MaxBtn = nullptr;
    QPushButton *m_FullScreenBtn = nullptr;
    QPushButton *m_CloseBtn = nullptr;

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    
private:
    QString m_strFileName = nullptr;
    QMenu m_Menu;
    QActionGroup m_ActionGroup;

    void InitUi();
    void OpenFile();
    void ConnectSig();

    void paintEvent(QPaintEvent *event);
    void ChangeFilename();

signals:
    void sigMinBtnClicked();
    void sigMaxBtnClicked();
    void sigFullScreenBtnClicked();
    void sigCloseBtnClicked();

    void sigDoubleClicked();

    void sigOpenFile(QString strFileName);
    void sigShowMenu();

public slots:
    void OnChangeMaxBtnStyle(bool bIfMax);
    void OnPlay(QString strMovieName);
    void OnStopFinished();
};

#endif  //TITLE_H