#include "testcaseservice.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

namespace {
QString jsonString(const QJsonObject &object, const char *key)
{
    return object.value(QString::fromLatin1(key)).toString().trimmed();
}

int jsonInt(const QJsonObject &object, const char *key, int defaultValue)
{
    const QJsonValue value = object.value(QString::fromLatin1(key));
    return value.isDouble() ? value.toInt(defaultValue) : defaultValue;
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
    if (!ensureEvidenceFile(caseId, error)) {
        return false;
    }

    m_activeCaseId = caseId;
    TestCaseResult result = m_results.value(caseId);
    result.caseId = caseId;
    result.startedAt = QDateTime::currentDateTime();
    result.finishedAt = QDateTime();
    result.evidenceLogPath = caseEvidencePath(caseId);
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
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("无法创建证据日志: %1").arg(file.fileName());
        }
        return false;
    }
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << QString::fromUtf8("\xEF\xBB\xBF");
    stream << "时间,方向,通道,ID,数据,解析\n";
    return true;
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
