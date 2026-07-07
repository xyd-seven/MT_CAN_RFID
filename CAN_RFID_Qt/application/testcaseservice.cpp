#include "testcaseservice.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QRegExp>
#include <QTextStream>

#include <climits>

namespace {
const int kBroadcastFirstId = 0x2C0;
const int kBroadcastLastId = 0x2C6;
const int kMaxExtractedFrames = 20;

struct EvidenceLogFrame
{
    QDateTime timestamp;
    QString direction;
    QString channel;
    int canId = -1;
    QString canIdText;
    QString dataHex;
    QString decodedText;
};

struct PeriodStats
{
    int frameCount = 0;
    int sampleCount = 0;
    qint64 minMs = 0;
    qint64 maxMs = 0;
    double averageMs = 0.0;
    bool valid = false;
};

QString jsonString(const QJsonObject &object, const char *key)
{
    return object.value(QString::fromLatin1(key)).toString().trimmed();
}

int jsonInt(const QJsonObject &object, const char *key, int defaultValue)
{
    const QJsonValue value = object.value(QString::fromLatin1(key));
    return value.isDouble() ? value.toInt(defaultValue) : defaultValue;
}

bool jsonBool(const QJsonObject &object, const char *key, bool defaultValue)
{
    const QJsonValue value = object.value(QString::fromLatin1(key));
    return value.isBool() ? value.toBool(defaultValue) : defaultValue;
}

QStringList evidenceTextLines(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    QStringList normalized;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        line.replace(QLatin1Char(','), QString::fromUtf8("，"));
        normalized.append(line);
    }
    if (normalized.isEmpty()) {
        normalized.append(QString::fromUtf8("（无）"));
    }
    return normalized;
}

QString evidenceFieldLine(const QString &label, const QString &value)
{
    QString text = QString::fromUtf8("%1：%2").arg(label, value.trimmed().isEmpty() ? QString::fromUtf8("（无）") : value.trimmed());
    text.replace(QLatin1Char(','), QString::fromUtf8("，"));
    return text;
}

