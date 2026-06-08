#include "logservice.h"

#include <QDate>
#include <QDateTime>
#include <QDir>

namespace {
constexpr int FlushRowThreshold = 100;
}

LogService::LogService() :
    canAutoSaveEnabled(false),
    pendingRuntimeRows(0),
    pendingCanRows(0)
{
}

LogService::~LogService()
{
    if (runtimeLogFile.isOpen()) {
        runtimeLogFile.flush();
        runtimeLogFile.close();
    }
    if (canLogFile.isOpen()) {
        canLogFile.flush();
        canLogFile.close();
    }
}

void LogService::setLogDirectory(const QString &directoryPath)
{
    if (logDirectory != directoryPath) {
        if (runtimeLogFile.isOpen()) {
            runtimeLogFile.flush();
            runtimeLogFile.close();
        }
        if (canLogFile.isOpen()) {
            canLogFile.flush();
            canLogFile.close();
        }
        runtimeLogDate = QDate();
        canLogDate = QDate();
        pendingRuntimeRows = 0;
        pendingCanRows = 0;
    }
    logDirectory = directoryPath;
    QDir().mkpath(logDirectory);
}

void LogService::setCanAutoSaveEnabled(bool enabled)
{
    canAutoSaveEnabled = enabled;
    if (!canAutoSaveEnabled && canLogFile.isOpen()) {
        canLogFile.flush();
        canLogFile.close();
        pendingCanRows = 0;
    }
}

void LogService::logRuntime(LogLevel level, const QString &message)
{
    ensureRuntimeLogOpen();
    if (!runtimeLogFile.isOpen()) {
        return;
    }

    QTextStream stream(&runtimeLogFile);
    stream << csvEscape(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")) << ','
           << csvEscape(levelText(level)) << ','
           << csvEscape(message) << '\n';
    ++pendingRuntimeRows;
    if (pendingRuntimeRows >= FlushRowThreshold) {
        runtimeLogFile.flush();
        pendingRuntimeRows = 0;
    }
}

void LogService::logCanFrame(const CanFrame &frame)
{
    if (!canAutoSaveEnabled) {
        return;
    }

    ensureCanLogOpen();
    if (!canLogFile.isOpen()) {
        return;
    }

    QTextStream stream(&canLogFile);
    stream << csvEscape(frame.hostDateTime.toString("yyyy-MM-dd HH:mm:ss.zzz")) << ','
           << frame.channel << ','
           << csvEscape(frame.directionText()) << ','
           << csvEscape(frame.idText()) << ','
           << csvEscape(frame.frameTypeText()) << ','
           << csvEscape(frame.payloadTypeText()) << ','
           << frame.dlc() << ','
           << csvEscape(frame.protocolText()) << ','
           << csvEscape(frame.dataText().trimmed()) << ','
           << (frame.hasZlgTimestamp ? QString::number(frame.zlgTimestampRaw) : QString()) << '\n';
    ++pendingCanRows;
    if (pendingCanRows >= FlushRowThreshold) {
        canLogFile.flush();
        pendingCanRows = 0;
    }
}

void LogService::ensureRuntimeLogOpen()
{
    if (logDirectory.isEmpty()) {
        return;
    }

    const QDate today = QDate::currentDate();
    if (runtimeLogFile.isOpen() && runtimeLogDate == today) {
        return;
    }
    if (runtimeLogFile.isOpen()) {
        runtimeLogFile.flush();
        runtimeLogFile.close();
    }
    runtimeLogDate = today;
    pendingRuntimeRows = 0;

    const QString filePath = QDir(logDirectory).filePath(
        QString("runtime_%1.csv").arg(today.toString("yyyyMMdd")));
    runtimeLogFile.setFileName(filePath);
    const bool existed = QFile::exists(filePath);
    if (!runtimeLogFile.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }
    if (!existed) {
        QTextStream stream(&runtimeLogFile);
        stream << "pc_time,level,message\n";
    }
}

void LogService::ensureCanLogOpen()
{
    if (logDirectory.isEmpty()) {
        return;
    }

    const QDate today = QDate::currentDate();
    if (canLogFile.isOpen() && canLogDate == today) {
        return;
    }
    if (canLogFile.isOpen()) {
        canLogFile.flush();
        canLogFile.close();
    }
    canLogDate = today;
    pendingCanRows = 0;

    const QString filePath = QDir(logDirectory).filePath(
        QString("can_%1.csv").arg(today.toString("yyyyMMdd")));
    canLogFile.setFileName(filePath);
    const bool existed = QFile::exists(filePath);
    if (!canLogFile.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }
    if (!existed) {
        QTextStream stream(&canLogFile);
        stream << "pc_time,channel,direction,id,frame_type,payload_type,dlc,protocol,data,zlg_timestamp_raw\n";
    }
}

QString LogService::levelText(LogLevel level) const
{
    switch (level) {
    case LogLevel::Info:
        return "Info";
    case LogLevel::Warning:
        return "Warning";
    case LogLevel::Error:
        return "Error";
    default:
        return "Unknown";
    }
}

QString LogService::csvEscape(const QString &value) const
{
    QString escaped = value;
    escaped.replace("\"", "\"\"");
    return QString("\"%1\"").arg(escaped);
}
