#include "productiontestservice.h"
#include "application/qingjurfidservice.h"

#include <QRegularExpression>

namespace {
constexpr quint16 SnDid = 0xE7E1;
constexpr int TagWaitTimeoutMs = 200;
constexpr int SampleTimeoutMs = 1000;
}

ProductionTestService::ProductionTestService(QObject *parent)
    : QObject(parent)
    , m_phase(Phase::Idle)
    , m_protocolMode(0)
    , m_waitingTagCompletion(false)
    , m_tagWaitTimer(new QTimer(this))
    , m_sampleTimeoutTimer(new QTimer(this))
    , m_cardTestStartTime(0)
{
    m_tagWaitTimer->setSingleShot(true);
    m_sampleTimeoutTimer->setSingleShot(true);
    connect(m_tagWaitTimer, &QTimer::timeout, this, &ProductionTestService::onTagWaitTimeout);
    connect(m_sampleTimeoutTimer, &QTimer::timeout, this, &ProductionTestService::onSampleTimeout);
    m_state.phaseText = QStringLiteral("待扫码");
    m_state.resultText = QStringLiteral("-");
}

bool ProductionTestService::start(int protocolMode, const QString &sn, const QString &hwVer, const QString &matChange, const ProductionTestConfig &config, QString *error)
{
    if (isRunning()) {
        if (error != nullptr) {
            *error = QStringLiteral("产线检测正在运行，请等待当前流程完成");
        }
        return false;
    }

    const QString normalizedSn = sn.trimmed().toUpper();
    if (!validateSn(normalizedSn, error)) {
        return false;
    }

    const QString trimmedHwVer = hwVer.trimmed();
    if (!validateHwVersion(trimmedHwVer, nullptr, error)) {
        return false;
    }

    QString trimmedMatChange;
    if (protocolMode == 0) {
        trimmedMatChange = matChange.trimmed();
        if (!validateMaterialChange(trimmedMatChange, nullptr, error)) {
            return false;
        }
    }

    m_protocolMode = protocolMode;
    m_config = config;
    if (m_config.totalSamples <= 0) {
        m_config.totalSamples = 100;
    }
    if (m_config.passRateThreshold < 0.0) {
        m_config.passRateThreshold = 0.0;
    } else if (m_config.passRateThreshold > 100.0) {
        m_config.passRateThreshold = 100.0;
    }

    m_state = ProductionTestState();
    m_state.running = true;
    m_state.phaseText = QStringLiteral("写入SN");
    m_state.sn = normalizedSn;
    m_state.hwVer = trimmedHwVer;
    m_state.matChange = trimmedMatChange;
    m_state.totalSamples = m_config.totalSamples;
    m_state.passRateThreshold = m_config.passRateThreshold;
    m_state.resultText = QStringLiteral("RUNNING");
    m_state.startTime = QDateTime::currentDateTime();
    m_lastValidTag.clear();
    m_pendingState = RfidState();
    m_waitingTagCompletion = false;
    setPhase(Phase::WritingSn, QStringLiteral("写入SN"));

    if (m_protocolMode == 0) {
        emit logMessage(QStringLiteral("产线检测开始 SN=%1 硬件版本=%2 物料变更=%3").arg(normalizedSn).arg(trimmedHwVer).arg(trimmedMatChange));
        emit scanControlRequested(false);
        emit logMessage(QStringLiteral("写入SN前停止RFID扫描并清空旧状态"));
        emit writeSnRequested(SnDid, snToBytes(normalizedSn));
    } else {
        emit logMessage(QStringLiteral("产线检测开始 SN=%1 硬件版本=%2 (青桔协议)").arg(normalizedSn).arg(trimmedHwVer));
        emit scanControlRequested(false);
        emit logMessage(QStringLiteral("写入SN前停止RFID扫描并清空旧状态"));
        emit writeSnRequested(0xA00D, snToBytes(normalizedSn));
    }
    emitStateChanged();
    return true;
}

