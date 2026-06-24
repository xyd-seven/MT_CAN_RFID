#ifndef TESTCASESERVICE_H
#define TESTCASESERVICE_H

#include "testsession.h"

#include <QMap>
#include <QVector>

class CanFrame;

class TestCaseService
{
public:
    bool loadCasesFromJsonFile(const QString &filePath, QString *error = nullptr);
    bool loadCasesFromJsonData(const QByteArray &jsonData, QString *error = nullptr);

    const QVector<TestCase> &cases() const;
    QStringList modules() const;

    bool createSession(const TestSession &session, QString *error = nullptr);
    bool hasSession() const;
    const TestSession &session() const;

    bool startCase(const QString &caseId, QString *error = nullptr);
    bool hasActiveCase() const;
    QString activeCaseId() const;
    void finishActiveCase();

    TestCaseResult resultForCase(const QString &caseId) const;
    QMap<QString, TestCaseResult> results() const;
    bool saveResult(const TestCaseResult &result, QString *error = nullptr);

    bool appendEvidence(const QString &direction,
                        const QString &channel,
                        const QString &frameId,
                        const QString &dataHex,
                        const QString &decodedText,
                        QString *error = nullptr);

    bool exportResultsCsv(const QString &filePath, QString *error = nullptr) const;
    bool exportResultsExcelHtml(const QString &filePath, QString *error = nullptr) const;
    QString evidencePathForCase(const QString &caseId) const;
    bool saveSessionJson(QString *error = nullptr) const;

private:
    bool ensureEvidenceFile(const QString &caseId, QString *error);
    QString caseEvidencePath(const QString &caseId) const;
    QString resultsCsvPath() const;
    static QString csvEscape(QString value);

    QVector<TestCase> m_cases;
    QMap<QString, TestCaseResult> m_results;
    TestSession m_session;
    QString m_activeCaseId;
    bool m_hasSession = false;
};

#endif // TESTCASESERVICE_H
