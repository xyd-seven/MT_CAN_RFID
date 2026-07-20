#include "logservice.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QThread>
#include <QWaitCondition>

namespace {
constexpr int FlushRowThreshold = 100;
constexpr qint64 FastFrameDelayThresholdMs = 150;
constexpr qint64 SlowFrameDelayThresholdMs = 15000;

qint64 timingDelayThresholdMs(quint32 canId)
{
    return (canId >= 0x2C3 && canId <= 0x2C5)
        ? SlowFrameDelayThresholdMs
        : FastFrameDelayThresholdMs;
}
}

class CanLogWriter final : public QThread
{
public:
    CanLogWriter()
    {
        start();
    }

    ~CanLogWriter() override
    {
        shutdown();
    }

    void configure(const QString &path, bool enabled)
    {
        QMutexLocker locker(&mutex);
        targetPath = path;
        writeEnabled = enabled;
        reopenRequested = true;
        condition.wakeOne();
    }

    void enqueue(const QString &line)
    {
        QMutexLocker locker(&mutex);
        if (!writeEnabled || stopping) {
            return;
        }
        pendingLines.enqueue(line);
        condition.wakeOne();
    }

    void flushAndDisable()
    {
        QMutexLocker locker(&mutex);
        if (!writeEnabled) {
            return;
        }
        disableRequested = true;
        disableCompleted = false;
        condition.wakeOne();
        while (!disableCompleted) {
            disableCondition.wait(&mutex);
        }
    }

    void shutdown()
    {
        {
            QMutexLocker locker(&mutex);
            if (stopping) {
                return;
            }
            stopping = true;
            condition.wakeAll();
        }
        wait();
    }

protected:
    void run() override
    {
        QFile file;
        QString openedPath;
        int rowsSinceFlush = 0;
        for (;;) {
            QQueue<QString> lines;
            QString path;
            bool enabled = false;
            bool reopen = false;
            bool shouldDisable = false;
            bool shouldStop = false;
            {
                QMutexLocker locker(&mutex);
                while (pendingLines.isEmpty() && !reopenRequested && !disableRequested && !stopping) {
                    condition.wait(&mutex);
                }
                lines.swap(pendingLines);
                path = targetPath;
                enabled = writeEnabled;
                reopen = reopenRequested;
                reopenRequested = false;
                shouldDisable = disableRequested;
                shouldStop = stopping;
            }

            if (reopen || !enabled || openedPath != path) {
                if (file.isOpen()) {
                    file.flush();
                    file.close();
                }
                openedPath.clear();
                rowsSinceFlush = 0;
                if (enabled && !path.isEmpty()) {
                    const bool existed = QFile::exists(path);
                    file.setFileName(path);
                    if (file.open(QIODevice::Append | QIODevice::Text)) {
                        openedPath = path;
                        if (!existed) {
                            QTextStream stream(&file);
                            stream << "pc_time\tchannel\tdirection\tid\tframe_type\tpayload_type\tdlc\tprotocol\tdata\n";
                        }
                    }
                }
            }

            if (file.isOpen()) {
                QTextStream stream(&file);
                while (!lines.isEmpty()) {
                    stream << lines.dequeue() << '\n';
                    ++rowsSinceFlush;
                }
                if (rowsSinceFlush >= FlushRowThreshold || shouldStop) {
                    file.flush();
                    rowsSinceFlush = 0;
                }
            }

            if (shouldDisable) {
                if (file.isOpen()) {
                    file.flush();
                    file.close();
                }
                openedPath.clear();
                rowsSinceFlush = 0;
                QMutexLocker locker(&mutex);
                writeEnabled = false;
                disableRequested = false;
                disableCompleted = true;
                disableCondition.wakeAll();
            }

            if (shouldStop) {
                if (file.isOpen()) {
                    file.flush();
                    file.close();
                }
                return;
            }
        }
    }

private:
    QMutex mutex;
    QWaitCondition condition;
    QWaitCondition disableCondition;
    QQueue<QString> pendingLines;
    QString targetPath;
    bool writeEnabled = false;
    bool reopenRequested = false;
    bool disableRequested = false;
    bool disableCompleted = false;
    bool stopping = false;
};

LogService::LogService() :
    canAutoSaveEnabled(false),
    pendingRuntimeRows(0),
    pendingSerialRows(0),
    canLogWriter(new CanLogWriter()),
    canTimingMeasurementActive(false)
{
}

LogService::~LogService()
{
    if (runtimeLogFile.isOpen()) {
        runtimeLogFile.flush();
        runtimeLogFile.close();
    }
    delete canLogWriter;
    canLogWriter = nullptr;
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
        canLogWriter->flushAndDisable();
        if (serialLogFile.isOpen()) {
            serialLogFile.flush();
            serialLogFile.close();
        }
        runtimeLogDate = QDate();
        serialLogDate = QDate();
        pendingRuntimeRows = 0;
        pendingSerialRows = 0;
    }
    logDirectory = directoryPath;
    QDir().mkpath(logDirectory);
    if (canAutoSaveEnabled) {
        canLogWriter->configure(canLogFilePath(), true);
    }
}