void ProductionTestService::stop(const QString &reason)
{
    if (!isRunning()) {
        return;
    }

    stopTimers();
    m_state.running = false;
    m_state.finishTime = QDateTime::currentDateTime();
    m_state.lastFailureReason = reason;
    m_state.resultText = QStringLiteral("STOPPED");
    setPhase(Phase::Stopped, QStringLiteral("已停止"));
    emit scanControlRequested(false);
    emit logMessage(QStringLiteral("产线检测停止：%1").arg(reason));
    emit finished(false, m_state);
}

void ProductionTestService::reset()
{
    stopTimers();
    m_phase = Phase::Idle;
    m_config = ProductionTestConfig();
    m_state = ProductionTestState();
    m_state.phaseText = QStringLiteral("待扫码");
    m_state.resultText = QStringLiteral("-");
    m_lastValidTag.clear();
    m_pendingState = RfidState();
    m_waitingTagCompletion = false;
    emitStateChanged();
}

void ProductionTestService::handleWriteFinished(bool success, const QString &message)
{
    if (m_phase == Phase::WritingSn) {
        if (!success) {
            finishTest(false, QStringLiteral("SN写入失败：%1").arg(message));
            return;
        }
        emit logMessage(QStringLiteral("SN写入成功：%1").arg(message));

        setPhase(Phase::WritingHwVersion, QStringLiteral("写入硬件版本"));
        quint16 hwValue = 0;
        validateHwVersion(m_state.hwVer, &hwValue);
        QByteArray data;
        data.append(static_cast<char>((hwValue >> 8) & 0xFF));
        data.append(static_cast<char>(hwValue & 0xFF));
        emit logMessage(QStringLiteral("开始写入硬件版本，值=%1").arg(m_state.hwVer));
        if (m_protocolMode == 0) {
            emit writeSnRequested(0xE7E0, data);
        } else {
            emit writeSnRequested(0xA004, data);
        }
    } else if (m_phase == Phase::WritingHwVersion) {
        if (!success) {
            finishTest(false, QStringLiteral("硬件版本写入失败：%1").arg(message));
            return;
        }
        emit logMessage(QStringLiteral("硬件版本写入成功：%1").arg(message));

        if (m_protocolMode == 0) {
            setPhase(Phase::WritingMaterialChange, QStringLiteral("写入物料变更记录"));
            quint16 matValue = 0;
            validateMaterialChange(m_state.matChange, &matValue);
            QByteArray data;
            data.append(static_cast<char>((matValue >> 8) & 0xFF));
            data.append(static_cast<char>(matValue & 0xFF));
            emit logMessage(QStringLiteral("开始写入物料变更记录，值=%1").arg(m_state.matChange));
            emit writeSnRequested(0xE7E2, data);
        } else {
            // 青桔协议直接进入读卡测试，不写入物料变更记录
            startTesting();
        }
    } else if (m_phase == Phase::WritingMaterialChange) {
        if (!success) {
            finishTest(false, QStringLiteral("物料变更记录写入失败：%1").arg(message));
            return;
        }
        emit logMessage(QStringLiteral("物料变更记录写入成功：%1").arg(message));
        startTesting();
    }
}

void ProductionTestService::handleRfidStatus(const RfidState &state)
{
    if (m_phase != Phase::TestingCard) {
        return;
    }

    // 过滤写码开启检测后的首帧/过渡状态（射频寻卡需要物理时间稳定，延迟 150ms 采样）
    if (QDateTime::currentMSecsSinceEpoch() - m_cardTestStartTime < 150) {
        return;
    }

    if (!state.tag.isEmpty()) {
        m_lastValidTag = state.tag;
        m_state.currentTag = state.tag;
    }

    if (hasFault(state)) {
        recordFailure(state.faultStatus);
        return;
    }

    if (isTagLengthError(state)) {
        recordFailure(QStringLiteral("TAG长度异常"));
        return;
    }

    if (isNoTag(state)) {
        recordFailure(QStringLiteral("未识别到TAG"));
        return;
    }

    if (isCardPresent(state)) {
        if (!state.tag.isEmpty()) {
            recordSuccess(state.tag);
            return;
        }
        m_pendingState = state;
        m_waitingTagCompletion = true;
        m_tagWaitTimer->start(TagWaitTimeoutMs);
    }
}

