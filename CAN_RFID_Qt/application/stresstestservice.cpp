#include "stresstestservice.h"

#include <QDate>
#include <QDir>

namespace {
constexpr int FlushRowThreshold = 100;
}

void StressTestService::start()
{
    currentStats.running = true;
    currentStats.startTime = QDateTime::currentDateTime();
    currentStats.elapsedSeconds = 0;
    currentStats.elapsedMilliseconds = 0;
    elapsedTimer.restart();
}

void StressTestService::stop()
{
    currentStats.running = false;
    if (elapsedTimer.isValid()) {
        currentStats.elapsedMilliseconds = elapsedTimer.elapsed();
        currentStats.elapsedSeconds = currentStats.elapsedMilliseconds / 1000;
    }
    if (sampleCsvFile.isOpen()) {
        sampleCsvFile.flush();
        pendingSampleRows = 0;
    }
}

void StressTestService::reset()
{
    currentStats = StressTestStats();
    tagPart1.clear();
    tagPart2.clear();
    tagPart3.clear();
    uniqueTags.clear();
    if (sampleCsvFile.isOpen()) {
        sampleCsvFile.close();
    }
    pendingSampleRows = 0;
}

void StressTestService::setOutputDirectory(const QString &directoryPath)
{
    outputDirectory = directoryPath;
    QDir().mkpath(outputDirectory);
}

void StressTestService::setAutoSaveEnabled(bool enabled)
{
    autoSaveCsv = enabled;
    if (!autoSaveCsv && sampleCsvFile.isOpen()) {
        sampleCsvFile.flush();
        pendingSampleRows = 0;
    }
}

bool StressTestService::autoSaveEnabled() const
{
    return autoSaveCsv;
}

bool StressTestService::exportSummary(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    const StressTestStats snapshot = stats();
    QTextStream stream(&file);
    stream << "item,value\n";
    stream << "running," << (snapshot.running ? "true" : "false") << '\n';
    stream << "start_time," << csvEscape(snapshot.startTime.toString("yyyy-MM-dd HH:mm:ss.zzz")) << '\n';
    stream << "elapsed_seconds," << snapshot.elapsedSeconds << '\n';
    stream << "total_samples," << snapshot.totalSamples << '\n';
    stream << "success_count," << snapshot.successCount << '\n';
    stream << "no_tag_count," << snapshot.noTagCount << '\n';
    stream << "tag_length_error_count," << snapshot.tagLengthErrorCount << '\n';
    stream << "module_fault_count," << snapshot.moduleFaultCount << '\n';
    stream << "communication_fault_count," << snapshot.communicationFaultCount << '\n';
    stream << "tag_content_error_count," << snapshot.tagContentErrorCount << '\n';
    stream << "valid_tag_count," << snapshot.validTagCount << '\n';
    stream << "success_rate," << QString::number(snapshot.successRate, 'f', 2) << '\n';
    stream << "tag_valid_rate," << QString::number(snapshot.tagValidRate, 'f', 2) << '\n';
    stream << "tag_change_count," << snapshot.tagChangeCount << '\n';
    stream << "unique_tag_count," << snapshot.uniqueTagCount << '\n';
    stream << "max_continuous_failure," << snapshot.maxContinuousFailure << '\n';
    stream << "current_tag," << csvEscape(snapshot.currentTag) << '\n';
    stream << "last_success_tag," << csvEscape(snapshot.lastSuccessTag) << '\n';
    stream << "last_failure_reason," << csvEscape(snapshot.lastFailureReason) << '\n';
    return true;
}

bool StressTestService::handleFrame(const CanFrame &frame)
{
    if (frame.protocol != CanFrameProtocol::ClassicCan ||
        frame.data.size() != RfidProtocol::ClassicCanDlc) {
        return false;
    }

    switch (frame.id) {
    case RfidProtocol::TagPart1FrameId:
    case RfidProtocol::TagPart2FrameId:
    case RfidProtocol::TagPart3FrameId:
        updateTagPart(frame.id, frame.data);
        return true;
    case RfidProtocol::StatusFrameId:
        if (currentStats.running) {
            handleStatusFrame(frame);
        }
        return true;
    default:
        return false;
    }
}