QStringList parseEvidenceCsvLine(const QString &line)
{
    QStringList fields;
    QString current;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (ch == QLatin1Char('"')) {
            if (quoted && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                current.append(ch);
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (ch == QLatin1Char(',') && !quoted) {
            fields.append(current);
            current.clear();
        } else {
            current.append(ch);
        }
    }
    fields.append(current);
    return fields;
}

bool parseEvidenceCanId(const QString &text, int *canId)
{
    QString value = text.trimmed().toUpper();
    if (value.startsWith(QStringLiteral("0X"))) {
        value.remove(0, 2);
    }
    bool ok = false;
    const int parsed = value.toInt(&ok, 16);
    if (!ok || parsed < 0 || parsed > 0x7FF) {
        return false;
    }
    if (canId != nullptr) {
        *canId = parsed;
    }
    return true;
}

bool isTransmitEvidence(const EvidenceLogFrame &frame)
{
    const QString direction = frame.direction.trimmed();
    return direction == QString::fromUtf8("发送") ||
           direction.compare(QStringLiteral("TX"), Qt::CaseInsensitive) == 0 ||
           direction.contains(QString::fromUtf8("发送"));
}

bool isReceiveEvidence(const EvidenceLogFrame &frame)
{
    const QString direction = frame.direction.trimmed();
    return direction == QString::fromUtf8("接收") ||
           direction.compare(QStringLiteral("RX"), Qt::CaseInsensitive) == 0 ||
           direction.contains(QString::fromUtf8("接收"));
}

bool isBroadcastEvidence(const EvidenceLogFrame &frame)
{
    return isReceiveEvidence(frame) && frame.canId >= kBroadcastFirstId && frame.canId <= kBroadcastLastId;
}

bool isControl207Evidence(const EvidenceLogFrame &frame)
{
    return frame.canId == 0x207;
}

QString formatCanId(int canId)
{
    return QStringLiteral("0x%1").arg(canId, 3, 16, QChar('0')).toUpper().replace(QStringLiteral("0X"), QStringLiteral("0x"));
}

QString evidenceFrameLine(const EvidenceLogFrame &frame, int index)
{
    return QString::fromUtf8("%1. 时间=%2；方向=%3；通道=%4；CAN ID=%5；Data=%6；解析=%7")
        .arg(index)
        .arg(frame.timestamp.isValid() ? frame.timestamp.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")) : QString::fromUtf8("（无）"))
        .arg(frame.direction)
        .arg(frame.channel)
        .arg(frame.canIdText)
        .arg(frame.dataHex)
        .arg(frame.decodedText.trimmed().isEmpty() ? QString::fromUtf8("（无）") : frame.decodedText.trimmed());
}

QString caseEvidenceText(const TestCase *testCase);
bool caseRequiresTransmitEvidence(const TestCase *testCase);
bool caseRequiresReceiveEvidence(const TestCase *testCase);

PeriodStats calculatePeriodStats(const QVector<EvidenceLogFrame> &frames)
{
    PeriodStats stats;
    stats.frameCount = frames.size();

    qint64 minMs = LLONG_MAX;
    qint64 maxMs = 0;
    qint64 totalMs = 0;
    int periodCount = 0;
    for (int i = 1; i < frames.size(); ++i) {
        if (!frames.at(i - 1).timestamp.isValid() || !frames.at(i).timestamp.isValid()) {
            continue;
        }
        const qint64 intervalMs = frames.at(i - 1).timestamp.msecsTo(frames.at(i).timestamp);
        if (intervalMs < 0) {
            continue;
        }
        minMs = qMin(minMs, intervalMs);
        maxMs = qMax(maxMs, intervalMs);
        totalMs += intervalMs;
        ++periodCount;
    }
    if (periodCount <= 0) {
        return stats;
    }

    stats.sampleCount = periodCount;
    stats.minMs = minMs;
    stats.maxMs = maxMs;
    stats.averageMs = static_cast<double>(totalMs) / periodCount;
    stats.valid = true;
    return stats;
}

QString periodSummaryLine(const QString &label, const QVector<EvidenceLogFrame> &frames)
{
    const PeriodStats stats = calculatePeriodStats(frames);
    if (frames.size() < 2) {
        return QString::fromUtf8("%1：采集=%2帧，周期样本不足，判定=证据不足")
            .arg(label)
            .arg(frames.size());
    }
    if (!stats.valid) {
        return QString::fromUtf8("%1：采集=%2帧，周期样本无效，判定=证据不足")
            .arg(label)
            .arg(frames.size());
    }

    return QString::fromUtf8("%1：实际平均=%2 ms，范围=%3~%4 ms，采集=%5帧，判定=需结合期望周期确认")
        .arg(label)
        .arg(QString::number(stats.averageMs, 'f', 1))
        .arg(stats.minMs)
        .arg(stats.maxMs)
        .arg(stats.frameCount);
}

QString expectedPeriodText(const TestCase *testCase, int canId)
{
    if (testCase == nullptr) {
        return QStringLiteral("-");
    }

    const QString text = caseEvidenceText(testCase);
    QRegularExpression configRegex(QStringLiteral("05\\s+29\\s+%1\\s+([0-9A-Fa-f]{2})\\s+([0-9A-Fa-f]{2})")
        .arg(QStringLiteral("%1\\s+%2")
            .arg((canId >> 8) & 0xFF, 2, 16, QChar('0'))
            .arg(canId & 0xFF, 2, 16, QChar('0'))),
        QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = configRegex.match(text);
    if (match.hasMatch()) {
        bool okHigh = false;
        bool okLow = false;
        const int high = match.captured(1).toInt(&okHigh, 16);
        const int low = match.captured(2).toInt(&okLow, 16);
        if (okHigh && okLow) {
            return QStringLiteral("%1 ms").arg((high << 8) | low);
        }
    }

    if (canId == 0x2C0 && text.contains(QStringLiteral("100ms"), Qt::CaseInsensitive)) {
        return QStringLiteral("100 ms");
    }
    if (canId == 0x2C3) {
        return QString::fromUtf8("上电前20帧约200 ms，之后约10 s");
    }
    if (canId == 0x2C4 || canId == 0x2C5) {
        return QString::fromUtf8("上电前20帧按协议快发，之后约10 s");
    }
    return QStringLiteral("-");
}

QString conciseBroadcastPeriodLine(const TestCase *testCase, int canId, const QVector<EvidenceLogFrame> &frames)
{
    const QString idText = formatCanId(canId);
    const QString expectedText = expectedPeriodText(testCase, canId);
    const PeriodStats stats = calculatePeriodStats(frames);
    if (frames.isEmpty()) {
        return QString::fromUtf8("%1：期望=%2，未采集到广播帧，判定=证据不足")
            .arg(idText, expectedText);
    }
    if (!stats.valid) {
        return QString::fromUtf8("%1：期望=%2，采集=%3帧，周期样本不足，判定=证据不足")
            .arg(idText, expectedText)
            .arg(frames.size());
    }
    return QString::fromUtf8("%1：期望=%2，实际平均=%3 ms，范围=%4~%5 ms，采集=%6帧，判定=已采集")
        .arg(idText, expectedText)
        .arg(QString::number(stats.averageMs, 'f', 1))
        .arg(stats.minMs)
        .arg(stats.maxMs)
        .arg(stats.frameCount);
}

QString evidenceCompactFrameLine(const EvidenceLogFrame &frame)
{
    return QString::fromUtf8("%1，%2，Data=%3")
        .arg(frame.timestamp.isValid() ? frame.timestamp.toString(QStringLiteral("hh:mm:ss.zzz")) : QString::fromUtf8("时间无效"))
        .arg(frame.canIdText)
        .arg(frame.dataHex);
}

QString caseEvidenceText(const TestCase *testCase)
{
    if (testCase == nullptr) {
        return QString();
    }
    return QStringList{
        testCase->id,
        testCase->module,
        testCase->basis,
        testCase->precondition,
        testCase->testData,
        testCase->steps,
        testCase->expectedResult,
        testCase->executionMode,
        testCase->commandTemplate,
        testCase->judgeTemplate,
        testCase->manualPrompt,
        testCase->semiAssistTemplate,
        testCase->semiPrompt,
        testCase->semiJudgeTemplate,
        testCase->keyFrameIds.join(QLatin1Char(' '))
    }.join(QLatin1Char(' '));
}

bool caseRequiresTransmitEvidence(const TestCase *testCase)
{
    if (testCase == nullptr) {
        return false;
    }
    const QString commandTemplate = testCase->commandTemplate.trimmed();
    const QString assistTemplate = testCase->semiAssistTemplate.trimmed();
    return !commandTemplate.isEmpty() ||
           (!assistTemplate.isEmpty() && assistTemplate != QStringLiteral("mt.semi.fault_status"));
}

bool caseRequiresReceiveEvidence(const TestCase *testCase)
{
    if (testCase == nullptr) {
        return true;
    }
    const QString text = caseEvidenceText(testCase);
    return text.contains(QStringLiteral("0x2C"), Qt::CaseInsensitive) ||
           text.contains(QStringLiteral("SID"), Qt::CaseInsensitive) ||
           text.contains(QStringLiteral("响应")) ||
           text.contains(QStringLiteral("广播")) ||
           text.contains(QStringLiteral("上报"));
}

bool caseFocusesControl207(const TestCase *testCase)
{
    if (testCase == nullptr) {
        return false;
    }
    const QString text = caseEvidenceText(testCase);
    return text.contains(QStringLiteral("0x207"), Qt::CaseInsensitive) ||
           testCase->commandTemplate.startsWith(QStringLiteral("mt.control_0x207")) ||
           testCase->postCommandTemplate.contains(QStringLiteral("0x207"), Qt::CaseInsensitive) ||
           testCase->semiAssistTemplate.startsWith(QStringLiteral("mt.semi.tag")) ||
           testCase->semiAssistTemplate.contains(QStringLiteral("fault_control"), Qt::CaseInsensitive) ||
           testCase->judgeTemplate.contains(QStringLiteral("0x207"), Qt::CaseInsensitive) ||
           testCase->judgeTemplate.contains(QStringLiteral("control"), Qt::CaseInsensitive) ||
           testCase->semiJudgeTemplate.contains(QStringLiteral("0x207"), Qt::CaseInsensitive) ||
           testCase->semiJudgeTemplate.contains(QStringLiteral("control"), Qt::CaseInsensitive);
}

QString control207SummaryText(const QVector<EvidenceLogFrame> &frames)
{
    QMap<QString, int> dataCounts;
    for (const EvidenceLogFrame &frame : frames) {
        dataCounts[frame.dataHex.trimmed().isEmpty() ? QString::fromUtf8("空数据") : frame.dataHex.trimmed()] += 1;
    }

    QStringList summaries;
    for (auto it = dataCounts.constBegin(); it != dataCounts.constEnd(); ++it) {
        summaries << QString::fromUtf8("%1×%2").arg(it.key()).arg(it.value());
    }
    return summaries.join(QString::fromUtf8("；"));
}

QList<int> configuredEvidenceIds(const TestCase *testCase)
{
    QList<int> ids;
    if (testCase == nullptr) {
        return ids;
    }
    for (const QString &idTextValue : testCase->keyFrameIds) {
        int canId = -1;
        if (parseEvidenceCanId(idTextValue, &canId) &&
            canId >= kBroadcastFirstId && canId <= kBroadcastLastId &&
            !ids.contains(canId)) {
            ids.append(canId);
        }
    }
    return ids;
}

QString idListText(const QList<int> &ids)
{
    QStringList values;
    for (const int canId : ids) {
        values << formatCanId(canId);
    }
    return values.isEmpty() ? QString::fromUtf8("（无）") : values.join(QString::fromUtf8("、"));
}

QList<int> relevantBroadcastIds(const TestCase *testCase)
{
    QList<int> ids = configuredEvidenceIds(testCase);
    const QString text = caseEvidenceText(testCase);
    for (int canId = kBroadcastFirstId; canId <= kBroadcastLastId; ++canId) {
        const QString idTextValue = formatCanId(canId);
        if (text.contains(idTextValue, Qt::CaseInsensitive) && !ids.contains(canId)) {
            ids.append(canId);
        }
    }
    QRegularExpression rangeRegex(QStringLiteral("0x(2C[0-6])\\s*[~\\-～至到]\\s*(?:0x)?(2C[0-6])"),
                                  QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator ranges = rangeRegex.globalMatch(text);
    while (ranges.hasNext()) {
        const QRegularExpressionMatch match = ranges.next();
        bool startOk = false;
        bool endOk = false;
        const int startId = match.captured(1).toInt(&startOk, 16);
        const int endId = match.captured(2).toInt(&endOk, 16);
        if (!startOk || !endOk) {
            continue;
        }
        for (int canId = qMin(startId, endId); canId <= qMax(startId, endId); ++canId) {
            if (canId >= kBroadcastFirstId && canId <= kBroadcastLastId && !ids.contains(canId)) {
                ids.append(canId);
            }
        }
    }

    const QString judgeTemplate = testCase == nullptr ? QString() : testCase->judgeTemplate.trimmed();
    const bool expectsAllBroadcast =
        judgeTemplate == QStringLiteral("mt.broadcast_all_present_after_reboot") ||
        judgeTemplate == QStringLiteral("mt.broadcast_start_after_reboot") ||
        judgeTemplate == QStringLiteral("mt.reboot_response_and_broadcast") ||
        judgeTemplate == QStringLiteral("mt.positive_response_and_broadcast") ||
        judgeTemplate == QStringLiteral("mt.broadcast_recovered") ||
        text.contains(QStringLiteral("0x2C0~0x2C6"), Qt::CaseInsensitive) ||
        text.contains(QStringLiteral("0x2C0-0x2C6"), Qt::CaseInsensitive);
    if (expectsAllBroadcast) {
        ids.clear();
        for (int canId = kBroadcastFirstId; canId <= kBroadcastLastId; ++canId) {
            ids.append(canId);
        }
    }

    if (ids.isEmpty() && text.contains(QStringLiteral("广播"))) {
        for (int canId = kBroadcastFirstId; canId <= kBroadcastLastId; ++canId) {
            ids.append(canId);
        }
    }
    return ids;
}

QStringList jsonStringList(const QJsonObject &object, const char *key)
{
    QStringList values;
    const QJsonValue value = object.value(QString::fromLatin1(key));
    if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue &item : array) {
            const QString text = item.toString().trimmed();
            if (!text.isEmpty()) {
                values.append(text.toUpper());
            }
        }
    } else {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            values.append(text.toUpper());
        }
    }
    return values;
}

void inferAutomationFields(TestCase *testCase)
{
    if (testCase == nullptr) {
        return;
    }

    const QString text = QStringLiteral("%1 %2 %3 %4 %5 %6")
        .arg(testCase->id,
             testCase->module,
             testCase->basis,
             testCase->testData,
             testCase->steps,
             testCase->expectedResult);
    const bool negativeCase = text.contains(QStringLiteral("异常")) ||
                              text.contains(QStringLiteral("否定")) ||
                              text.contains(QStringLiteral("非法")) ||
                              text.contains(QStringLiteral("错误"));

    if (testCase->executionMode.isEmpty()) {
        testCase->executionMode = QStringLiteral("manual");
    }

    if (text.contains(QStringLiteral("SID=0x01")) || text.contains(QStringLiteral("扫描周期"))) {
        if (testCase->commandTemplate.isEmpty() && !negativeCase) {
            testCase->commandTemplate = QStringLiteral("mt.sid_0x01_set_scan_period");
        }
        if (testCase->judgeTemplate.isEmpty()) {
            testCase->judgeTemplate = QStringLiteral("mt.positive_response");
        }
        if (testCase->executionMode == QStringLiteral("manual") && !negativeCase) {
            testCase->executionMode = QStringLiteral("auto");
        }
    } else if (text.contains(QStringLiteral("SID=0x02")) || text.contains(QStringLiteral("重启"))) {
        if (testCase->commandTemplate.isEmpty() && !negativeCase) {
            testCase->commandTemplate = QStringLiteral("mt.sid_0x02_reboot");
        }
        if (testCase->judgeTemplate.isEmpty()) {
            testCase->judgeTemplate = QStringLiteral("mt.positive_response");
        }
        if (testCase->executionMode == QStringLiteral("manual") && !negativeCase) {
            testCase->executionMode = QStringLiteral("auto");
        }
    } else if (text.contains(QStringLiteral("0x29"))) {
        if (testCase->commandTemplate.isEmpty() && !negativeCase) {
            testCase->commandTemplate = QStringLiteral("mt.sid_0x29_period_config");
        }
        if (testCase->judgeTemplate.isEmpty()) {
            testCase->judgeTemplate = QStringLiteral("mt.positive_response");
        }
        if (testCase->executionMode == QStringLiteral("manual") && !negativeCase) {
            testCase->executionMode = QStringLiteral("semi");
        }
    } else if (text.contains(QStringLiteral("0x2E")) || text.contains(QStringLiteral("SN"))) {
        if (testCase->judgeTemplate.isEmpty()) {
            testCase->judgeTemplate = QStringLiteral("mt.write_nvm_response");
        }
        if (testCase->executionMode == QStringLiteral("manual")) {
            testCase->executionMode = QStringLiteral("semi");
        }
        if (testCase->manualPrompt.isEmpty()) {
            testCase->manualPrompt = QStringLiteral("0x2E 属于非易失写入，批量执行前需人工确认写入对象和数据。");
        }
    } else if (text.contains(QStringLiteral("0x2C0")) || text.contains(QStringLiteral("0x2C6")) ||
               text.contains(QStringLiteral("广播"))) {
        if (testCase->judgeTemplate.isEmpty()) {
            testCase->judgeTemplate = QStringLiteral("mt.broadcast_present");
        }
        if (testCase->executionMode == QStringLiteral("manual")) {
            testCase->executionMode = QStringLiteral("semi");
        }
    }
    if (negativeCase && testCase->executionMode == QStringLiteral("auto")) {
        testCase->executionMode = QStringLiteral("semi");
    }
}
}