void ProductionTestService::handleRfidTagUpdate(const QString &tag)
{
    if (m_phase != Phase::TestingCard || tag.isEmpty()) {
        return;
    }

    m_lastValidTag = tag;
    m_state.currentTag = tag;
    if (m_waitingTagCompletion) {
        recordSuccess(tag);
    } else {
        emitStateChanged();
    }
}

void ProductionTestService::handleQingjuStatus(const QingjuNpkState &state)
{
    if (m_phase != Phase::TestingCard) {
        return;
    }

    // 过滤写码开启检测后的首帧/过渡状态（射频寻卡需要物理时间稳定，延迟 150ms 采样）
    if (QDateTime::currentMSecsSinceEpoch() - m_cardTestStartTime < 150) {
        return;
    }

    if (!state.uidText.isEmpty()) {
        m_lastValidTag = state.uidText;
        m_state.currentTag = state.uidText;
    }

    if (state.result == 1) {
        if (!state.uidText.isEmpty()) {
            recordSuccess(state.uidText);
        } else {
            recordFailure(QStringLiteral("UID为空"));
        }
    } else if (state.result == 2) {
        recordFailure(QStringLiteral("未检测到卡片"));
    } else if (state.result == 3) {
        recordFailure(QStringLiteral("读取UID失败"));
    } else if (state.result == 4) {
        recordFailure(QStringLiteral("模块锁定(秘钥错误)"));
    } else if (state.result >= 5 && state.result <= 7) {
        recordFailure(QStringLiteral("读卡故障码：%1").arg(state.result));
    } else {
        recordFailure(state.statusText.isEmpty() ? QStringLiteral("等待检测中") : state.statusText);
    }
}

bool ProductionTestService::isRunning() const
{
    return m_phase == Phase::WritingSn || m_phase == Phase::WritingHwVersion || m_phase == Phase::WritingMaterialChange || m_phase == Phase::TestingCard;
}

bool ProductionTestService::isWritingSn() const
{
    return m_phase == Phase::WritingSn;
}

bool ProductionTestService::isWriting() const
{
    return m_phase == Phase::WritingSn || m_phase == Phase::WritingHwVersion || m_phase == Phase::WritingMaterialChange;
}

bool ProductionTestService::isTestingCard() const
{
    return m_phase == Phase::TestingCard;
}

ProductionTestState ProductionTestService::state() const
{
    return m_state;
}

bool ProductionTestService::validateSn(const QString &sn, QString *error)
{
    const QString normalizedSn = sn.trimmed().toUpper();
    if (normalizedSn.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("SN不能为空");
        return false;
    }
    if (normalizedSn.size() != 16) {
        if (error != nullptr) *error = QStringLiteral("SN必须为16位");
        return false;
    }
    if (!normalizedSn.startsWith(QStringLiteral("R2A3A0"))) {
        if (error != nullptr) *error = QStringLiteral("SN前6位必须为R2A3A0");
        return false;
    }
    static const QRegularExpression pattern(QStringLiteral("^[A-Z0-9]{16}$"));
    if (!pattern.match(normalizedSn).hasMatch()) {
        if (error != nullptr) *error = QStringLiteral("SN仅支持大写字母和数字");
        return false;
    }
    return true;
}

