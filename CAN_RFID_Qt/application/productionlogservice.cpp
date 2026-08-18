#include "productionlogservice.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>

#include "application/productiontestservice.h"

namespace {
const QStringList EventHeader = QStringList()
    << QStringLiteral("time")
    << QStringLiteral("protocol")
    << QStringLiteral("station")
    << QStringLiteral("device_index")
    << QStringLiteral("sn")
    << QStringLiteral("phase")
    << QStringLiteral("level")
    << QStringLiteral("message");

const QStringList ResultHeader = QStringList()
    << QStringLiteral("record_time")
    << QStringLiteral("start_time")
    << QStringLiteral("finish_time")
    << QStringLiteral("duration_ms")
    << QStringLiteral("protocol")
    << QStringLiteral("station")
    << QStringLiteral("device_index")
    << QStringLiteral("sn")
    << QStringLiteral("target_hw_version")
    << QStringLiteral("target_material_version")
    << QStringLiteral("read_hw_version")
    << QStringLiteral("read_material_version")
    << QStringLiteral("read_device_id")
    << QStringLiteral("total_samples")
    << QStringLiteral("completed_samples")
    << QStringLiteral("success_count")
    << QStringLiteral("failure_count")
    << QStringLiteral("success_rate")
    << QStringLiteral("threshold")
    << QStringLiteral("result")
    << QStringLiteral("failure_reason");

constexpr int ResultColumnIndex = 19;
constexpr int SnColumnIndex = 7;

void addResultToStats(ProductionDailyStats *stats, const QString &result)
{
    if (stats == nullptr) {
        return;
    }
    ++stats->total;
    if (result == QStringLiteral("PASS")) {
        ++stats->passed;
    } else if (result == QStringLiteral("STOPPED")) {
        ++stats->stopped;
    } else {
        ++stats->failed;
    }
}

void removeResultFromStats(ProductionDailyStats *stats, const QString &result)
{
    if (stats == nullptr || stats->total <= 0) {
        return;
    }
    --stats->total;
    if (result == QStringLiteral("PASS")) {
        stats->passed = qMax(0, stats->passed - 1);
    } else if (result == QStringLiteral("STOPPED")) {
        stats->stopped = qMax(0, stats->stopped - 1);
    } else {
        stats->failed = qMax(0, stats->failed - 1);
    }
}
}

double ProductionDailyStats::passRate() const
{
    const int completed = passed + failed;
    return completed > 0 ? static_cast<double>(passed) * 100.0 / completed : 0.0;
}

void ProductionLogService::setOutputDirectory(const QString &directoryPath)
{
    m_outputDirectory = directoryPath.trimmed();
    m_lastError.clear();
    const QString requestedProductionDirectory = productionDirectory();
    if (m_outputDirectory.isEmpty() ||
        !QDir().mkpath(requestedProductionDirectory)) {
        const QString requestedDirectory = requestedProductionDirectory;
        m_outputDirectory = QDir(
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                                .filePath(QStringLiteral("logs"));
        if (!QDir().mkpath(productionDirectory())) {
            m_lastError = QStringLiteral("产线日志目录和备用目录均无法创建：%1；%2")
                              .arg(requestedDirectory, productionDirectory());
        } else {
            m_lastError = QStringLiteral("原产线日志目录不可写，已切换到：%1")
                              .arg(productionDirectory());
        }
    }
    loadDailyStats(QDate::currentDate());
}

QString ProductionLogService::productionDirectory() const
{
    return m_outputDirectory.isEmpty()
               ? QString()
               : QDir(m_outputDirectory).filePath(QStringLiteral("production"));
}

QString ProductionLogService::lastError() const
{
    return m_lastError;
}

bool ProductionLogService::appendEvent(int protocolMode,
                                       int stationNumber,
                                       quint32 deviceIndex,
                                       const ProductionTestState &state,
                                       const QString &level,
                                       const QString &message)
{
    const QDateTime now = QDateTime::currentDateTime();
    return appendCsvRow(
        eventFilePath(now.date()),
        EventHeader,
        QStringList()
            << now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
            << protocolName(protocolMode)
            << (stationNumber > 0 ? QString::number(stationNumber) : QStringLiteral("系统"))
            << (stationNumber > 0 ? QString::number(deviceIndex) : QStringLiteral("-"))
            << state.sn
            << state.phaseText
            << level
            << message);
}

bool ProductionLogService::appendResult(int protocolMode,
                                        int stationNumber,
                                        quint32 deviceIndex,
                                        const ProductionTestState &state,
                                        const QString &readHardwareVersion,
                                        const QString &readMaterialVersion,
                                        const QString &readDeviceId)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime finishTime = state.finishTime.isValid() ? state.finishTime : now;
    const qint64 durationMs = state.startTime.isValid()
                                  ? qMax<qint64>(0, state.startTime.msecsTo(finishTime))
                                  : 0;
    const bool written = appendCsvRow(
        resultFilePath(now.date()),
        ResultHeader,
        QStringList()
            << now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
            << state.startTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
            << finishTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
            << QString::number(durationMs)
            << protocolName(protocolMode)
            << QString::number(stationNumber)
            << QString::number(deviceIndex)
            << state.sn
            << state.hwVer
            << state.matChange
            << readHardwareVersion
            << readMaterialVersion
            << readDeviceId
            << QString::number(state.totalSamples)
            << QString::number(state.completedSamples)
            << QString::number(state.successCount)
            << QString::number(state.failureCount)
            << QString::number(state.successRate, 'f', 2)
            << QString::number(state.passRateThreshold, 'f', 2)
            << state.resultText
            << state.lastFailureReason);
    if (!written) {
        return false;
    }

    if (m_dailyStats.date != now.date()) {
        loadDailyStats(now.date());
    } else {
        const QString snKey = state.sn.trimmed().toUpper();
        const QString newResult = state.resultText.trimmed().toUpper();
        if (m_dailyResultBySn.contains(snKey)) {
            removeResultFromStats(&m_dailyStats, m_dailyResultBySn.value(snKey));
        }
        m_dailyResultBySn.insert(snKey, newResult);
        addResultToStats(&m_dailyStats, newResult);
    }
    return true;
}

