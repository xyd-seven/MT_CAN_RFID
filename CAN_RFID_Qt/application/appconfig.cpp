#include "appconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>

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
    config.maxLogRows = settings.value("log/maxRows", config.maxLogRows).toInt();
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