bool ProductionTestService::validateHwVersion(const QString &hwVer, quint16 *versionValue, QString *error)
{
    const QString trimmed = hwVer.trimmed();
    if (trimmed.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("硬件版本不能为空");
        return false;
    }

    static const QRegularExpression versionPattern(QStringLiteral("^(\\d+)\\.0\\.(\\d+)$"));
    const QRegularExpressionMatch versionMatch = versionPattern.match(trimmed);
    if (versionMatch.hasMatch()) {
        bool majorOk = false;
        bool revisionOk = false;
        const int major = versionMatch.captured(1).toInt(&majorOk);
        const int revision = versionMatch.captured(2).toInt(&revisionOk);
        if (!majorOk || !revisionOk || major < 0 || major > 0xFF || revision < 0 || revision > 0xFF) {
            if (error != nullptr) {
                *error = QStringLiteral("硬件版本范围无效，主/子版本范围为 0~255，例如 1.0.1。");
            }
            return false;
        }
        if (versionValue != nullptr) {
            *versionValue = static_cast<quint16>((major << 8) | revision);
        }
        return true;
    }

    QString hexText = trimmed;
    if (hexText.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        hexText = hexText.mid(2);
    }
    bool ok = false;
    const uint val = hexText.toUInt(&ok, 16);
    if (ok && val <= 0xFFFF) {
        if (versionValue != nullptr) {
            *versionValue = static_cast<quint16>(val);
        }
        return true;
    }

    if (error != nullptr) {
        *error = QStringLiteral("硬件版本请输入 A.0.B 格式，例如 1.0.1；也可输入 0x0101。");
    }
    return false;
}

bool ProductionTestService::validateMaterialChange(const QString &matChange, quint16 *value, QString *error)
{
    const QString trimmed = matChange.trimmed();
    if (trimmed.isEmpty()) {
        if (error != nullptr) *error = QStringLiteral("物料更改记录不能为空");
        return false;
    }
    QString hexText = trimmed;
    if (hexText.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
        hexText = hexText.mid(2);
    }
    bool ok = false;
    const uint val = hexText.toUInt(&ok, 16);
    if (ok && val <= 0xFFFF) {
        if (value != nullptr) {
            *value = static_cast<quint16>(val);
        }
        return true;
    }
    if (error != nullptr) {
        *error = QStringLiteral("物料更改记录格式错误，请输入 0x0000 ~ 0xFFFF 范围内的十六进制值，例如 0x0001。");
    }
    return false;
}

QByteArray ProductionTestService::snToBytes(const QString &sn)
{
    return sn.trimmed().toUpper().toLatin1();
}

void ProductionTestService::onTagWaitTimeout()
{
    if (m_phase != Phase::TestingCard || !m_waitingTagCompletion) {
        return;
    }
    m_waitingTagCompletion = false;
    if (!m_lastValidTag.isEmpty()) {
        recordSuccess(m_lastValidTag);
    } else {
        recordFailure(QStringLiteral("识别状态有效但TAG未拼齐"));
    }
}

void ProductionTestService::onSampleTimeout()
{
    if (m_phase != Phase::TestingCard) {
        return;
    }
    recordFailure(QStringLiteral("状态帧超时"));
}

void ProductionTestService::setPhase(Phase phase, const QString &phaseText)
{
    m_phase = phase;
    m_state.phaseText = phaseText;
    emitStateChanged();
}

void ProductionTestService::emitStateChanged()
{
    emit stateChanged(m_state);
}

void ProductionTestService::startTesting()
{
    m_state.completedSamples = 0;
    m_state.successCount = 0;
    m_state.failureCount = 0;
    m_state.successRate = 0.0;
    m_state.currentTag.clear();
    m_state.lastFailureReason.clear();
    m_state.resultText = QStringLiteral("RUNNING");
    m_lastValidTag.clear();
    m_waitingTagCompletion = false;
    setPhase(Phase::TestingCard, QStringLiteral("读卡测试中"));
    m_cardTestStartTime = QDateTime::currentMSecsSinceEpoch();
    emit scanControlRequested(true);
    if (m_protocolMode == 0) {
        emit logMessage(QStringLiteral("SN写入完成，发送0x207开始检测，开始读卡测试 %1 次，阈值 %2%")
                            .arg(m_config.totalSamples)
                            .arg(m_config.passRateThreshold, 0, 'f', 2));
    } else {
        emit logMessage(QStringLiteral("SN写入完成，开始天线扫描，开始读卡测试 %1 次，阈值 %2% (青桔协议)")
                            .arg(m_config.totalSamples)
                            .arg(m_config.passRateThreshold, 0, 'f', 2));
    }
    scheduleSampleTimeout();
}

