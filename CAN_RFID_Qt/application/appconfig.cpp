#include "appconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QtGlobal>

AppConfig::AppConfig()
{
}

AppConfigData AppConfig::load() const
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    AppConfigData config;

    config.deviceTypeIndex = settings.value("device/typeIndex", config.deviceTypeIndex).toInt();
    config.deviceIndex = settings.value("device/index", config.deviceIndex).toInt();
    config.defaultChannel = settings.value("device/channel", config.defaultChannel).toInt();
    config.resistanceEnabled = settings.value("device/resistanceEnabled", config.resistanceEnabled).toBool();
    config.scanPeriod10ms = settings.value("rfid/scanPeriod10ms", config.scanPeriod10ms).toInt();
    config.maxLogRows = qBound(1, settings.value("log/maxRows", config.maxLogRows).toInt(), 1000);
    config.canAutoSaveCsv = settings.value("log/canAutoSaveCsv", config.canAutoSaveCsv).toBool();
    config.stressAutoSaveCsv = settings.value("stress/autoSaveCsv", config.stressAutoSaveCsv).toBool();
    config.stressAutoExportSummary = settings.value("stress/autoExportSummary", config.stressAutoExportSummary).toBool();
    config.stressDurationSeconds = settings.value("stress/durationSeconds", config.stressDurationSeconds).toInt();
    config.stressTargetSamples = settings.value("stress/targetSamples", config.stressTargetSamples).toInt();
    config.logDirectory = settings.value("log/directory", config.logDirectory).toString();
    config.rfidControlEnabled = settings.value("rfid/controlEnabled", config.rfidControlEnabled).toBool();
    config.show0x207Log = settings.value("log/show0x207Log", config.show0x207Log).toBool();
    config.protocolMode = settings.value("device/protocolMode", config.protocolMode).toInt();
    config.qjAutoWritePwd = settings.value("rfid/qjAutoWritePwd", config.qjAutoWritePwd).toBool();
    config.qjTargetDevice = settings.value("rfid/qjTargetDevice", config.qjTargetDevice).toInt();
    config.qjQueryMode = settings.value("rfid/qjQueryMode", config.qjQueryMode).toInt();
    config.qjHostPollIntervalMs = settings.value("rfid/qjHostPollIntervalMs", config.qjHostPollIntervalMs).toInt();
    config.serialPortName = settings.value("serial/portName", config.serialPortName).toString();
    config.serialBaudRate = settings.value("serial/baudRate", config.serialBaudRate).toInt();
    config.rs485QueryMode = settings.value("serial/rs485QueryMode", config.rs485QueryMode).toInt();
    config.rs485PollIntervalMs = settings.value("serial/rs485PollIntervalMs", config.rs485PollIntervalMs).toInt();
    config.bbPower = settings.value("serial/bbPower", config.bbPower).toInt();
    config.ffPower = settings.value("serial/ffPower", config.ffPower).toInt();
    config.hlScanTimeMs = settings.value("serial/hlScanTimeMs", config.hlScanTimeMs).toInt();
    config.hlScanIntervalMs = settings.value("serial/hlScanIntervalMs", config.hlScanIntervalMs).toInt();
    config.hlSavedTagCount = settings.value("serial/hlSavedTagCount", config.hlSavedTagCount).toInt();
    config.hlClearAfterRead = settings.value("serial/hlClearAfterRead", config.hlClearAfterRead).toBool();
    config.hlDecryptEnable = settings.value("serial/hlDecryptEnable", config.hlDecryptEnable).toBool();
    config.mainTopHeight = qBound(360, settings.value("layout/mainTopHeight", config.mainTopHeight).toInt(), 4000);
    config.mainLogHeight = qBound(80, settings.value("layout/mainLogHeight", config.mainLogHeight).toInt(), 2000);
    config.canLogCompact = settings.value("layout/canLogCompact", config.canLogCompact).toBool();
    config.testCaseListWidth = qBound(260, settings.value("layout/testCaseListWidth", config.testCaseListWidth).toInt(), 4000);
    config.testCaseDetailWidth = qBound(360, settings.value("layout/testCaseDetailWidth", config.testCaseDetailWidth).toInt(), 4000);
    config.layoutPreset = qBound(0, settings.value("layout/preset", config.layoutPreset).toInt(), 3);
    config.testSessionCollapsed = settings.value("layout/testSessionCollapsed", config.testSessionCollapsed).toBool();
    config.autoCompactLogOnTestExecution = settings.value("layout/autoCompactLogOnTestExecution", config.autoCompactLogOnTestExecution).toBool();

    if (config.logDirectory.isEmpty()) {
        config.logDirectory = QDir(QCoreApplication::applicationDirPath()).filePath("logs");
    }
    return config;
}

