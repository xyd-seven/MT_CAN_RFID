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
    pendingCanRows(0),
    pendingSerialRows(0)
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
    if (serialLogFile.isOpen()) {
        serialLogFile.flush();
        serialLogFile.close();
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
        if (serialLogFile.isOpen()) {
            serialLogFile.flush();
            serialLogFile.close();
        }
        runtimeLogDate = QDate();
        canLogDate = QDate();
        serialLogDate = QDate();
        pendingRuntimeRows = 0;
        pendingCanRows = 0;
        pendingSerialRows = 0;
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
    if (!canAutoSaveEnabled && serialLogFile.isOpen()) {
        serialLogFile.flush();
        serialLogFile.close();
        pendingSerialRows = 0;
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
    stream << textField(frame.hostDateTime.toString("yyyy-MM-dd HH:mm:ss.zzz")) << '\t'
           << frame.channel << '\t'
           << textField(frame.directionText()) << '\t'
           << textField(frame.idText()) << '\t'
           << textField(frame.frameTypeText()) << '\t'
           << textField(frame.payloadTypeText()) << '\t'
           << frame.dlc() << '\t'
           << textField(frame.protocolText()) << '\t'
           << textField(frame.dataText().trimmed()) << '\t'
           << (frame.hasZlgTimestamp ? QString::number(frame.zlgTimestampRaw) : QString()) << '\n';
    ++pendingCanRows;
    if (pendingCanRows >= FlushRowThreshold) {
        canLogFile.flush();
        pendingCanRows = 0;
    }
}

void LogService::logSerialFrame(bool isTx,
                                const QByteArray &data,
                                const QString &protocolId,
                                const QString &decodeText)
{
    if (!canAutoSaveEnabled) {
        return;
    }

    ensureSerialLogOpen();
    if (!serialLogFile.isOpen()) {
        return;
    }

    QTextStream stream(&serialLogFile);
    stream << textField(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")) << '\t'
           << textField(QStringLiteral("RS485")) << '\t'
           << textField(isTx ? QStringLiteral("发送") : QStringLiteral("接收")) << '\t'
           << textField(protocolId) << '\t'
           << textField(QStringLiteral("数据帧")) << '\t'
           << textField(QStringLiteral("-")) << '\t'
           << data.size() << '\t'
           << textField(QStringLiteral("-")) << '\t'
           << textField(QString::fromLatin1(data.toHex(' ').toUpper())) << '\t'
           << textField(decodeText) << '\n';
    ++pendingSerialRows;
    if (pendingSerialRows >= FlushRowThreshold) {
        serialLogFile.flush();
        pendingSerialRows = 0;
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
        QString("can_%1.txt").arg(today.toString("yyyyMMdd")));
    canLogFile.setFileName(filePath);
    const bool existed = QFile::exists(filePath);
    if (!canLogFile.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }
    if (!existed) {
        QTextStream stream(&canLogFile);
        stream << "pc_time\tchannel\tdirection\tid\tframe_type\tpayload_type\tdlc\tprotocol\tdata\tzlg_timestamp_raw\n";
    }
}

void LogService::ensureSerialLogOpen()
{
    if (logDirectory.isEmpty()) {
        return;
    }

    const QDate today = QDate::currentDate();
    if (serialLogFile.isOpen() && serialLogDate == today) {
        return;
    }
    if (serialLogFile.isOpen()) {
        serialLogFile.flush();
        serialLogFile.close();
    }
    serialLogDate = today;
    pendingSerialRows = 0;

    const QString filePath = QDir(logDirectory).filePath(
        QString("serial_%1.txt").arg(today.toString("yyyyMMdd")));
    serialLogFile.setFileName(filePath);
    const bool existed = QFile::exists(filePath);
    if (!serialLogFile.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }
    if (!existed) {
        QTextStream stream(&serialLogFile);
        stream << "pc_time\tchannel\tdirection\tid\tframe_type\tpayload_type\tdlc\tprotocol\tdata\tprotocol_decode\n";
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

QString LogService::textField(const QString &value) const
{
    QString res = value;
    res.replace('\r', ' ');
    res.replace('\n', ' ');
    res.replace('\t', ' ');
    return res;
}
