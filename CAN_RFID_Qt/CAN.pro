QT += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = CAN_RFID
TEMPLATE = app
CONFIG += c++11

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    canthread.cpp \
    rfidprotocol.cpp \
    domain/canframe.cpp \
    domain/isotptransport.cpp \
    application/appconfig.cpp \
    application/logservice.cpp \
    application/otaservice.cpp \
    application/rfidservice.cpp \
    application/stresstestservice.cpp

HEADERS += \
    mainwindow.h \
    canthread.h \
    rfidprotocol.h \
    domain/canframe.h \
    domain/isotptransport.h \
    domain/crc16.h \
    application/appconfig.h \
    application/logservice.h \
    application/otaservice.h \
    application/rfidservice.h \
    application/stresstestservice.h \
    third_party/zlgcan/zlgcan.h \
    third_party/zlgcan/config.h

FORMS += \
    mainwindow.ui

INCLUDEPATH += $$PWD \
    $$PWD/third_party/zlgcan
DEPENDPATH += $$PWD \
    $$PWD/third_party/zlgcan

win32: LIBS += -L$$PWD/third_party/zlgcan -lzlgcan
win32:!win32-g++: PRE_TARGETDEPS += $$PWD/third_party/zlgcan/zlgcan.lib

win32 {
    CONFIG(debug, debug|release) {
        ZLGCAN_DLL_TARGET = $$OUT_PWD/debug/zlgcan.dll
    } else {
        ZLGCAN_DLL_TARGET = $$OUT_PWD/release/zlgcan.dll
    }
    QMAKE_POST_LINK += $$QMAKE_COPY $$shell_path($$PWD/third_party/zlgcan/zlgcan.dll) $$shell_path($$ZLGCAN_DLL_TARGET)
    RC_ICONS = $$PWD/images/MT_RFID.ico
}

RESOURCES += \
    resources.qrc

