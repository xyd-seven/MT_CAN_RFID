#include "productiontestservice.h"

#include <QRegularExpression>

namespace {
constexpr quint16 SnDid = 0xE7E1;
constexpr int TagWaitTimeoutMs = 200;
constexpr int SampleTimeoutMs = 1000;
}

ProductionTestService::ProductionTestService(QObject *parent)
    : QObject(parent)
    , m_phase(Phase::Idle)
    , m_waitingTagCompletion(false)
    , m_tagWaitTimer(new QTimer(this))
    , m_sampleTimeoutTimer(new QTimer(this))
{
    m_tagWaitTimer->setSingleShot(true);
    m_sampleTimeoutTimer->setSingleShot(true);
    connect(m_tagWaitTimer, &QTimer::timeout, this, &ProductionTestService::onTagWaitTimeout);
    connect(m_sampleTimeoutTimer, &QTimer::timeout, this, &ProductionTestService::onSampleTimeout);
    m_state.phaseText = QStringLiteral("待扫码");
    m_state.resultText = QStringLiteral("-");
}

bool ProductionTestService::start(const QString &sn, const ProductionTestConfig &config, QString *error)
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
    m_state.totalSamples = m_config.totalSamples;
    m_state.passRateThreshold = m_config.passRateThreshold;
    m_state.resultText = QStringLiteral("RUNNING");
    m_state.startTime = QDateTime::currentDateTime();
    m_lastValidTag.clear();
    m_pendingState = RfidState();
    m_waitingTagCompletion = false;
    setPhase(Phase::WritingSn, QStringLiteral("写入SN"));

    emit logMessage(QStringLiteral("产线检测开始 SN=%1").arg(normalizedSn));
    emit scanControlRequested(false);
    emit logMessage(QStringLiteral("写入SN前停止RFID扫描并清空旧状态"));
    emit writeSnRequested(SnDid, snToBytes(normalizedSn));
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
    if (m_phase != Phase::WritingSn) {
        return;
    }

    if (!success) {
        finishTest(false, QStringLiteral("SN写入失败：%1").arg(message));
        return;
    }

    emit logMessage(QStringLiteral("SN写入成功：%1").arg(message));
    startTesting();
}

void ProductionTestService::handleRfidStatus(const RfidState &state)
{
    if (m_phase != Phase::TestingCard) {
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

bool ProductionTestService::isRunning() const
{
    return m_phase == Phase::WritingSn || m_phase == Phase::TestingCard;
}

bool ProductionTestService::isWritingSn() const
{
    return m_phase == Phase::WritingSn;
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
    emit scanControlRequested(true);
    emit logMessage(QStringLiteral("SN写入完成，发送0x207开始检测，开始读卡测试 %1 次，阈值 %2%")
                        .arg(m_config.totalSamples)
                        .arg(m_config.passRateThreshold, 0, 'f', 2));
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
