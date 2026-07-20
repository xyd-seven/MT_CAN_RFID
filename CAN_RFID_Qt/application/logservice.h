#ifndef LOGSERVICE_H
#define LOGSERVICE_H

#include <QDate>
#include <QFile>
#include <QByteArray>
#include <QString>
#include <QTextStream>
#include <QDateTime>
#include <QHash>
#include "domain/canframe.h"

class CanLogWriter;

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
    void beginCanTimingMeasurement();
    QString finishCanTimingMeasurement();
    void logSerialFrame(bool isTx,
                        const QByteArray &data,
                        const QString &protocolId,
                        const QString &decodeText);

private:
    void ensureRuntimeLogOpen();
    void ensureSerialLogOpen();
    QString canLogFilePath() const;
    void recordCanTiming(const CanFrame &frame);
    void writeCanTimingSummary(const QString &summary) const;
    QString levelText(LogLevel level) const;
    QString csvEscape(const QString &value) const;
    QString textField(const QString &value) const;

    QString logDirectory;
    bool canAutoSaveEnabled;
    QDateTime autoSaveSessionTime;
    QFile runtimeLogFile;
    QFile serialLogFile;
    QDate runtimeLogDate;
    QDate serialLogDate;
    int pendingRuntimeRows;
    int pendingSerialRows;
    CanLogWriter *canLogWriter;
    bool canTimingMeasurementActive;

    struct CanTimingRow {
        quint64 count = 0;
        qint64 lastElapsedMs = -1;
        qint64 intervalTotalMs = 0;
        qint64 intervalMaxMs = 0;
        quint64 delayedCount = 0;
    };
    QHash<quint32, CanTimingRow> canTimingRows;
};

#endif // LOGSERVICE_H