StressTestStats StressTestService::stats() const
{
    StressTestStats snapshot = currentStats;
    if (snapshot.running && snapshot.startTime.isValid()) {
        snapshot.elapsedMilliseconds = elapsedTimer.isValid() ? elapsedTimer.elapsed() : 0;
        snapshot.elapsedSeconds = snapshot.elapsedMilliseconds / 1000;
    }
    return snapshot;
}

void StressTestService::handleStatusFrame(const CanFrame &frame)
{
    const RfidStatus status = RfidProtocol::parseStatusFrame(frame.data);
    if (!status.valid) {
        return;
    }

    ++currentStats.totalSamples;
    if (elapsedTimer.isValid()) {
        currentStats.elapsedMilliseconds = elapsedTimer.elapsed();
        currentStats.elapsedSeconds = currentStats.elapsedMilliseconds / 1000;
    }
    currentStats.currentTag = currentTagText();

    if (status.faultStatus == 0x01) {
        ++currentStats.moduleFaultCount;
    } else if (status.faultStatus == 0x02) {
        ++currentStats.communicationFaultCount;
    }

    switch (status.cardStatus) {
    case 0x01:
        markSuccess();
        break;
    case 0x02:
        ++currentStats.tagLengthErrorCount;
        clearCurrentTag();
        markFailure(QStringLiteral("TAG 长度异常"));
        break;
    case 0x00:
        ++currentStats.noTagCount;
        clearCurrentTag();
        markFailure(QStringLiteral("未识别到 TAG"));
        break;
    default:
        clearCurrentTag();
        markFailure(QStringLiteral("卡状态非法"));
        break;
    }

    updateRates();
    writeSampleCsv(frame, status, status.cardStatus == 0x01, currentTagIsValid());
}

void StressTestService::updateTagPart(quint32 frameId, const QByteArray &payload)
{
    const QString previousTag = currentTagText();
    const QString payloadText = RfidProtocol::parseAsciiPayload(payload);

    if (frameId == RfidProtocol::TagPart1FrameId) {
        tagPart1 = payloadText;
    } else if (frameId == RfidProtocol::TagPart2FrameId) {
        tagPart2 = payloadText;
    } else if (frameId == RfidProtocol::TagPart3FrameId) {
        tagPart3 = payloadText;
    }

    const QString newTag = currentTagText();
    currentStats.currentTag = newTag;
    if (!newTag.isEmpty()) {
        currentStats.lastTagUpdateTime = QDateTime::currentDateTime();
        uniqueTags.insert(newTag);
        currentStats.uniqueTagCount = static_cast<quint64>(uniqueTags.size());
    }
    if (!previousTag.isEmpty() && previousTag != newTag) {
        ++currentStats.tagChangeCount;
    }
}

void StressTestService::updateRates()
{
    if (currentStats.totalSamples == 0) {
        currentStats.successRate = 0.0;
    } else {
        currentStats.successRate = static_cast<double>(currentStats.successCount) * 100.0 /
            static_cast<double>(currentStats.totalSamples);
    }

    if (currentStats.successCount == 0) {
        currentStats.tagValidRate = 0.0;
    } else {
        currentStats.tagValidRate = static_cast<double>(currentStats.validTagCount) * 100.0 /
            static_cast<double>(currentStats.successCount);
    }
}

bool StressTestService::currentTagIsValid() const
{
    const QString tag = currentTagText();
    return isValidTagText(tag);
}

bool StressTestService::isValidTagText(const QString &tag) const
{
    if (tag.length() != 16 && tag.length() != 24) {
        return false;
    }
    for (const QChar ch : tag) {
        if (!ch.isDigit() &&
            !(ch >= QLatin1Char('a') && ch <= QLatin1Char('f')) &&
            !(ch >= QLatin1Char('A') && ch <= QLatin1Char('F'))) {
            return false;
        }
    }
    return true;
}

