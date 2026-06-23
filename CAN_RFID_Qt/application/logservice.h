#ifndef LOGSERVICE_H
#define LOGSERVICE_H

#include <QDate>
#include <QFile>
#include <QByteArray>
#include <QString>
#include <QTextStream>
#include "domain/canframe.h"

enum class LogLevel
{
    Info,
    Warning,
    Error
};

class LogService
{
public:
    LogService();
    ~LogService();

    void setLogDirectory(const QString &directoryPath);
    void setCanAutoSaveEnabled(bool enabled);
    void logRuntime(LogLevel level, const QString &message);
    void logCanFrame(const CanFrame &frame);
    void logSerialFrame(bool isTx,
                        const QByteArray &data,
                        const QString &protocolId,
                        const QString &decodeText);

private:
    void ensureRuntimeLogOpen();
    void ensureCanLogOpen();
    void ensureSerialLogOpen();
    QString levelText(LogLevel level) const;
    QString csvEscape(const QString &value) const;

    QString logDirectory;
    bool canAutoSaveEnabled;
    QFile runtimeLogFile;
    QFile canLogFile;
    QFile serialLogFile;
    QDate runtimeLogDate;
    QDate canLogDate;
    QDate serialLogDate;
    int pendingRuntimeRows;
    int pendingCanRows;
    int pendingSerialRows;
};

#endif // LOGSERVICE_H
