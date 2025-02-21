#include "title.h"
#include <QFileDialog>
#include <QSettings>
#include <QStandardPaths>

Title::Title(QWidget *parent) :
    QWidget(parent), m_Menu(this), m_ActionGroup(this)
{
    InitUi();
    ConnectSig();
}

Title::~Title()
{
    
}

void Title::contextMenuEvent(QContextMenuEvent *event)
{
    m_Menu.popup(event->globalPos());
}

void Title::InitUi()
{   
    m_gridLayout = new QGridLayout(this);
    m_Logo = new QLabel(this);
    m_MenuBtn = new QPushButton(this);
    m_FileNameLab = new QLabel(this);
    m_MinBtn = new QPushButton(this);    //最小化
    m_MaxBtn = new QPushButton(this);    //最大化
    m_FullScreenBtn = new QPushButton(this); //全屏
    m_CloseBtn = new QPushButton(this);  //关闭

    this->resize(800, 50);
    this->setFixedHeight(50);
    m_gridLayout->setSpacing(0);
    m_gridLayout->setContentsMargins(0, 0, 0, 0);

    m_Logo->setObjectName("Logo");
    m_Logo->setFixedSize(50, 50);
    m_gridLayout->addWidget(m_Logo, 0, 0, 1, 1);

    m_MenuBtn->setMinimumWidth(120);
    m_MenuBtn->setMaximumWidth(200);
    m_MenuBtn->setToolTip(u8"显示菜单");
    QFont font;
    font.setFamilies({QString::fromUtf8("Bahnschrift Light SemiCondensed")});
    font.setPointSize(18);
    m_MenuBtn->setFont(font);
    m_gridLayout->addWidget(m_MenuBtn, 0, 1, 1, 1);

    m_FileNameLab->setObjectName("FileName");
    m_FileNameLab->setText("Attack.on.Titan.The.Final.Chapters.Part2.S04E30.1080p.WEB-DL.Hi10.Chs&Cht&Eng.DDP.2.0.H.264-Q66.mkv");
    m_FileNameLab->setMargin(15);
    m_gridLayout->addWidget(m_FileNameLab, 0, 2, 1, 1);

    m_MinBtn->setFixedSize(QSize(50, 50));
    m_MinBtn->setToolTip(u8"最小化");
    m_gridLayout->addWidget(m_MinBtn, 0, 3, 1, 1);

    m_MaxBtn->setFixedSize(QSize(50, 50));
    m_MaxBtn->setToolTip(u8"最大化");
    m_gridLayout->addWidget(m_MaxBtn, 0, 4, 1, 1);

    m_FullScreenBtn->setFixedSize(QSize(50, 50));
    m_FullScreenBtn->setToolTip(u8"全屏");
    m_gridLayout->addWidget(m_FullScreenBtn, 0, 5, 1, 1);

    m_CloseBtn->setObjectName("CloseBtn");
    m_CloseBtn->setFixedSize(QSize(50, 50));
    m_CloseBtn->setToolTip(u8"关闭");
    m_gridLayout->addWidget(m_CloseBtn, 0, 6, 1, 1);

    //保证窗口不被绘制上的部分透明
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet(GlobalHelper::GetQssStr(":/qss/title.css"));

    GlobalHelper::SetIcon(m_MinBtn, 9, QChar(0xf2d1));
    GlobalHelper::SetIcon(m_MaxBtn, 9, QChar(0xf2d0));
    GlobalHelper::SetIcon(m_FullScreenBtn, 9, QChar(0xf065));
    GlobalHelper::SetIcon(m_CloseBtn, 9, QChar(0xf00d));
}

void Title::OpenFile()
{
    QString cfgPath = "HKEY_CURRENT_USER\\Software\\MediaPlayer";
    QSettings settings(cfgPath, QSettings::NativeFormat);
    QString lastPath = settings.value("openfile_path").toString();  // 从注册表获取路径

    if (lastPath.isEmpty())
    {
        lastPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);  //获取默认的文档路径
    }

    //可以同时打开多个文件
    QString fileName= QFileDialog::getOpenFileName(
        this,
        u8"选择要播放的文件",
        lastPath,
        u8"视频文件 (*.flv *.rmvb *.avi *.mp4 *.mkv *.ts);; 所有文件 (*.*);; ");

    if (fileName.isEmpty())
    {
        return;
    }

    int end = fileName.lastIndexOf("/");
    QString tmppath = fileName.left(end + 1);
    settings.setValue("openfile_path", tmppath);  // 将当前打开的路径写入到注册表

    emit sigOpenFile(fileName);
}

void Title::ConnectSig()
{
    connect(m_MinBtn, &QPushButton::clicked, this, &Title::sigMinBtnClicked);
    connect(m_MaxBtn, &QPushButton::clicked, this, &Title::sigMaxBtnClicked);
    connect(m_FullScreenBtn, &QPushButton::clicked, this, &Title::sigFullScreenBtnClicked);
    connect(m_CloseBtn, &QPushButton::clicked, this, &Title::sigCloseBtnClicked);
    
    m_Menu.addAction(u8"最小化", this, &Title::sigMinBtnClicked);
    m_Menu.addAction(u8"最大化", this, &Title::sigMaxBtnClicked);
    m_Menu.addAction(u8"全屏", this, &Title::sigFullScreenBtnClicked);
    m_Menu.addAction(u8"关闭", this, &Title::sigCloseBtnClicked);

    QMenu *menu = m_Menu.addMenu(u8"打开");
    menu->addAction(u8"打开文件", this, &Title::OpenFile);
}

void Title::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
}

void Title::ChangeFilename()
{
    QFontMetrics fontMetrics(m_FileNameLab->font());
    QRect rect = fontMetrics.boundingRect(m_strFileName);
    int font_width = rect.width();
    int show_width = m_FileNameLab->width();
    if(font_width > show_width)
    {
        //文件过长，返回一个带有省略号的字符串
        QString str = fontMetrics.elidedText(m_strFileName, Qt::ElideRight, m_FileNameLab->width());
        m_FileNameLab->setText(str);
    }
    else
    {
        m_FileNameLab->setText(m_strFileName);
    }
}

void Title::OnChangeMaxBtnStyle(bool bIfMax)
{
    if(bIfMax)
    {
        GlobalHelper::SetIcon(m_MaxBtn, 9, QChar(0xf2d2));
        m_MaxBtn->setToolTip("还原");
    }
    else
    {
        GlobalHelper::SetIcon(m_MaxBtn, 9, QChar(0xf2d0));
        m_MaxBtn->setToolTip("最大化");
    }
}

void Title::OnPlay(QString strMovieName)
{
    QFileInfo fileInfo(strMovieName);
    m_strFileName = fileInfo.fileName();
    //m_FileNameLab->setText(m_strFileName);
    ChangeFilename();
}

void Title::OnStopFinished()
{
    m_FileNameLab->clear();
}