bool TestCaseService::loadCasesFromJsonFile(const QString &filePath, QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QStringLiteral("无法打开用例文件: %1").arg(filePath);
        }
        return false;
    }
    return loadCasesFromJsonData(file.readAll(), error);
}

bool TestCaseService::loadCasesFromJsonData(const QByteArray &jsonData, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error) {
            *error = QStringLiteral("用例 JSON 格式无效: %1").arg(parseError.errorString());
        }
        return false;
    }

    QVector<TestCase> loadedCases;
    const QJsonArray array = document.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        TestCase testCase;
        testCase.id = jsonString(object, "id");
        testCase.module = jsonString(object, "module");
        testCase.priority = jsonString(object, "priority");
        testCase.type = jsonString(object, "type");
        testCase.basis = jsonString(object, "basis");
        testCase.precondition = jsonString(object, "precondition");
        testCase.testData = jsonString(object, "testData");
        testCase.steps = jsonString(object, "steps");
        testCase.expectedResult = jsonString(object, "expectedResult");
        testCase.executionMode = jsonString(object, "executionMode");
        testCase.commandTemplate = jsonString(object, "commandTemplate");
        testCase.judgeTemplate = jsonString(object, "judgeTemplate");
        testCase.manualPrompt = jsonString(object, "manualPrompt");
        testCase.expectedNegativeSid = jsonString(object, "expectedNegativeSid").toUpper();
        testCase.allowedNrc = jsonStringList(object, "allowedNrc");
        testCase.keyFrameIds = jsonStringList(object, "keyFrameIds");
        testCase.judgeWindow = jsonString(object, "judgeWindow");
        testCase.postCommandTemplate = jsonString(object, "postCommandTemplate");
        testCase.requiredPrefix = jsonString(object, "requiredPrefix");
        testCase.allowEmptyValue = jsonBool(object, "allowEmptyValue", true);
        testCase.semiAssistTemplate = jsonString(object, "semiAssistTemplate");
        testCase.semiPrompt = jsonString(object, "semiPrompt");
        testCase.semiJudgeTemplate = jsonString(object, "semiJudgeTemplate");
        testCase.semiWaitMs = jsonInt(object, "semiWaitMs", 1000);
        testCase.timeoutMs = jsonInt(object, "timeoutMs", 1000);
        testCase.retryCount = jsonInt(object, "retryCount", 0);
        inferAutomationFields(&testCase);
        if (!testCase.id.isEmpty()) {
            loadedCases.append(testCase);
        }
    }

    if (loadedCases.isEmpty()) {
        if (error) {
            *error = QStringLiteral("用例文件中未找到有效用例");
        }
        return false;
    }

    m_cases = loadedCases;
    m_results.clear();
    for (const TestCase &testCase : qAsConst(m_cases)) {
        TestCaseResult result;
        result.caseId = testCase.id;
        m_results.insert(testCase.id, result);
    }
    return true;
}

const QVector<TestCase> &TestCaseService::cases() const
{
    return m_cases;
}

QStringList TestCaseService::modules() const
{
    QStringList moduleList;
    for (const TestCase &testCase : m_cases) {
        if (!moduleList.contains(testCase.module)) {
            moduleList.append(testCase.module);
        }
    }
    return moduleList;
}

bool TestCaseService::createSession(const TestSession &session, QString *error)
{
    if (session.sessionDirectory.trimmed().isEmpty()) {
        if (error) {
            *error = QStringLiteral("会话目录为空");
        }
        return false;
    }

    QDir dir;
    if (!dir.mkpath(session.sessionDirectory) ||
        !dir.mkpath(QDir(session.sessionDirectory).filePath(QStringLiteral("evidence")))) {
        if (error) {
            *error = QStringLiteral("无法创建会话目录: %1").arg(session.sessionDirectory);
        }
        return false;
    }

    m_session = session;
    m_hasSession = true;
    m_activeCaseId.clear();
    for (auto it = m_results.begin(); it != m_results.end(); ++it) {
        it->status = TestResultStatus::NotRun;
        it->actualResult.clear();
        it->defectId.clear();
        it->remark.clear();
        it->startedAt = QDateTime();
        it->finishedAt = QDateTime();
        it->evidenceLogPath.clear();
        it->failureCategory.clear();
        it->judgeReason.clear();
        it->keyFrames.clear();
        it->previousStatus.clear();
        it->previousFailureReason.clear();
        it->retestCount = 0;
        it->lastRetestAt = QDateTime();
    }
    return saveSessionJson(error);
}

