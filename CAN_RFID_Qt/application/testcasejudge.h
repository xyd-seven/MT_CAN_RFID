#ifndef TESTCASEJUDGE_H
#define TESTCASEJUDGE_H

#include "testsession.h"

#include <QString>
#include <QStringList>

struct TestJudgeResult
{
    TestResultStatus status = TestResultStatus::Blocked;
    QString reason;
    QString failureCategory;
    QStringList keyFrames;
};

class TestCaseJudge
{
public:
    QString expectationText(const TestCase &testCase) const;
    TestJudgeResult judge(const TestCase &testCase, const QString &evidenceText) const;

private:
    static QString caseJoinedText(const TestCase &testCase);
    static QString compactHexText(QString text);
    static bool isNegativeCase(const QString &caseText);
    static QStringList extractKeyFrames(const QString &evidenceText, const QStringList &keywords);
};

#endif // TESTCASEJUDGE_H