void ProductionTestService::finishTest(bool passed, const QString &reason)
{
    stopTimers();
    m_state.running = false;
    m_state.finishTime = QDateTime::currentDateTime();
    m_state.lastFailureReason = reason;
    m_state.resultText = passed ? QStringLiteral("PASS") : QStringLiteral("FAIL");
    setPhase(passed ? Phase::Passed : Phase::Failed, passed ? QStringLiteral("PASS") : QStringLiteral("FAIL"));
    emit scanControlRequested(false);
    emit logMessage(QStringLiteral("产线检测完成 SN=%1 结果=%2 成功率=%3% 原因=%4")
                        .arg(m_state.sn)
                        .arg(m_state.resultText)
                        .arg(m_state.successRate, 0, 'f', 2)
                        .arg(reason));
    emit finished(passed, m_state);
}

void ProductionTestService::recordSuccess(const QString &tag)
{
    if (m_phase != Phase::TestingCard || m_state.completedSamples >= m_config.totalSamples) {
        return;
    }

    stopTimers();
    m_waitingTagCompletion = false;
    ++m_state.completedSamples;
    ++m_state.successCount;
    m_state.currentTag = tag;
    m_state.lastFailureReason.clear();
    updateRate();
    emit logMessage(QStringLiteral("#%1 PASS TAG=%2")
                        .arg(m_state.completedSamples, 3, 10, QChar('0'))
                        .arg(tag));

    if (m_state.completedSamples >= m_config.totalSamples) {
        finishTest(m_state.successRate >= m_config.passRateThreshold,
                   m_state.successRate >= m_config.passRateThreshold
                       ? QStringLiteral("达到通过阈值")
                       : QStringLiteral("成功率不足"));
        return;
    }
    scheduleSampleTimeout();
    emitStateChanged();
}

void ProductionTestService::recordFailure(const QString &reason)
{
    if (m_phase != Phase::TestingCard || m_state.completedSamples >= m_config.totalSamples) {
        return;
    }

    stopTimers();
    m_waitingTagCompletion = false;
    ++m_state.completedSamples;
    ++m_state.failureCount;
    m_state.lastFailureReason = reason;
    updateRate();
    emit logMessage(QStringLiteral("#%1 FAIL %2")
                        .arg(m_state.completedSamples, 3, 10, QChar('0'))
                        .arg(reason));

    if (m_state.completedSamples >= m_config.totalSamples) {
        finishTest(m_state.successRate >= m_config.passRateThreshold,
                   m_state.successRate >= m_config.passRateThreshold
                       ? QStringLiteral("达到通过阈值")
                       : QStringLiteral("成功率不足"));
        return;
    }
    scheduleSampleTimeout();
    emitStateChanged();
}

void ProductionTestService::updateRate()
{
    if (m_state.completedSamples <= 0) {
        m_state.successRate = 0.0;
        return;
    }
    m_state.successRate = static_cast<double>(m_state.successCount) * 100.0 /
        static_cast<double>(m_state.completedSamples);
}

void ProductionTestService::scheduleSampleTimeout()
{
    m_sampleTimeoutTimer->start(SampleTimeoutMs);
}

void ProductionTestService::stopTimers()
{
    m_tagWaitTimer->stop();
    m_sampleTimeoutTimer->stop();
}

bool ProductionTestService::isCardPresent(const RfidState &state) const
{
    return state.cardStatus.contains(QStringLiteral("识别到 TAG"));
}

bool ProductionTestService::isNoTag(const RfidState &state) const
{
    return state.cardStatus.contains(QStringLiteral("未识别到 TAG"));
}

bool ProductionTestService::isTagLengthError(const RfidState &state) const
{
    return state.cardStatus.contains(QStringLiteral("TAG 长度异常"));
}

bool ProductionTestService::hasFault(const RfidState &state) const
{
    return !state.faultStatus.isEmpty() &&
        !state.faultStatus.contains(QStringLiteral("无故障"));
}
