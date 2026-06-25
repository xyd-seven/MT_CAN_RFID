#ifndef TESTSUMMARYBUILDER_H
#define TESTSUMMARYBUILDER_H

#include "testsession.h"

#include <QMap>
#include <QVector>

class TestSummaryBuilder
{
public:
    QString progressText(const QVector<TestCase> &cases, const QMap<QString, TestCaseResult> &results) const;
    QString failureSummaryMarkdown(const QVector<TestCase> &cases, const QMap<QString, TestCaseResult> &results) const;
    QString retestListText(const QVector<TestCase> &cases, const QMap<QString, TestCaseResult> &results) const;
    QString reportConclusion(const QVector<TestCase> &cases, const QMap<QString, TestCaseResult> &results) const;
};

#endif // TESTSUMMARYBUILDER_H