void LogService::setCanAutoSaveEnabled(bool enabled)
{
    canAutoSaveEnabled = enabled;
    if (canAutoSaveEnabled) {
        autoSaveSessionTime = QDateTime::currentDateTime();
        canLogWriter->configure(canLogFilePath(), true);
    } else {
        canLogWriter->flushAndDisable();
        if (serialLogFile.isOpen()) {
            serialLogFile.flush();
            serialLogFile.close();
            pendingSerialRows = 0;
        }
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
    recordCanTiming(frame);
    if (!canAutoSaveEnabled) {
        return;
    }

    const QString row = textField(frame.hostDateTime.toString("yyyy-MM-dd HH:mm:ss.zzz")) + '\t'
        + QString::number(frame.channel) + '\t'
        + textField(frame.directionText()) + '\t'
        + textField(frame.idText()) + '\t'
        + textField(frame.frameTypeText()) + '\t'
        + textField(frame.payloadTypeText()) + '\t'
        + QString::number(frame.dlc()) + '\t'
        + textField(frame.protocolText()) + '\t'
        + textField(frame.dataText().trimmed());
    canLogWriter->enqueue(row);
}

void LogService::beginCanTimingMeasurement()
{
    canTimingRows.clear();
    canTimingMeasurementActive = true;
}

QString LogService::finishCanTimingMeasurement()
{
    canTimingMeasurementActive = false;
    QStringList parts;
    const QList<quint32> ids = {0x207, 0x2C0, 0x2C1, 0x2C2, 0x2C3, 0x2C4, 0x2C5, 0x2C6};
    for (quint32 id : ids) {
        const CanTimingRow row = canTimingRows.value(id);
        if (row.count < 2) {
            continue;
        }
        const qint64 intervalCount = static_cast<qint64>(row.count - 1);
        const qint64 thresholdMs = timingDelayThresholdMs(id);
        parts << QString("0x%1：%2帧，平均%3ms，最大%4ms，超%5ms %6次")
                     .arg(id, 0, 16)
                     .arg(row.count)
                     .arg(static_cast<double>(row.intervalTotalMs) / intervalCount, 0, 'f', 1)
                     .arg(row.intervalMaxMs)
                     .arg(thresholdMs)
                     .arg(row.delayedCount);
    }
    const QString summary = parts.isEmpty()
        ? QStringLiteral("压测时序摘要：未采集到足够的 0x207 / 0x2C0~0x2C6 报文")
        : QStringLiteral("压测时序摘要：") + parts.join(QStringLiteral("；"));
    writeCanTimingSummary(summary);
    return summary;
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

QString LogService::canLogFilePath() const
{
    if (logDirectory.isEmpty()) {
        return QString();
    }
    const QDateTime sessionTime = autoSaveSessionTime.isValid()
        ? autoSaveSessionTime : QDateTime::currentDateTime();
    return QDir(logDirectory).filePath(
        QString("can_%1.txt").arg(sessionTime.toString("yyyyMMdd_HHmmss")));
}

void LogService::recordCanTiming(const CanFrame &frame)
{
    if (!canTimingMeasurementActive ||
        (frame.id != 0x207 && (frame.id < 0x2C0 || frame.id > 0x2C6))) {
        return;
    }
    CanTimingRow &row = canTimingRows[frame.id];
    if (row.lastElapsedMs >= 0 && frame.monotonicElapsedMs >= row.lastElapsedMs) {
        const qint64 intervalMs = frame.monotonicElapsedMs - row.lastElapsedMs;
        row.intervalTotalMs += intervalMs;
        row.intervalMaxMs = qMax(row.intervalMaxMs, intervalMs);
        if (intervalMs > timingDelayThresholdMs(frame.id)) {
            ++row.delayedCount;
        }
    }
    row.lastElapsedMs = frame.monotonicElapsedMs;
    ++row.count;
}

void LogService::writeCanTimingSummary(const QString &summary) const
{
    if (!canAutoSaveEnabled || logDirectory.isEmpty()) {
        return;
    }
    const QString canPath = canLogFilePath();
    const QFileInfo info(canPath);
    QFile file(info.dir().filePath(info.completeBaseName() + QStringLiteral("_timing_summary.txt")));
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")
           << '\t' << summary << '\n';
}

void LogService::ensureSerialLogOpen()
{
    if (logDirectory.isEmpty()) {
        return;
    }

    if (serialLogFile.isOpen()) {
        return;
    }
    pendingSerialRows = 0;

    if (!autoSaveSessionTime.isValid()) {
        autoSaveSessionTime = QDateTime::currentDateTime();
    }

    const QString filePath = QDir(logDirectory).filePath(
        QString("serial_%1.txt").arg(autoSaveSessionTime.toString("yyyyMMdd_HHmmss")));
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