bool TestCaseService::loadSession(const QString &sessionJsonPath, QString *error)
{
    QFile file(sessionJsonPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("无法打开会话文件: %1").arg(sessionJsonPath);
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) {
            *error = QStringLiteral("会话 JSON 格式无效: %1").arg(parseError.errorString());
        }
        return false;
    }

    const QJsonObject root = document.object();
    const QFileInfo sessionFileInfo(sessionJsonPath);
    TestSession loadedSession;
    loadedSession.sessionId = root.value(QStringLiteral("sessionId")).toString(sessionFileInfo.dir().dirName());
    loadedSession.projectName = root.value(QStringLiteral("projectName")).toString(QStringLiteral("美团RFID CAN通信"));
    loadedSession.softwareVersion = root.value(QStringLiteral("softwareVersion")).toString();
    loadedSession.firmwareVersion = root.value(QStringLiteral("firmwareVersion")).toString();
    loadedSession.deviceSn = root.value(QStringLiteral("deviceSn")).toString();
    loadedSession.tester = root.value(QStringLiteral("tester")).toString();
    loadedSession.environment = root.value(QStringLiteral("environment")).toString();
    loadedSession.remark = root.value(QStringLiteral("remark")).toString();
    loadedSession.createdAt = QDateTime::fromString(root.value(QStringLiteral("createdAt")).toString(), Qt::ISODate);
    if (!loadedSession.createdAt.isValid()) {
        loadedSession.createdAt = sessionFileInfo.lastModified();
    }
    loadedSession.sessionDirectory = root.value(QStringLiteral("sessionDirectory")).toString();
    if (loadedSession.sessionDirectory.trimmed().isEmpty()) {
        loadedSession.sessionDirectory = sessionFileInfo.absolutePath();
    }

    QDir dir;
    if (!dir.mkpath(loadedSession.sessionDirectory) ||
        !dir.mkpath(QDir(loadedSession.sessionDirectory).filePath(QStringLiteral("evidence")))) {
        if (error) {
            *error = QStringLiteral("无法访问会话目录: %1").arg(loadedSession.sessionDirectory);
        }
        return false;
    }

    QMap<QString, TestCaseResult> loadedResults;
    for (const TestCase &testCase : qAsConst(m_cases)) {
        TestCaseResult result;
        result.caseId = testCase.id;
        loadedResults.insert(testCase.id, result);
    }

    const QJsonArray results = root.value(QStringLiteral("results")).toArray();
    for (const QJsonValue &value : results) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        const QString caseId = object.value(QStringLiteral("caseId")).toString().trimmed();
        if (caseId.isEmpty() || !loadedResults.contains(caseId)) {
            continue;
        }

        TestCaseResult result = loadedResults.value(caseId);
        result.caseId = caseId;
        result.status = testResultStatusFromText(object.value(QStringLiteral("status")).toString());
        result.actualResult = object.value(QStringLiteral("actualResult")).toString();
        result.defectId = object.value(QStringLiteral("defectId")).toString();
        result.remark = object.value(QStringLiteral("remark")).toString();
        result.startedAt = QDateTime::fromString(object.value(QStringLiteral("startedAt")).toString(), Qt::ISODate);
        result.finishedAt = QDateTime::fromString(object.value(QStringLiteral("finishedAt")).toString(), Qt::ISODate);
        result.evidenceLogPath = object.value(QStringLiteral("evidenceLogPath")).toString();
        if (result.evidenceLogPath.trimmed().isEmpty()) {
            const QString candidate = QDir(loadedSession.sessionDirectory).filePath(QStringLiteral("evidence/%1.log").arg(caseId));
            if (QFileInfo::exists(candidate)) {
                result.evidenceLogPath = candidate;
            }
        }
        result.failureCategory = object.value(QStringLiteral("failureCategory")).toString();
        result.judgeReason = object.value(QStringLiteral("judgeReason")).toString();
        result.keyFrames.clear();
        const QJsonArray keyFrames = object.value(QStringLiteral("keyFrames")).toArray();
        for (const QJsonValue &keyFrame : keyFrames) {
            result.keyFrames.append(keyFrame.toString());
        }
        result.previousStatus = object.value(QStringLiteral("previousStatus")).toString();
        result.previousFailureReason = object.value(QStringLiteral("previousFailureReason")).toString();
        result.retestCount = object.value(QStringLiteral("retestCount")).toInt(0);
        result.lastRetestAt = QDateTime::fromString(object.value(QStringLiteral("lastRetestAt")).toString(), Qt::ISODate);
        loadedResults.insert(caseId, result);
    }

    m_session = loadedSession;
    m_results = loadedResults;
    m_activeCaseId.clear();
    m_hasSession = true;
    return true;
}

bool TestCaseService::hasSession() const
{
    return m_hasSession;
}

const TestSession &TestCaseService::session() const
{
    return m_session;
}

bool TestCaseService::startCase(const QString &caseId, QString *error)
{
    if (!m_hasSession) {
        if (error) {
            *error = QStringLiteral("请先新建测试会话");
        }
        return false;
    }
    if (!m_results.contains(caseId)) {
        if (error) {
            *error = QStringLiteral("用例不存在: %1").arg(caseId);
        }
        return false;
    }
    TestCaseResult result = m_results.value(caseId);
    result.caseId = caseId;
    result.startedAt = QDateTime::currentDateTime();
    result.finishedAt = QDateTime();
    result.evidenceLogPath = caseEvidencePath(caseId);
    if (!initializeEvidenceFile(caseId, result, error)) {
        return false;
    }

    m_activeCaseId = caseId;
    m_results.insert(caseId, result);
    return saveSessionJson(error);
}

bool TestCaseService::hasActiveCase() const
{
    return !m_activeCaseId.isEmpty();
}

QString TestCaseService::activeCaseId() const
{
    return m_activeCaseId;
}

void TestCaseService::finishActiveCase()
{
    m_activeCaseId.clear();
}

TestCaseResult TestCaseService::resultForCase(const QString &caseId) const
{
    return m_results.value(caseId);
}

QMap<QString, TestCaseResult> TestCaseService::results() const
{
    return m_results;
}

bool TestCaseService::saveResult(const TestCaseResult &result, QString *error)
{
    if (!m_results.contains(result.caseId)) {
        if (error) {
            *error = QStringLiteral("用例不存在: %1").arg(result.caseId);
        }
        return false;
    }

    TestCaseResult saved = result;
    const TestCaseResult previous = m_results.value(saved.caseId);
    if (!saved.startedAt.isValid()) {
        saved.startedAt = previous.startedAt;
    }
    saved.finishedAt = QDateTime::currentDateTime();
    if (saved.evidenceLogPath.isEmpty()) {
        saved.evidenceLogPath = previous.evidenceLogPath;
    }
    if ((previous.status == TestResultStatus::Failed || previous.status == TestResultStatus::Blocked) &&
        previous.status != saved.status) {
        saved.previousStatus = testResultStatusText(previous.status);
        saved.previousFailureReason = previous.judgeReason.isEmpty() ? previous.actualResult : previous.judgeReason;
        saved.retestCount = previous.retestCount + 1;
        saved.lastRetestAt = QDateTime::currentDateTime();
    } else {
        saved.previousStatus = previous.previousStatus;
        saved.previousFailureReason = previous.previousFailureReason;
        saved.retestCount = previous.retestCount;
        saved.lastRetestAt = previous.lastRetestAt;
    }
    m_results.insert(saved.caseId, saved);
    appendResultEvidence(saved);
    return saveSessionJson(error);
}

bool TestCaseService::appendEvidence(const QString &direction,
                                     const QString &channel,
                                     const QString &frameId,
                                     const QString &dataHex,
                                     const QString &decodedText,
                                     QString *error)
{
    if (!hasActiveCase()) {
        return true;
    }
    if (!ensureEvidenceFile(m_activeCaseId, error)) {
        return false;
    }

    QFile file(caseEvidencePath(m_activeCaseId));
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("无法写入证据日志: %1").arg(file.fileName());
        }
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz") << ','
           << csvEscape(direction) << ','
           << csvEscape(channel) << ','
           << csvEscape(frameId) << ','
           << csvEscape(dataHex) << ','
           << csvEscape(decodedText) << '\n';
    return true;
}

