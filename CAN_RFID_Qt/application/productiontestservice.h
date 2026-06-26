#ifndef PRODUCTIONTESTSERVICE_H
#define PRODUCTIONTESTSERVICE_H

#include <QDateTime>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include "application/rfidservice.h"

struct ProductionTestConfig
{
    int totalSamples = 100;
    double passRateThreshold = 95.0;
};

struct ProductionTestState
{
    bool running = false;
    QString phaseText;
    QString sn;
    QString hwVer;
    QString matChange;
    int totalSamples = 100;
    int completedSamples = 0;
    int successCount = 0;
    int failureCount = 0;
    double successRate = 0.0;
    double passRateThreshold = 95.0;
    QString currentTag;
    QString lastFailureReason;
    QString resultText;
    QDateTime startTime;
    QDateTime finishTime;
};

class ProductionTestService : public QObject
{
    Q_OBJECT
public:
    explicit ProductionTestService(QObject *parent = nullptr);

    bool start(const QString &sn, const QString &hwVer, const QString &matChange, const ProductionTestConfig &config, QString *error);
    void stop(const QString &reason);
    void reset();
    void handleWriteFinished(bool success, const QString &message);
    void handleRfidStatus(const RfidState &state);
    void handleRfidTagUpdate(const QString &tag);

    bool isRunning() const;
    bool isWritingSn() const;
    bool isWriting() const;
    bool isTestingCard() const;
    ProductionTestState state() const;

    static bool validateSn(const QString &sn, QString *error);
    static bool validateHwVersion(const QString &hwVer, quint16 *versionValue = nullptr, QString *error = nullptr);
    static bool validateMaterialChange(const QString &matChange, quint16 *value = nullptr, QString *error = nullptr);
    static QByteArray snToBytes(const QString &sn);

signals:
    void writeSnRequested(quint16 did, const QByteArray &data);
    void scanControlRequested(bool enabled);
    void stateChanged(const ProductionTestState &state);
    void logMessage(const QString &message);
    void finished(bool passed, const ProductionTestState &state);

private slots:
    void onTagWaitTimeout();
    void onSampleTimeout();

private:
    enum class Phase
    {
        Idle,
        WritingSn,
        WritingHwVersion,
        WritingMaterialChange,
        TestingCard,
        Passed,
        Failed,
        Stopped
    };

    void setPhase(Phase phase, const QString &phaseText);
    void emitStateChanged();
    void startTesting();
    void finishTest(bool passed, const QString &reason);
    void recordSuccess(const QString &tag);
    void recordFailure(const QString &reason);
    void updateRate();
    void scheduleSampleTimeout();
    void stopTimers();
    bool isCardPresent(const RfidState &state) const;
    bool isNoTag(const RfidState &state) const;
    bool isTagLengthError(const RfidState &state) const;
    bool hasFault(const RfidState &state) const;

    Phase m_phase;
    ProductionTestConfig m_config;
    ProductionTestState m_state;
    RfidState m_pendingState;
    QString m_lastValidTag;
    bool m_waitingTagCompletion;
    QTimer *m_tagWaitTimer;
    QTimer *m_sampleTimeoutTimer;
    qint64 m_cardTestStartTime;
};

#endif // PRODUCTIONTESTSERVICE_H
