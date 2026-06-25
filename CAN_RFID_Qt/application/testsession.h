#ifndef TESTSESSION_H
#define TESTSESSION_H

#include <QDateTime>
#include <QString>
#include <QStringList>

enum class TestResultStatus
{
    NotRun,
    Passed,
    Failed,
    Blocked,
    NotApplicable
};

struct TestCase
{
    QString id;
    QString module;
    QString priority;
    QString type;
    QString basis;
    QString precondition;
    QString testData;
    QString steps;
    QString expectedResult;
    QString executionMode;
    QString commandTemplate;
    QString judgeTemplate;
    QString manualPrompt;
    int timeoutMs = 1000;
    int retryCount = 0;
};

struct TestCaseResult
{
    QString caseId;
    TestResultStatus status = TestResultStatus::NotRun;
    QString actualResult;
    QString defectId;
    QString remark;
    QDateTime startedAt;
    QDateTime finishedAt;
    QString evidenceLogPath;
    QString failureCategory;
    QString judgeReason;
    QStringList keyFrames;
    QString previousStatus;
    QString previousFailureReason;
    int retestCount = 0;
    QDateTime lastRetestAt;
};

struct TestSession
{
    QString sessionId;
    QString projectName;
    QString softwareVersion;
    QString firmwareVersion;
    QString deviceSn;
    QString tester;
    QString environment;
    QString remark;
    QDateTime createdAt;
    QString sessionDirectory;
};

QString testResultStatusText(TestResultStatus status);
TestResultStatus testResultStatusFromText(const QString &text);

#endif // TESTSESSION_H
