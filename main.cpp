#include "mainwindow.h"
#include <QApplication>
#include <QFile>
#include "feiqwin.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setWindowIcon(QIcon(":/default/res/icon.png"));

    // 加载 QSS 样式表
    QFile styleFile(":/default/res/style.qss");
    if (styleFile.open(QFile::ReadOnly | QFile::Text))
    {
        a.setStyleSheet(styleFile.readAll());
        styleFile.close();
    }

    MainWindow w;
    FeiqWin feiqWin;
    w.setFeiqWin(&feiqWin);

    w.show();
    return a.exec();
}