void AppConfig::save(const AppConfigData &config) const
{
    QSettings settings(settingsFilePath(), QSettings::IniFormat);
    settings.setValue("device/typeIndex", config.deviceTypeIndex);
    settings.setValue("device/index", config.deviceIndex);
    settings.setValue("device/channel", config.defaultChannel);
    settings.setValue("device/resistanceEnabled", config.resistanceEnabled);
    settings.setValue("rfid/scanPeriod10ms", config.scanPeriod10ms);
    settings.setValue("log/maxRows", config.maxLogRows);
    settings.setValue("log/canAutoSaveCsv", config.canAutoSaveCsv);
    settings.setValue("stress/autoSaveCsv", config.stressAutoSaveCsv);
    settings.setValue("stress/autoExportSummary", config.stressAutoExportSummary);
    settings.setValue("stress/durationSeconds", config.stressDurationSeconds);
    settings.setValue("stress/targetSamples", config.stressTargetSamples);
    settings.setValue("log/directory", config.logDirectory);
    settings.setValue("rfid/controlEnabled", config.rfidControlEnabled);
    settings.setValue("log/show0x207Log", config.show0x207Log);
    settings.setValue("device/protocolMode", config.protocolMode);
    settings.setValue("rfid/qjAutoWritePwd", config.qjAutoWritePwd);
    settings.setValue("rfid/qjTargetDevice", config.qjTargetDevice);
    settings.setValue("rfid/qjQueryMode", config.qjQueryMode);
    settings.setValue("rfid/qjHostPollIntervalMs", config.qjHostPollIntervalMs);
    settings.setValue("serial/portName", config.serialPortName);
    settings.setValue("serial/baudRate", config.serialBaudRate);
    settings.setValue("serial/rs485QueryMode", config.rs485QueryMode);
    settings.setValue("serial/rs485PollIntervalMs", config.rs485PollIntervalMs);
    settings.setValue("serial/bbPower", config.bbPower);
    settings.setValue("serial/ffPower", config.ffPower);
    settings.setValue("serial/hlScanTimeMs", config.hlScanTimeMs);
    settings.setValue("serial/hlScanIntervalMs", config.hlScanIntervalMs);
    settings.setValue("serial/hlSavedTagCount", config.hlSavedTagCount);
    settings.setValue("serial/hlClearAfterRead", config.hlClearAfterRead);
    settings.setValue("serial/hlDecryptEnable", config.hlDecryptEnable);
    settings.setValue("layout/mainTopHeight", config.mainTopHeight);
    settings.setValue("layout/mainLogHeight", config.mainLogHeight);
    settings.setValue("layout/canLogCompact", config.canLogCompact);
    settings.setValue("layout/testCaseListWidth", config.testCaseListWidth);
    settings.setValue("layout/testCaseDetailWidth", config.testCaseDetailWidth);
    settings.setValue("layout/preset", config.layoutPreset);
    settings.setValue("layout/testSessionCollapsed", config.testSessionCollapsed);
    settings.setValue("layout/autoCompactLogOnTestExecution", config.autoCompactLogOnTestExecution);
    settings.sync();
}

QString AppConfig::settingsFilePath() const
{
    const QString writableConfigPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!writableConfigPath.isEmpty()) {
        QDir().mkpath(writableConfigPath);
        return QDir(writableConfigPath).filePath("settings.ini");
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath("settings.ini");
}