bool TestCaseService::appendExecutionEvent(const QString &eventType,
                                           const QString &description,
                                           QString *error)
{
    if (!hasActiveCase()) {
        return true;
    }
    if (!ensureEvidenceFile(m_activeCaseId, error)) {
        return false;
    }

    const QString title = eventType.contains(QString::fromUtf8("人工")) ||
                          eventType.contains(QStringLiteral("manual"), Qt::CaseInsensitive)
        ? QString::fromUtf8("人工事件")
        : QString::fromUtf8("执行步骤摘要");
    const QString header = QString::fromUtf8("【%1】").arg(title);
    QString eventLine = QString::fromUtf8("%1 %2：%3")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")),
             eventType.trimmed().isEmpty() ? QString::fromUtf8("事件") : eventType.trimmed(),
             description.trimmed().isEmpty() ? QString::fromUtf8("（无说明）") : description.trimmed());
    eventLine.replace(QLatin1Char(','), QString::fromUtf8("，"));

    QFile file(caseEvidencePath(m_activeCaseId));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QString::fromUtf8("无法读取证据日志: %1").arg(file.fileName());
        }
        return false;
    }
    QTextStream input(&file);
    input.setCodec("UTF-8");
    QString text = input.readAll();
    file.close();

    const int sectionStart = text.indexOf(header);
    if (sectionStart < 0) {
        int insertAt = text.indexOf(QString::fromUtf8("【原始帧记录】"));
        if (insertAt < 0) {
            insertAt = text.size();
        }
        text.insert(insertAt, QString::fromUtf8("\n%1\n%2\n").arg(header, eventLine));
    } else {
        int insertAt = text.indexOf(QString::fromUtf8("\n【"), sectionStart + header.size());
        if (insertAt < 0) {
            insertAt = text.size();
        }
        const int contentStart = text.indexOf(QLatin1Char('\n'), sectionStart);
        if (contentStart >= 0 && contentStart < insertAt) {
            QString sectionText = text.mid(contentStart + 1, insertAt - contentStart - 1);
            const QString placeholder = title == QString::fromUtf8("人工事件")
                ? QString::fromUtf8("（无）\n")
                : QString::fromUtf8("等待执行步骤记录。\n");
            if (sectionText.trimmed() == placeholder.trimmed()) {
                text.remove(contentStart + 1, insertAt - contentStart - 1);
                insertAt = contentStart + 1;
            }
        }
        text.insert(insertAt, eventLine + QLatin1Char('\n'));
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) {
            *error = QString::fromUtf8("无法写入证据日志: %1").arg(file.fileName());
        }
        return false;
    }
    QTextStream output(&file);
    output.setCodec("UTF-8");
    output << text;
    return true;
}

bool TestCaseService::exportResultsCsv(const QString &filePath, QString *error) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("无法导出结果: %1").arg(filePath);
        }
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << QString::fromUtf8("\xEF\xBB\xBF");
    stream << "用例ID,模块,优先级,测试类型,执行模式,执行结果,实际结果,判定原因,失败归类,关键帧,缺陷编号,备注,证据日志路径,开始时间,结束时间,复测次数\n";

    for (const TestCase &testCase : m_cases) {
        const TestCaseResult result = m_results.value(testCase.id);
        stream << csvEscape(testCase.id) << ','
               << csvEscape(testCase.module) << ','
               << csvEscape(testCase.priority) << ','
               << csvEscape(testCase.type) << ','
               << csvEscape(testCase.executionMode) << ','
               << csvEscape(testResultStatusText(result.status)) << ','
               << csvEscape(result.actualResult) << ','
               << csvEscape(result.judgeReason) << ','
               << csvEscape(result.failureCategory) << ','
               << csvEscape(result.keyFrames.join(QStringLiteral("\n"))) << ','
               << csvEscape(result.defectId) << ','
               << csvEscape(result.remark) << ','
               << csvEscape(result.evidenceLogPath) << ','
               << csvEscape(result.startedAt.isValid() ? result.startedAt.toString("yyyy-MM-dd hh:mm:ss") : QString()) << ','
               << csvEscape(result.finishedAt.isValid() ? result.finishedAt.toString("yyyy-MM-dd hh:mm:ss") : QString()) << ','
               << result.retestCount << '\n';
    }
    return true;
}

bool TestCaseService::exportResultsExcelHtml(const QString &filePath, QString *error) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("无法导出Excel结果副本: %1").arg(filePath);
        }
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << QString::fromUtf8("\xEF\xBB\xBF");
    stream << "<html><head><meta charset=\"utf-8\">";
    stream << "<style>"
              "body{font-family:SimSun,serif;font-size:11pt;}"
              "table{border-collapse:collapse;width:100%;}"
              "th{font-family:SimHei,sans-serif;background:#D9EAF7;color:#1F4E78;}"
              "td,th{border:1px solid #BFBFBF;padding:4px;vertical-align:top;}"
              ".pass{background:#E2F0D9}.fail{background:#FCE4D6}.block{background:#FFF2CC}"
              "</style></head><body>";
    stream << "<h2>美团 RFID CAN 通信软件测试结果</h2>";
    if (m_hasSession) {
        stream << "<p>项目：" << m_session.projectName.toHtmlEscaped()
               << "；软件版本：" << m_session.softwareVersion.toHtmlEscaped()
               << "；固件版本：" << m_session.firmwareVersion.toHtmlEscaped()
               << "；设备SN：" << m_session.deviceSn.toHtmlEscaped()
               << "；测试人员：" << m_session.tester.toHtmlEscaped()
               << "；测试环境：" << m_session.environment.toHtmlEscaped()
               << "</p>";
    }
    stream << "<table><tr>"
              "<th>用例ID</th><th>模块</th><th>优先级</th><th>测试类型</th>"
              "<th>执行模式</th><th>执行结果</th><th>实际结果</th><th>判定原因</th><th>失败归类</th><th>关键帧</th>"
              "<th>缺陷编号</th><th>备注</th><th>证据日志路径</th><th>开始时间</th><th>结束时间</th><th>复测次数</th>"
              "</tr>";

    for (const TestCase &testCase : m_cases) {
        const TestCaseResult result = m_results.value(testCase.id);
        QString cssClass;
        if (result.status == TestResultStatus::Passed) {
            cssClass = QStringLiteral("pass");
        } else if (result.status == TestResultStatus::Failed) {
            cssClass = QStringLiteral("fail");
        } else if (result.status == TestResultStatus::Blocked) {
            cssClass = QStringLiteral("block");
        }
        stream << "<tr class=\"" << cssClass << "\">"
               << "<td>" << testCase.id.toHtmlEscaped() << "</td>"
               << "<td>" << testCase.module.toHtmlEscaped() << "</td>"
               << "<td>" << testCase.priority.toHtmlEscaped() << "</td>"
               << "<td>" << testCase.type.toHtmlEscaped() << "</td>"
               << "<td>" << testCase.executionMode.toHtmlEscaped() << "</td>"
               << "<td>" << testResultStatusText(result.status).toHtmlEscaped() << "</td>"
               << "<td>" << result.actualResult.toHtmlEscaped().replace("\n", "<br>") << "</td>"
               << "<td>" << result.judgeReason.toHtmlEscaped().replace("\n", "<br>") << "</td>"
               << "<td>" << result.failureCategory.toHtmlEscaped() << "</td>"
               << "<td>" << result.keyFrames.join(QStringLiteral("\n")).toHtmlEscaped().replace("\n", "<br>") << "</td>"
               << "<td>" << result.defectId.toHtmlEscaped() << "</td>"
               << "<td>" << result.remark.toHtmlEscaped().replace("\n", "<br>") << "</td>"
               << "<td>" << result.evidenceLogPath.toHtmlEscaped() << "</td>"
               << "<td>" << (result.startedAt.isValid() ? result.startedAt.toString("yyyy-MM-dd hh:mm:ss") : QString()).toHtmlEscaped() << "</td>"
               << "<td>" << (result.finishedAt.isValid() ? result.finishedAt.toString("yyyy-MM-dd hh:mm:ss") : QString()).toHtmlEscaped() << "</td>"
               << "<td>" << result.retestCount << "</td>"
               << "</tr>";
    }
    stream << "</table></body></html>";
    return true;
}