ProductionDailyStats ProductionLogService::dailyStats()
{
    const QDate today = QDate::currentDate();
    if (m_dailyStats.date != today) {
        loadDailyStats(today);
    }
    return m_dailyStats;
}

bool ProductionLogService::appendCsvRow(const QString &filePath,
                                        const QStringList &header,
                                        const QStringList &values)
{
    m_lastError.clear();
    if (filePath.isEmpty()) {
        m_lastError = QStringLiteral("产线日志目录未配置");
        return false;
    }
    if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) {
        m_lastError = QStringLiteral("无法创建产线日志目录：%1")
                          .arg(QFileInfo(filePath).absolutePath());
        return false;
    }

    QFile file(filePath);
    const bool writeHeader = !file.exists() || file.size() == 0;
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        m_lastError = QStringLiteral("无法写入产线日志：%1").arg(filePath);
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    if (writeHeader) {
        stream.setGenerateByteOrderMark(true);
        QStringList escapedHeader;
        for (const QString &field : header) {
            escapedHeader.append(csvEscape(field));
        }
        stream << escapedHeader.join(QLatin1Char(',')) << '\n';
    }
    QStringList escapedValues;
    for (const QString &value : values) {
        escapedValues.append(csvEscape(value));
    }
    stream << escapedValues.join(QLatin1Char(',')) << '\n';
    file.flush();
    return true;
}

void ProductionLogService::loadDailyStats(const QDate &date)
{
    m_dailyStats = ProductionDailyStats();
    m_dailyStats.date = date;
    m_dailyResultBySn.clear();
    const QString filePath = resultFilePath(date);
    if (filePath.isEmpty() || !QFile::exists(filePath)) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lastError = QStringLiteral("无法读取产线统计日志：%1").arg(filePath);
        return;
    }
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    bool headerSkipped = false;
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (!headerSkipped) {
            headerSkipped = true;
            continue;
        }
        const QStringList fields = parseCsvLine(line);
        if (fields.size() <= ResultColumnIndex || fields.size() <= SnColumnIndex) {
            continue;
        }
        const QString sn = fields.at(SnColumnIndex).trimmed().toUpper();
        if (sn.isEmpty()) {
            continue;
        }
        const QString result = fields.at(ResultColumnIndex).trimmed().toUpper();
        m_dailyResultBySn.insert(sn, result);
    }
    for (auto iterator = m_dailyResultBySn.constBegin();
         iterator != m_dailyResultBySn.constEnd();
         ++iterator) {
        addResultToStats(&m_dailyStats, iterator.value());
    }
}

QString ProductionLogService::eventFilePath(const QDate &date) const
{
    return productionDirectory().isEmpty()
               ? QString()
               : QDir(productionDirectory())
                     .filePath(QStringLiteral("production_events_%1.csv")
                                   .arg(date.toString(QStringLiteral("yyyyMMdd"))));
}

QString ProductionLogService::resultFilePath(const QDate &date) const
{
    return productionDirectory().isEmpty()
               ? QString()
               : QDir(productionDirectory())
                     .filePath(QStringLiteral("production_results_%1.csv")
                                   .arg(date.toString(QStringLiteral("yyyyMMdd"))));
}

QString ProductionLogService::protocolName(int protocolMode)
{
    return protocolMode == 1 ? QStringLiteral("青桔") : QStringLiteral("美团");
}

QString ProductionLogService::csvEscape(const QString &value)
{
    QString safeValue = value;
    if (!safeValue.isEmpty() && QStringLiteral("=+-@").contains(safeValue.at(0))) {
        safeValue.prepend(QLatin1Char('\''));
    }
    safeValue.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    if (safeValue.contains(QLatin1Char(',')) ||
        safeValue.contains(QLatin1Char('"')) ||
        safeValue.contains(QLatin1Char('\r')) ||
        safeValue.contains(QLatin1Char('\n'))) {
        safeValue = QLatin1Char('"') + safeValue + QLatin1Char('"');
    }
    return safeValue;
}

QStringList ProductionLogService::parseCsvLine(const QString &line)
{
    QStringList fields;
    QString current;
    bool quoted = false;
    for (int index = 0; index < line.size(); ++index) {
        const QChar character = line.at(index);
        if (character == QLatin1Char('"')) {
            if (quoted && index + 1 < line.size() && line.at(index + 1) == QLatin1Char('"')) {
                current.append(character);
                ++index;
            } else {
                quoted = !quoted;
            }
        } else if (character == QLatin1Char(',') && !quoted) {
            fields.append(current);
            current.clear();
        } else {
            current.append(character);
        }
    }
    fields.append(current);
    return fields;
}
