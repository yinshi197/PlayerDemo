#ifndef ABOUT_H
#define ABOUT_H

#include <QWidget>
#include <QMouseEvent>
#include <QPoint>

class About : public QWidget
{
    Q_OBJECT

public:
    About(QWidget *parent = nullptr);
    ~About();

// protected:
//     void mousePressEvent(QMouseEvent *event);
//     void mouseReleaseEvent(QMouseEvent *event);
//     void mouseMoveEvent(QMouseEvent *event);

// private:
//     bool m_MoveIndex;
//     QPoint m_DragPosition;
 
// private slots:
//     void OnCloseBtnClicked();
};

#endif