QString TestCaseService::evidencePathForCase(const QString &caseId) const
{
    const TestCaseResult result = m_results.value(caseId);
    if (!result.evidenceLogPath.isEmpty()) {
        return result.evidenceLogPath;
    }
    if (!m_hasSession) {
        return QString();
    }
    return caseEvidencePath(caseId);
}

bool TestCaseService::evidenceExistsForCase(const QString &caseId) const
{
    if (!m_hasSession || !m_results.contains(caseId)) {
        return false;
    }

    const TestCaseResult result = m_results.value(caseId);
    QString path = result.evidenceLogPath;
    if (path.trimmed().isEmpty()) {
        path = caseEvidencePath(caseId);
    }
    QFileInfo info(path);
    return info.exists() && info.size() > 0;
}

bool TestCaseService::saveSessionJson(QString *error) const
{
    if (!m_hasSession) {
        return true;
    }

    QJsonObject root;
    root.insert(QStringLiteral("sessionId"), m_session.sessionId);
    root.insert(QStringLiteral("projectName"), m_session.projectName);
    root.insert(QStringLiteral("softwareVersion"), m_session.softwareVersion);
    root.insert(QStringLiteral("firmwareVersion"), m_session.firmwareVersion);
    root.insert(QStringLiteral("deviceSn"), m_session.deviceSn);
    root.insert(QStringLiteral("tester"), m_session.tester);
    root.insert(QStringLiteral("environment"), m_session.environment);
    root.insert(QStringLiteral("remark"), m_session.remark);
    root.insert(QStringLiteral("createdAt"), m_session.createdAt.toString(Qt::ISODate));
    root.insert(QStringLiteral("sessionDirectory"), m_session.sessionDirectory);

    QJsonArray resultArray;
    for (const TestCase &testCase : m_cases) {
        const TestCaseResult result = m_results.value(testCase.id);
        QJsonObject object;
        object.insert(QStringLiteral("caseId"), result.caseId);
        object.insert(QStringLiteral("status"), testResultStatusText(result.status));
        object.insert(QStringLiteral("actualResult"), result.actualResult);
        object.insert(QStringLiteral("defectId"), result.defectId);
        object.insert(QStringLiteral("remark"), result.remark);
        object.insert(QStringLiteral("startedAt"), result.startedAt.toString(Qt::ISODate));
        object.insert(QStringLiteral("finishedAt"), result.finishedAt.toString(Qt::ISODate));
        object.insert(QStringLiteral("evidenceLogPath"), result.evidenceLogPath);
        object.insert(QStringLiteral("failureCategory"), result.failureCategory);
        object.insert(QStringLiteral("judgeReason"), result.judgeReason);
        object.insert(QStringLiteral("keyFrames"), QJsonArray::fromStringList(result.keyFrames));
        object.insert(QStringLiteral("previousStatus"), result.previousStatus);
        object.insert(QStringLiteral("previousFailureReason"), result.previousFailureReason);
        object.insert(QStringLiteral("retestCount"), result.retestCount);
        object.insert(QStringLiteral("lastRetestAt"), result.lastRetestAt.toString(Qt::ISODate));
        resultArray.append(object);
    }
    root.insert(QStringLiteral("results"), resultArray);

    QFile file(QDir(m_session.sessionDirectory).filePath(QStringLiteral("session.json")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("无法保存会话文件: %1").arg(file.fileName());
        }
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    exportResultsCsv(resultsCsvPath(), nullptr);
    return true;
}

bool TestCaseService::ensureEvidenceFile(const QString &caseId, QString *error)
{
    if (!m_hasSession) {
        if (error) {
            *error = QStringLiteral("请先新建测试会话");
        }
        return false;
    }

    QFile file(caseEvidencePath(caseId));
    if (file.exists()) {
        return true;
    }
    const TestCaseResult result = m_results.value(caseId);
    return initializeEvidenceFile(caseId, result, error);
}

bool TestCaseService::initializeEvidenceFile(const QString &caseId, const TestCaseResult &result, QString *error) const
{
    if (!m_hasSession) {
        if (error) {
            *error = QStringLiteral("请先新建测试会话");
        }
        return false;
    }

    const QString path = caseEvidencePath(caseId);
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("无法创建证据日志: %1").arg(file.fileName());
        }
        return false;
    }

    const TestCase *testCase = findCase(caseId);
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << QString::fromUtf8("\xEF\xBB\xBF");
    stream << QString::fromUtf8("【用例信息】\n");
    stream << evidenceFieldLine(QString::fromUtf8("用例ID"), caseId) << '\n';
    if (testCase != nullptr) {
        stream << evidenceFieldLine(QString::fromUtf8("测试模块"), testCase->module) << '\n';
        stream << evidenceFieldLine(QString::fromUtf8("优先级"), testCase->priority) << '\n';
        stream << evidenceFieldLine(QString::fromUtf8("测试类型"), testCase->type) << '\n';
        stream << evidenceFieldLine(QString::fromUtf8("执行方式"), testCase->executionMode) << '\n';
        stream << evidenceFieldLine(QString::fromUtf8("命令模板"), testCase->commandTemplate) << '\n';
        stream << evidenceFieldLine(QString::fromUtf8("判定模板"), testCase->judgeTemplate) << '\n';
    }
    stream << evidenceFieldLine(QString::fromUtf8("项目名称"), m_session.projectName) << '\n';
    stream << evidenceFieldLine(QString::fromUtf8("软件版本"), m_session.softwareVersion) << '\n';
    stream << evidenceFieldLine(QString::fromUtf8("固件版本"), m_session.firmwareVersion) << '\n';
    stream << evidenceFieldLine(QString::fromUtf8("设备SN"), m_session.deviceSn) << '\n';
    stream << evidenceFieldLine(QString::fromUtf8("测试人员"), m_session.tester) << '\n';
    stream << evidenceFieldLine(QString::fromUtf8("测试环境"), m_session.environment) << '\n';
    stream << evidenceFieldLine(QString::fromUtf8("开始时间"),
                                result.startedAt.isValid() ? result.startedAt.toString("yyyy-MM-dd hh:mm:ss") : QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")) << '\n';
    stream << '\n';

    stream << QString::fromUtf8("【测试数据】\n");
    for (const QString &line : evidenceTextLines(testCase != nullptr ? testCase->testData : QString())) {
        stream << line << '\n';
    }
    stream << '\n';

    stream << QString::fromUtf8("【操作步骤】\n");
    for (const QString &line : evidenceTextLines(testCase != nullptr ? testCase->steps : QString())) {
        stream << line << '\n';
    }
    stream << '\n';

    stream << QString::fromUtf8("【预期结果】\n");
    for (const QString &line : evidenceTextLines(testCase != nullptr ? testCase->expectedResult : QString())) {
        stream << line << '\n';
    }
    stream << '\n';

    stream << QString::fromUtf8("【人工操作】\n");
    for (const QString &line : evidenceTextLines(testCase != nullptr ? testCase->manualPrompt : QString())) {
        stream << line << '\n';
    }
    stream << '\n';

    stream << QString::fromUtf8("【执行记录】\n");
    stream << QString::fromUtf8("用例已开始执行，后续 Tx/Rx/CAN 广播帧见【原始帧记录】。\n\n");
    stream << QString::fromUtf8("【执行步骤摘要】\n");
    stream << QString::fromUtf8("等待执行步骤记录。\n\n");
    stream << QString::fromUtf8("【人工事件】\n");
    stream << QString::fromUtf8("（无）\n\n");
    stream << QString::fromUtf8("【发送证据】\n");
    stream << QString::fromUtf8("发送类证据以【原始帧记录】中方向为“发送”或 Tx 的 CAN ID/Data 行为准。\n\n");
    stream << QString::fromUtf8("【接收证据】\n");
    stream << QString::fromUtf8("接收类证据以【原始帧记录】中方向为“接收”或 Rx 的 CAN ID/Data 行为准。\n\n");
    stream << QString::fromUtf8("【广播证据】\n");
    stream << QString::fromUtf8("广播类证据以【原始帧记录】中 0x2C0~0x2C6 等广播 CAN ID 行为准；周期统计在后续增强阶段补充。\n\n");
    stream << QString::fromUtf8("【原始帧记录】\n");
    stream << QString::fromUtf8("时间,方向,通道,ID,数据,解析\n");
    return true;
}

