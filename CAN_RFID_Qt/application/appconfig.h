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
    int maxLogRows = 1000;
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
    QString serialPortName = "";
    int serialBaudRate = 9600;
    int rs485QueryMode = 0;
    int rs485PollIntervalMs = 500;
    int bbPower = 2000;
    int ffPower = 2000;
    int hlScanTimeMs = -1; // -1 means 0xFFFFFFFF (infinite)
    int hlScanIntervalMs = 1000;
    int hlSavedTagCount = 1;
    bool hlClearAfterRead = false;
    bool hlDecryptEnable = true;
    int mainTopHeight = 560;
    int mainLogHeight = 260;
    bool canLogCompact = false;
    int testCaseListWidth = 520;
    int testCaseDetailWidth = 720;
    int layoutPreset = 1; // 0: compact, 1: standard, 2: large, 3: custom
    bool testSessionCollapsed = true;
    bool autoCompactLogOnTestExecution = true;
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
