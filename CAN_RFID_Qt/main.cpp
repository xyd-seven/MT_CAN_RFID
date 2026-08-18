#include "mainwindow.h"
#include "application/productioncanworkerhost.h"
#include "domain/canframe.h"
#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    // 启用高 DPI 缩放支持与高 DPI 图标
#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    if (argc >= 3 && QString::fromLocal8Bit(argv[1]) ==
                         QStringLiteral("--production-can-worker")) {
        QCoreApplication workerApplication(argc, argv);
        qRegisterMetaType<CanFrame>("CanFrame");
        qRegisterMetaType<QVector<CanFrame>>("QVector<CanFrame>");
        ProductionCanWorkerHost workerHost(QString::fromLocal8Bit(argv[2]));
        QString error;
        if (!workerHost.listen(&error)) {
            return 2;
        }
        return workerApplication.exec();
    }

    QApplication a(argc, argv);

    MainWindow w;
    w.show();

    return a.exec();
}