bool TestCaseService::appendResultEvidence(const TestCaseResult &result) const
{
    if (!m_hasSession || result.caseId.trimmed().isEmpty()) {
        return false;
    }
    if (!QFileInfo::exists(caseEvidencePath(result.caseId))) {
        return false;
    }

    appendExtractedEvidence(result.caseId);

    QStringList conclusionLines;
    conclusionLines << evidenceFieldLine(QString::fromUtf8("保存时间"), QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"))
                    << evidenceFieldLine(QString::fromUtf8("执行结果"), testResultStatusText(result.status))
                    << evidenceFieldLine(QString::fromUtf8("开始时间"), result.startedAt.isValid() ? result.startedAt.toString("yyyy-MM-dd hh:mm:ss") : QString())
                    << evidenceFieldLine(QString::fromUtf8("结束时间"), result.finishedAt.isValid() ? result.finishedAt.toString("yyyy-MM-dd hh:mm:ss") : QString())
                    << evidenceFieldLine(QString::fromUtf8("失败归类"), result.failureCategory)
                    << evidenceFieldLine(QString::fromUtf8("判定原因"), result.judgeReason);
    conclusionLines << QString::fromUtf8("实际结果：");
    conclusionLines.append(evidenceTextLines(result.actualResult));
    conclusionLines << evidenceFieldLine(QString::fromUtf8("缺陷编号"), result.defectId)
                    << evidenceFieldLine(QString::fromUtf8("备注"), result.remark);
    replaceEvidenceSection(result.caseId, QString::fromUtf8("判定结论"), conclusionLines);

    QStringList keyFrameLines;
    if (result.keyFrames.isEmpty()) {
        keyFrameLines << QString::fromUtf8("（无）");
    } else {
        int index = 1;
        for (QString keyFrame : result.keyFrames) {
            keyFrame.replace(QLatin1Char(','), QString::fromUtf8("，"));
            keyFrameLines << QString::fromUtf8("关键帧%1：%2").arg(index++).arg(keyFrame);
        }
    }
    replaceEvidenceSection(result.caseId, QString::fromUtf8("关键帧"), keyFrameLines);

    QStringList referenceLines;
    referenceLines << evidenceFieldLine(QString::fromUtf8("用例证据日志"), result.evidenceLogPath.isEmpty() ? caseEvidencePath(result.caseId) : result.evidenceLogPath)
                   << QString::fromUtf8("完整 CAN 原始帧可直接查看本文件【原始帧记录】；全局运行日志以测试执行时自动保存目录为准。");
    replaceEvidenceSection(result.caseId, QString::fromUtf8("原始日志引用"), referenceLines);
    return true;
}

bool TestCaseService::appendExtractedEvidence(const QString &caseId) const
{
    const TestCase *testCase = findCase(caseId);
    QFile file(caseEvidencePath(caseId));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream input(&file);
    input.setCodec("UTF-8");
    const QString evidenceText = input.readAll();
    file.close();
    QVector<EvidenceLogFrame> frames;
    const QStringList lines = evidenceText.split(QRegExp(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = parseEvidenceCsvLine(line);
        if (fields.size() < 6) {
            continue;
        }

        EvidenceLogFrame frame;
        frame.timestamp = QDateTime::fromString(fields.at(0).trimmed(), QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
        frame.direction = fields.at(1).trimmed();
        frame.channel = fields.at(2).trimmed();
        frame.canIdText = fields.at(3).trimmed();
        if (!parseEvidenceCanId(frame.canIdText, &frame.canId)) {
            continue;
        }
        frame.dataHex = fields.at(4).trimmed();
        frame.decodedText = fields.at(5).trimmed();
        frames.append(frame);
    }

    const bool focusControl207 = caseFocusesControl207(testCase);
    QVector<EvidenceLogFrame> backgroundControl207Frames;
    QVector<EvidenceLogFrame> relatedControl207Frames;
    QStringList transmitLines;
    int transmitIndex = 1;
    bool transmitTruncated = false;
    bool hasAnyTransmitEvidence = false;
    for (const EvidenceLogFrame &frame : qAsConst(frames)) {
        if (!isTransmitEvidence(frame)) {
            continue;
        }
        hasAnyTransmitEvidence = true;
        if (isControl207Evidence(frame)) {
            if (focusControl207) {
                relatedControl207Frames.append(frame);
            } else {
                backgroundControl207Frames.append(frame);
                continue;
            }
        }
        if (transmitLines.size() < kMaxExtractedFrames) {
            transmitLines << evidenceFrameLine(frame, transmitIndex++);
        } else {
            transmitTruncated = true;
        }
    }
    const bool hasTransmitEvidence = hasAnyTransmitEvidence;
    if (transmitLines.isEmpty() && !hasAnyTransmitEvidence) {
        transmitLines << QString::fromUtf8("未在原始帧记录中检出发送帧。");
    }
    if (transmitTruncated) {
        transmitLines << QString::fromUtf8("仅摘录前%1帧关键发送证据，完整记录见【原始帧记录】。").arg(kMaxExtractedFrames);
    }
    if (!backgroundControl207Frames.isEmpty()) {
        if (!transmitLines.isEmpty()) {
            transmitLines << QString();
        }
        transmitLines << QString::fromUtf8("背景周期控制帧：0x207 共%1帧，非本用例关键输入，已汇总展示。")
            .arg(backgroundControl207Frames.size());
        transmitLines << QString::fromUtf8("0x207 数据分布：%1。").arg(control207SummaryText(backgroundControl207Frames));
        transmitLines << QString::fromUtf8("0x207 首帧：%1").arg(evidenceCompactFrameLine(backgroundControl207Frames.first()));
        if (backgroundControl207Frames.size() > 1) {
            transmitLines << QString::fromUtf8("0x207 末帧：%1").arg(evidenceCompactFrameLine(backgroundControl207Frames.last()));
        }
        transmitLines << QString::fromUtf8("完整 0x207 周期帧见【原始帧记录】。");
    } else if (focusControl207 && !relatedControl207Frames.isEmpty()) {
        transmitLines << QString();
        transmitLines << QString::fromUtf8("0x207 控制帧汇总：共%1帧，数据分布：%2。")
            .arg(relatedControl207Frames.size())
            .arg(control207SummaryText(relatedControl207Frames));
    }
    replaceEvidenceSection(caseId, QString::fromUtf8("发送证据"), transmitLines);

    QStringList receiveLines;
    int receiveIndex = 1;
    for (const EvidenceLogFrame &frame : qAsConst(frames)) {
        if (!isReceiveEvidence(frame) || isBroadcastEvidence(frame)) {
            continue;
        }
        receiveLines << evidenceFrameLine(frame, receiveIndex++);
        if (receiveLines.size() >= kMaxExtractedFrames) {
            receiveLines << QString::fromUtf8("仅摘录前%1帧非广播接收证据，完整记录见【原始帧记录】。").arg(kMaxExtractedFrames);
            break;
        }
    }
    const bool hasReceiveEvidence = receiveIndex > 1;
    if (receiveLines.isEmpty()) {
        receiveLines << QString::fromUtf8("未在原始帧记录中检出非广播接收帧。");
    }
    replaceEvidenceSection(caseId, QString::fromUtf8("接收证据"), receiveLines);

    QMap<int, QVector<EvidenceLogFrame>> broadcastFrames;
    for (const EvidenceLogFrame &frame : qAsConst(frames)) {
        if (isBroadcastEvidence(frame)) {
            broadcastFrames[frame.canId].append(frame);
        }
    }

    QList<int> relevantIds = relevantBroadcastIds(testCase);
    QList<int> supplementalIds;
    int totalBroadcastCount = 0;
    for (int canId = kBroadcastFirstId; canId <= kBroadcastLastId; ++canId) {
        totalBroadcastCount += broadcastFrames.value(canId).size();
        if (!relevantIds.contains(canId) && !broadcastFrames.value(canId).isEmpty()) {
            supplementalIds.append(canId);
        }
    }
    if (relevantIds.isEmpty()) {
        relevantIds = supplementalIds;
    }

    QStringList missingRelevantIds;
    for (const int canId : relevantIds) {
        if (broadcastFrames.value(canId).isEmpty()) {
            missingRelevantIds << formatCanId(canId);
        }
    }

    QStringList broadcastLines;
    broadcastLines << QString::fromUtf8("判定结果：%1")
        .arg(missingRelevantIds.isEmpty()
            ? QString::fromUtf8("通过")
            : QString::fromUtf8("证据不足"));
    broadcastLines << QString::fromUtf8("关注ID：%1").arg(idListText(relevantIds));
    broadcastLines << QString::fromUtf8("判定说明：%1")
        .arg(missingRelevantIds.isEmpty()
            ? QString::fromUtf8("已采集到关注广播帧，完整原始帧见【原始帧记录】。")
            : QString::fromUtf8("缺少关注广播ID：%1。").arg(missingRelevantIds.join(QString::fromUtf8("、"))));
    broadcastLines << QString::fromUtf8("广播帧总数：%1").arg(totalBroadcastCount);
    broadcastLines << QString();
    broadcastLines << QString::fromUtf8("周期检查：");

    for (const int canId : relevantIds) {
        const QVector<EvidenceLogFrame> idFrames = broadcastFrames.value(canId);
        const QString idText = formatCanId(canId);
        broadcastLines << conciseBroadcastPeriodLine(testCase, canId, idFrames);
    }

    broadcastLines << QString();
    broadcastLines << QString::fromUtf8("关键帧：");
    for (const int canId : relevantIds) {
        const QVector<EvidenceLogFrame> idFrames = broadcastFrames.value(canId);
        const QString idText = formatCanId(canId);
        if (!idFrames.isEmpty()) {
            broadcastLines << QString::fromUtf8("%1 首帧：%2").arg(idText, evidenceCompactFrameLine(idFrames.first()));
            broadcastLines << QString::fromUtf8("%1 末帧：%2").arg(idText, evidenceCompactFrameLine(idFrames.last()));
        }
        if (canId >= 0x2C3 && canId <= 0x2C5 && idFrames.size() > 1) {
            const int firstWindowSize = qMin(20, idFrames.size());
            QVector<EvidenceLogFrame> firstWindow;
            for (int i = 0; i < firstWindowSize; ++i) {
                firstWindow.append(idFrames.at(i));
            }
            broadcastLines << QString::fromUtf8("%1 上电前%2帧：%3")
                .arg(idText)
                .arg(firstWindowSize)
                .arg(periodSummaryLine(QString::fromUtf8("周期"), firstWindow));
            if (idFrames.size() > 20) {
                QVector<EvidenceLogFrame> normalWindow;
                for (int i = 20; i < idFrames.size(); ++i) {
                    normalWindow.append(idFrames.at(i));
                }
                broadcastLines << QString::fromUtf8("%1 第20帧后：%2")
                    .arg(idText, periodSummaryLine(QString::fromUtf8("周期"), normalWindow));
            } else {
                broadcastLines << QString::fromUtf8("%1 第20帧后：当前日志仅采集到%2帧，需至少采集到第22帧才能形成10s周期样本，建议采集窗口不少于25s。")
                    .arg(idText)
                    .arg(idFrames.size());
            }
        }
    }

    if (!supplementalIds.isEmpty()) {
        QStringList supplementalSummary;
        for (const int canId : supplementalIds) {
            const QVector<EvidenceLogFrame> idFrames = broadcastFrames.value(canId);
            const QString idText = formatCanId(canId);
            supplementalSummary << QString::fromUtf8("%1=%2帧").arg(idText).arg(idFrames.size());
        }
        broadcastLines << QString();
        broadcastLines << QString::fromUtf8("补充采集：%1。").arg(supplementalSummary.join(QString::fromUtf8("，")));
    }
    broadcastLines << QString();
    broadcastLines << QString::fromUtf8("说明：周期样本为相邻两帧时间差，完整帧见【原始帧记录】。");
    replaceEvidenceSection(caseId, QString::fromUtf8("广播证据"), broadcastLines);

    const bool requireTransmitEvidence = caseRequiresTransmitEvidence(testCase);
    const bool requireReceiveEvidence = caseRequiresReceiveEvidence(testCase);
    QStringList completenessLines;
    if ((!requireTransmitEvidence || hasTransmitEvidence) &&
        (!requireReceiveEvidence || hasReceiveEvidence) &&
        missingRelevantIds.isEmpty()) {
        completenessLines << QString::fromUtf8("证据完整性：完整");
    } else {
        QStringList missing;
        if (requireTransmitEvidence && !hasTransmitEvidence) missing << QString::fromUtf8("缺少发送帧");
        if (requireReceiveEvidence && !hasReceiveEvidence) missing << QString::fromUtf8("缺少非广播接收帧");
        if (!missingRelevantIds.isEmpty()) missing << QString::fromUtf8("缺少广播帧 %1").arg(missingRelevantIds.join(QString::fromUtf8("、")));
        completenessLines << QString::fromUtf8("证据完整性：%1").arg(missing.isEmpty() ? QString::fromUtf8("需人工确认") : missing.join(QString::fromUtf8("；")));
    }
    replaceEvidenceSection(caseId, QString::fromUtf8("证据完整性"), completenessLines);
    return true;
}

bool TestCaseService::appendEvidenceSection(const QString &caseId, const QString &title, const QStringList &lines) const
{
    QFile file(caseEvidencePath(caseId));
    if (!file.open(QIODevice::Append | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << '\n' << QString::fromUtf8("【%1】").arg(title) << '\n';
    for (QString line : lines) {
        line.replace(QLatin1Char(','), QString::fromUtf8("，"));
        stream << line << '\n';
    }
    return true;
}

bool TestCaseService::replaceEvidenceSection(const QString &caseId, const QString &title, const QStringList &lines) const
{
    QFile file(caseEvidencePath(caseId));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream input(&file);
    input.setCodec("UTF-8");
    QString text = input.readAll();
    file.close();

    const QString header = QString::fromUtf8("【%1】").arg(title);
    const int sectionStart = text.indexOf(header);
    if (sectionStart < 0) {
        return appendEvidenceSection(caseId, title, lines);
    }

    int nextSectionStart = text.indexOf(QString::fromUtf8("\n【"), sectionStart + header.size());
    if (nextSectionStart < 0) {
        nextSectionStart = text.size();
    } else {
        ++nextSectionStart;
    }

    QString replacement = header + QLatin1Char('\n');
    for (QString line : lines) {
        line.replace(QLatin1Char(','), QString::fromUtf8("，"));
        replacement += line + QLatin1Char('\n');
    }
    replacement += QLatin1Char('\n');

    text.replace(sectionStart, nextSectionStart - sectionStart, replacement);

    int duplicateStart = text.indexOf(header, sectionStart + replacement.size());
    while (duplicateStart >= 0) {
        int duplicateEnd = text.indexOf(QString::fromUtf8("\n【"), duplicateStart + header.size());
        if (duplicateEnd < 0) {
            duplicateEnd = text.size();
        } else {
            ++duplicateEnd;
        }
        text.remove(duplicateStart, duplicateEnd - duplicateStart);
        duplicateStart = text.indexOf(header, sectionStart + replacement.size());
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }

    QTextStream output(&file);
    output.setCodec("UTF-8");
    output << text;
    return true;
}

const TestCase *TestCaseService::findCase(const QString &caseId) const
{
    for (const TestCase &testCase : m_cases) {
        if (testCase.id == caseId) {
            return &testCase;
        }
    }
    return nullptr;
}

QString TestCaseService::caseEvidencePath(const QString &caseId) const
{
    return QDir(m_session.sessionDirectory).filePath(QStringLiteral("evidence/%1.log").arg(caseId));
}

QString TestCaseService::resultsCsvPath() const
{
    return QDir(m_session.sessionDirectory).filePath(QStringLiteral("results.csv"));
}

QString TestCaseService::csvEscape(QString value)
{
    value.replace("\"", "\"\"");
    return QString("\"%1\"").arg(value);
}
