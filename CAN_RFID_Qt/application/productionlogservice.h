#ifndef PRODUCTIONLOGSERVICE_H
#define PRODUCTIONLOGSERVICE_H

#include <QDate>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QtGlobal>

struct ProductionTestState;

struct ProductionDailyStats
{
    QDate date;
    int total = 0;
    int passed = 0;
    int failed = 0;
    int stopped = 0;

    double passRate() const;
};

class ProductionLogService
{
public:
    void setOutputDirectory(const QString &directoryPath);
    QString productionDirectory() const;
    QString lastError() const;

    bool appendEvent(int protocolMode,
                     int stationNumber,
                     quint32 deviceIndex,
                     const ProductionTestState &state,
                     const QString &level,
                     const QString &message);
    bool appendResult(int protocolMode,
                      int stationNumber,
                      quint32 deviceIndex,
                      const ProductionTestState &state,
                      const QString &readHardwareVersion,
                      const QString &readMaterialVersion,
                      const QString &readDeviceId);
    ProductionDailyStats dailyStats();

private:
    bool appendCsvRow(const QString &filePath,
                      const QStringList &header,
                      const QStringList &values);
    void loadDailyStats(const QDate &date);
    QString eventFilePath(const QDate &date) const;
    QString resultFilePath(const QDate &date) const;
    static QString protocolName(int protocolMode);
    static QString csvEscape(const QString &value);
    static QStringList parseCsvLine(const QString &line);

    QString m_outputDirectory;
    QString m_lastError;
    ProductionDailyStats m_dailyStats;
    QHash<QString, QString> m_dailyResultBySn;
};

#endif // PRODUCTIONLOGSERVICE_H
