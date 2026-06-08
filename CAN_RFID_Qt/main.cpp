#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    // 启用高 DPI 缩放支持与高 DPI 图标
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication a(argc, argv);

    MainWindow w;
    w.show();

    return a.exec();
}
