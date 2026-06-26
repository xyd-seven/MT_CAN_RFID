#include "stresstestservice.h"

#include "qingjurfidservice.h"

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
    stream << "poll_skipped_count," << snapshot.pollSkippedCount << '\n';
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

bool StressTestService::handleQingjuState(const QingjuNpkState &state)
{
    if (!currentStats.running || !state.statusSample || state.statusText.isEmpty()) {
        return false;
    }

    ++currentStats.totalSamples;
    if (elapsedTimer.isValid()) {
        currentStats.elapsedMilliseconds = elapsedTimer.elapsed();
        currentStats.elapsedSeconds = currentStats.elapsedMilliseconds / 1000;
    }

    QString tagText = state.uidText.trimmed();
    if (tagText.isEmpty()) {
        tagText = QString("%1%2%3")
            .arg(state.assetModel.trimmed(), state.assetSupplier.trimmed(), state.assetSerial.trimmed());
    }
    currentStats.currentTag = tagText;

    bool success = false;
    bool tagValid = isValidTagText(tagText);

    switch (state.result) {
    case 1:
        success = true;
        ++currentStats.successCount;
        ++currentStats.currentContinuousSuccess;
        currentStats.currentContinuousFailure = 0;
        if (tagValid) {
            ++currentStats.validTagCount;
            currentStats.lastSuccessTag = tagText;
            currentStats.lastFailureReason.clear();
            currentStats.lastTagUpdateTime = QDateTime::currentDateTime();
            if (!tagText.isEmpty()) {
                uniqueTags.insert(tagText);
                currentStats.uniqueTagCount = static_cast<quint64>(uniqueTags.size());
            }
        } else {
            ++currentStats.tagContentErrorCount;
            currentStats.lastFailureReason = QStringLiteral("识别成功但 UID 内容异常");
        }
        break;
    case 2:
        ++currentStats.noTagCount;
        markFailure(QStringLiteral("未识别到 TAG"));
        break;
    case 3:
        ++currentStats.tagLengthErrorCount;
        markFailure(QStringLiteral("检测到标签，读取UID失败"));
        break;
    case 4:
    case 5:
    case 6:
    case 7:
        ++currentStats.moduleFaultCount;
        markFailure(state.statusText);
        break;
    default:
        markFailure(state.statusText.isEmpty() ? QStringLiteral("青桔读卡状态未知") : state.statusText);
        break;
    }

    updateRates();
    writeQingjuSampleCsv(state, success, tagValid);
    return true;
}

bool StressTestService::handleRs485State(int protocolMode, const QString &tagId, int errCode, bool isCommunicationTimeout, const QString &errorMsg)
{
    if (!currentStats.running) {
        return false;
    }

    ++currentStats.totalSamples;
    if (elapsedTimer.isValid()) {
        currentStats.elapsedMilliseconds = elapsedTimer.elapsed();
        currentStats.elapsedSeconds = currentStats.elapsedMilliseconds / 1000;
    }

    bool success = false;
    bool tagValid = false;
    QString cleanTag = tagId.trimmed();

    bool isSuccess = false;
    bool isNoTag = false;
    if (protocolMode == 4) {
        isSuccess = (errCode == 2);
        isNoTag = (errCode == 1);
    } else {
        isSuccess = (errCode == 0);
        isNoTag = (protocolMode == 2 && errCode == 0x15) || (protocolMode == 3 && errCode == -1);
    }

    if (isCommunicationTimeout) {
        ++currentStats.communicationFaultCount;
        ++currentStats.currentContinuousFailure;
        currentStats.currentContinuousSuccess = 0;
        if (currentStats.currentContinuousFailure > currentStats.maxContinuousFailure) {
            currentStats.maxContinuousFailure = currentStats.currentContinuousFailure;
        }
        currentStats.lastFailureReason = errorMsg.isEmpty() ? QStringLiteral("从机应答超时") : errorMsg;
    } 
    else if (isSuccess) { // Success tag reading
        success = true;
        currentStats.currentTag = cleanTag;
        ++currentStats.successCount;
        ++currentStats.currentContinuousSuccess;
        currentStats.currentContinuousFailure = 0;
        
        tagValid = isValidTagText(cleanTag);
        if (tagValid) {
            ++currentStats.validTagCount;
            if (currentStats.lastSuccessTag != cleanTag) {
                currentStats.lastSuccessTag = cleanTag;
                ++currentStats.tagChangeCount;
            }
            currentStats.lastFailureReason.clear();
            currentStats.lastTagUpdateTime = QDateTime::currentDateTime();
            if (!cleanTag.isEmpty()) {
                uniqueTags.insert(cleanTag);
                currentStats.uniqueTagCount = static_cast<quint64>(uniqueTags.size());
            }
        } else {
            ++currentStats.tagContentErrorCount;
            currentStats.lastFailureReason = QStringLiteral("识别成功但 UID 内容异常");
        }
    } 
    else { // Reader returned error (e.g. no tag)
        currentStats.currentTag.clear();
        ++currentStats.currentContinuousFailure;
        currentStats.currentContinuousSuccess = 0;
        if (currentStats.currentContinuousFailure > currentStats.maxContinuousFailure) {
            currentStats.maxContinuousFailure = currentStats.currentContinuousFailure;
        }

        if (isNoTag) {
            ++currentStats.noTagCount;
            currentStats.lastFailureReason = QStringLiteral("未扫描到标签");
        } else {
            ++currentStats.moduleFaultCount;
            currentStats.lastFailureReason = errorMsg.isEmpty() ? QStringLiteral("设备返回错误") : errorMsg;
        }
    }

    updateRates();
    writeRs485SampleCsv(protocolMode, cleanTag, errCode, isCommunicationTimeout, errorMsg, success, tagValid);
    return true;
}

