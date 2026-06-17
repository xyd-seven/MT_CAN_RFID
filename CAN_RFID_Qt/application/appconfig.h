#ifndef APPCONFIG_H
#define APPCONFIG_H

#include <QString>

struct AppConfigData
{
    int deviceTypeIndex = 6;
    int deviceIndex = 0;
    int defaultChannel = 0;
    bool resistanceEnabled = false;
    int scanPeriod10ms = 30;
    int maxLogRows = 5000;
    bool canAutoSaveCsv = false;
    bool stressAutoSaveCsv = false;
    bool stressAutoExportSummary = false;
    int stressDurationSeconds = 0;
    int stressTargetSamples = 0;
    QString logDirectory;
    bool rfidControlEnabled = true;
    bool show0x207Log = true;
    int protocolMode = 0; // 0: Meituan, 1: Qingju
    bool qjAutoWritePwd = false;
    int qjTargetDevice = 0x0A;
    int qjQueryMode = 0; // 0: Auto Poll, 1: Single Query
    int qjHostPollIntervalMs = 500;
};

class AppConfig
{
public:
    AppConfig();

    AppConfigData load() const;
    void save(const AppConfigData &config) const;

private:
    QString settingsFilePath() const;
};

#endif // APPCONFIG_H