QString StressTestService::currentTagText() const
{
    return tagPart1 + tagPart2 + tagPart3;
}

void StressTestService::clearCurrentTag()
{
    tagPart1.clear();
    tagPart2.clear();
    tagPart3.clear();
    currentStats.currentTag.clear();
}

void StressTestService::markSuccess()
{
    ++currentStats.successCount;
    ++currentStats.currentContinuousSuccess;
    currentStats.currentContinuousFailure = 0;

    if (currentTagIsValid()) {
        ++currentStats.validTagCount;
        currentStats.lastSuccessTag = currentTagText();
        currentStats.lastFailureReason.clear();
        return;
    }

    ++currentStats.tagContentErrorCount;
    currentStats.lastFailureReason = QStringLiteral("识别成功但 TAG 内容异常");
}

void StressTestService::markFailure(const QString &reason)
{
    ++currentStats.currentContinuousFailure;
    currentStats.currentContinuousSuccess = 0;
    if (currentStats.currentContinuousFailure > currentStats.maxContinuousFailure) {
        currentStats.maxContinuousFailure = currentStats.currentContinuousFailure;
    }
    currentStats.lastFailureReason = reason;
}

void StressTestService::writeSampleCsv(const CanFrame &frame, const RfidStatus &status, bool success, bool tagValid)
{
    if (!autoSaveCsv) {
        return;
    }
    ensureSampleCsvOpen();
    if (!sampleCsvFile.isOpen()) {
        return;
    }

    QTextStream stream(&sampleCsvFile);
    stream << csvEscape(frame.hostDateTime.toString("yyyy-MM-dd HH:mm:ss.zzz")) << ','
           << currentStats.elapsedMilliseconds << ','
           << currentStats.totalSamples << ','
           << static_cast<int>(status.workMode) << ','
           << static_cast<int>(status.cardStatus) << ','
           << static_cast<int>(status.faultStatus) << ','
           << static_cast<int>(status.scanPeriod10ms) * 10 << ','
           << csvEscape(currentStats.currentTag) << ','
           << currentStats.currentTag.length() << ','
           << (success ? "true" : "false") << ','
           << (tagValid ? "true" : "false") << ','
           << csvEscape(currentStats.lastFailureReason) << ','
           << QString::number(currentStats.successRate, 'f', 2) << ','
           << QString::number(currentStats.tagValidRate, 'f', 2) << ','
           << currentStats.currentContinuousFailure << ','
           << currentStats.maxContinuousFailure << ','
           << (frame.hasZlgTimestamp ? QString::number(frame.zlgTimestampRaw) : QString()) << '\n';
    ++pendingSampleRows;
    if (pendingSampleRows >= FlushRowThreshold) {
        sampleCsvFile.flush();
        pendingSampleRows = 0;
    }
}

void StressTestService::ensureSampleCsvOpen()
{
    if (sampleCsvFile.isOpen() || outputDirectory.isEmpty()) {
        return;
    }

    QDir().mkpath(outputDirectory);
    const QString filePath = QDir(outputDirectory).filePath(
        QString("stress_%1.csv").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")));
    sampleCsvFile.setFileName(filePath);
    if (!sampleCsvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }
    pendingSampleRows = 0;

    QTextStream stream(&sampleCsvFile);
    stream << "pc_time,elapsed_ms,total_samples,work_mode,card_status,fault_status,scan_period_ms,"
              "current_tag,tag_length,success,tag_valid,result,success_rate,tag_valid_rate,"
              "continuous_failure,max_continuous_failure,zlg_timestamp_raw\n";
}

QString StressTestService::csvEscape(const QString &value) const
{
    QString escaped = value;
    escaped.replace("\"", "\"\"");
    return QString("\"%1\"").arg(escaped);
}