void StressTestService::recordRs485PollSkipped()
{
    if (!currentStats.running) {
        return;
    }

    ++currentStats.pollSkippedCount;
    if (elapsedTimer.isValid()) {
        currentStats.elapsedMilliseconds = elapsedTimer.elapsed();
        currentStats.elapsedSeconds = currentStats.elapsedMilliseconds / 1000;
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
    if (isValidTagText(newTag)) {
        currentStats.currentTag = newTag;
        if (!uniqueTags.contains(newTag)) {
            uniqueTags.insert(newTag);
            currentStats.lastTagUpdateTime = QDateTime::currentDateTime();
            currentStats.uniqueTagCount = static_cast<quint64>(uniqueTags.size());
        }
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
           << currentStats.pollSkippedCount << ','
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

void StressTestService::writeQingjuSampleCsv(const QingjuNpkState &state, bool success, bool tagValid)
{
    if (!autoSaveCsv) {
        return;
    }
    ensureSampleCsvOpen();
    if (!sampleCsvFile.isOpen()) {
        return;
    }

    QTextStream stream(&sampleCsvFile);
    stream << csvEscape(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")) << ','
           << currentStats.elapsedMilliseconds << ','
           << currentStats.totalSamples << ','
           << currentStats.pollSkippedCount << ','
           << "qingju" << ','
           << static_cast<int>(state.result) << ','
           << static_cast<int>(state.alarm) << ','
           << QString() << ','
           << csvEscape(currentStats.currentTag) << ','
           << currentStats.currentTag.length() << ','
           << (success ? "true" : "false") << ','
           << (tagValid ? "true" : "false") << ','
           << csvEscape(currentStats.lastFailureReason) << ','
           << QString::number(currentStats.successRate, 'f', 2) << ','
           << QString::number(currentStats.tagValidRate, 'f', 2) << ','
           << currentStats.currentContinuousFailure << ','
           << currentStats.maxContinuousFailure << ','
           << csvEscape(state.statusText) << '\n';
    ++pendingSampleRows;
    if (pendingSampleRows >= FlushRowThreshold) {
        sampleCsvFile.flush();
        pendingSampleRows = 0;
    }
}

void StressTestService::writeRs485SampleCsv(int protocolMode, const QString &tagId, int errCode, bool isCommunicationTimeout, const QString &errorMsg, bool success, bool tagValid)
{
    if (!autoSaveCsv) {
        return;
    }
    ensureSampleCsvOpen();
    if (!sampleCsvFile.isOpen()) {
        return;
    }

    QTextStream stream(&sampleCsvFile);
    stream << csvEscape(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")) << ','
           << currentStats.elapsedMilliseconds << ','
           << currentStats.totalSamples << ','
           << currentStats.pollSkippedCount << ','
           << (protocolMode == 2 ? "rs485_bb" : (protocolMode == 3 ? "rs485_ff" : "rs485_haluo")) << ','
           << errCode << ','
           << (isCommunicationTimeout ? 1 : 0) << ','
           << QString() << ','
           << csvEscape(tagId) << ','
           << tagId.length() << ','
           << (success ? "true" : "false") << ','
           << (tagValid ? "true" : "false") << ','
           << csvEscape(currentStats.lastFailureReason) << ','
           << QString::number(currentStats.successRate, 'f', 2) << ','
           << QString::number(currentStats.tagValidRate, 'f', 2) << ','
           << currentStats.currentContinuousFailure << ','
           << currentStats.maxContinuousFailure << ','
           << csvEscape(errorMsg) << '\n';
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
    stream << "pc_time,elapsed_ms,total_samples,poll_skipped_count,work_mode,card_status,fault_status,scan_period_ms,"
              "current_tag,tag_length,success,tag_valid,result,success_rate,tag_valid_rate,"
              "continuous_failure,max_continuous_failure,zlg_timestamp_raw\n";
}

QString StressTestService::csvEscape(const QString &value) const
{
    QString escaped = value;
    escaped.replace("\"", "\"\"");
    return QString("\"%1\"").arg(escaped);
}
