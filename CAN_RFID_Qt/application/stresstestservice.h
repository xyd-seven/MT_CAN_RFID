#ifndef STRESSTESTSERVICE_H
#define STRESSTESTSERVICE_H

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QSet>
#include <QString>
#include <QTextStream>
#include "domain/canframe.h"
#include "rfidprotocol.h"

struct QingjuNpkState;

struct StressTestStats
{
    bool running = false;
    QDateTime startTime;
    qint64 elapsedSeconds = 0;
    qint64 elapsedMilliseconds = 0;
    quint64 totalSamples = 0;
    quint64 successCount = 0;
    quint64 noTagCount = 0;
    quint64 tagLengthErrorCount = 0;
    quint64 moduleFaultCount = 0;
    quint64 communicationFaultCount = 0;
    quint64 tagContentErrorCount = 0;
    quint64 pollSkippedCount = 0;
    quint64 validTagCount = 0;
    quint64 tagChangeCount = 0;
    quint64 uniqueTagCount = 0;
    quint64 currentContinuousSuccess = 0;
    quint64 currentContinuousFailure = 0;
    quint64 maxContinuousFailure = 0;
    double successRate = 0.0;
    double tagValidRate = 0.0;
    QString currentTag;
    QString lastSuccessTag;
    QString lastFailureReason;
    QDateTime lastTagUpdateTime;
};

class StressTestService
{
public:
    void start();
    void stop();
    void reset();
    void setOutputDirectory(const QString &directoryPath);
    void setAutoSaveEnabled(bool enabled);
    bool autoSaveEnabled() const;
    bool exportSummary(const QString &filePath) const;
    bool handleFrame(const CanFrame &frame);
    bool handleQingjuState(const QingjuNpkState &state);
    bool handleRs485State(int protocolMode, const QString &tagId, int errCode, bool isCommunicationTimeout, const QString &errorMsg);
    void recordRs485PollSkipped();
    StressTestStats stats() const;

private:
    void handleStatusFrame(const CanFrame &frame);
    void updateTagPart(quint32 frameId, const QByteArray &payload);
    void updateRates();
    bool currentTagIsValid() const;
    bool isValidTagText(const QString &tag) const;
    QString currentTagText() const;
    void clearCurrentTag();
    void markSuccess();
    void markFailure(const QString &reason);
    void writeSampleCsv(const CanFrame &frame, const RfidStatus &status, bool success, bool tagValid);
    void writeQingjuSampleCsv(const QingjuNpkState &state, bool success, bool tagValid);
    void writeRs485SampleCsv(int protocolMode, const QString &tagId, int errCode, bool isCommunicationTimeout, const QString &errorMsg, bool success, bool tagValid);
    void ensureSampleCsvOpen();
    QString csvEscape(const QString &value) const;

    StressTestStats currentStats;
    QString tagPart1;
    QString tagPart2;
    QString tagPart3;
    QSet<QString> uniqueTags;
    QString outputDirectory;
    bool autoSaveCsv = false;
    QFile sampleCsvFile;
    QElapsedTimer elapsedTimer;
    int pendingSampleRows = 0;
};

#endif // STRESSTESTSERVICE_H
