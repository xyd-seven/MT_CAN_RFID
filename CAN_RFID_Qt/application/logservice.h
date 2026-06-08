#ifndef LOGSERVICE_H
#define LOGSERVICE_H

#include <QDate>
#include <QFile>
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

private:
    void ensureRuntimeLogOpen();
    void ensureCanLogOpen();
    QString levelText(LogLevel level) const;
    QString csvEscape(const QString &value) const;

    QString logDirectory;
    bool canAutoSaveEnabled;
    QFile runtimeLogFile;
    QFile canLogFile;
    QDate runtimeLogDate;
    QDate canLogDate;
    int pendingRuntimeRows;
    int pendingCanRows;
};

#endif // LOGSERVICE_H
