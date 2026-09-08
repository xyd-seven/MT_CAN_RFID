#include "dualproductionwidget.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFont>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QSizePolicy>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "application/productionstationcontroller.h"
#include "rfidprotocol.h"

namespace {
constexpr int StationCount = 2;
constexpr int ProductionCanChannel = 0;
constexpr int ScanStableDelayMs = 200;
constexpr int TotalSamples = 100;
constexpr int MaxDeviceIndex = 15;

bool useCompactProductionLayout()
{
    const QScreen *screen = QApplication::primaryScreen();
    return screen != nullptr && screen->availableGeometry().height() <= 740;
}

QString protocolName(int protocolMode)
{
    return protocolMode == 1 ? QStringLiteral("青桔") : QStringLiteral("美团");
}

QString productionEventLevel(const QString &message)
{
    if (message.contains(QStringLiteral("失败")) ||
        message.contains(QStringLiteral("错误")) ||
        message.contains(QStringLiteral("超时")) ||
        message.contains(QStringLiteral("拒绝")) ||
        message.contains(QStringLiteral("不一致"))) {
        return QStringLiteral("ERROR");
    }
    if (message.contains(QStringLiteral("停止")) ||
        message.contains(QStringLiteral("警告"))) {
        return QStringLiteral("WARNING");
    }
    return QStringLiteral("INFO");
}

QString persistedReadbackValue(const QLabel *label)
{
    if (label == nullptr) {
        return QString();
    }
    const QString value = label->text().trimmed();
    if (value.startsWith(QStringLiteral("等待")) ||
        value == QStringLiteral("仅美团协议") ||
        value.contains(QStringLiteral("不适用")) ||
        value.contains(QStringLiteral("非ASCII"))) {
        return QString();
    }
    return value;
}
}

DualProductionWidget::DualProductionWidget(QWidget *parent)
    : QWidget(parent)
    , m_stations{new ProductionStationController(1, this),
                 new ProductionStationController(2, this)}
    , m_deviceType(41)
    , m_resistanceEnabled(false)
    , m_mainCanBusy(false)
    , m_protocolMode(0)
    , m_preferredStation(0)
    , m_shuttingDown(false)
    , m_meituanLocked(false)
    , m_qingjuLocked(false)
    , m_deviceIndexSpins{nullptr, nullptr}
    , m_protocolLabel(nullptr)
    , m_nextStationLabel(nullptr)
    , m_scanEdit(nullptr)
    , m_hwVersionEdit(nullptr)
    , m_materialChangeEdit(nullptr)
    , m_materialChangeLabel(nullptr)
    , m_passThresholdSpin(nullptr)
    , m_parameterLockButton(nullptr)
    , m_startDevicesButton(nullptr)
    , m_stopDevicesButton(nullptr)
    , m_startTestButton(nullptr)
    , m_statsDateValue(nullptr)
    , m_statsTotalValue(nullptr)
    , m_statsPassedValue(nullptr)
    , m_statsFailedValue(nullptr)
    , m_statsStoppedValue(nullptr)
    , m_statsRateValue(nullptr)
    , m_openLogDirectoryButton(nullptr)
    , m_systemLogLabel(nullptr)
    , m_stationLogTexts{nullptr, nullptr}
    , m_scanTimer(new QTimer(this))
{
    m_scanTimer->setSingleShot(true);
    buildUi();
    for (int stationIndex = 0; stationIndex < StationCount; ++stationIndex) {
        connectStation(stationIndex);
    }
    connect(m_scanTimer, &QTimer::timeout, this, [this]() {
        if (!m_scanBuffer.isEmpty()) {
            const QString scanText = m_scanBuffer;
            m_scanBuffer.clear();
            processScan(scanText);
        }
    });
    restoreCurrentProtocolSettings();
    updateUiState();
}

DualProductionWidget::~DualProductionWidget()
{
    shutdown();
}

void DualProductionWidget::loadConfig(const AppConfigData &config)
{
    m_deviceIndexSpins[0]->setValue(config.productionStation1DeviceIndex);
    m_deviceIndexSpins[1]->setValue(config.productionStation2DeviceIndex);
    m_meituanHwVersion = config.productionHwVer;
    m_meituanLocked = config.productionHwVerLocked;
    m_qingjuHwVersion = config.qingjuProductionHwVer;
    m_qingjuLocked = config.qingjuProductionHwVerLocked;
    m_materialChange = config.productionMatChange;
    setLogDirectory(config.logDirectory);
    restoreCurrentProtocolSettings();
    updateUiState();
}

void DualProductionWidget::saveConfig(AppConfigData *config) const
{
    if (config == nullptr) {
        return;
    }
    config->productionStation1DeviceIndex = m_deviceIndexSpins[0]->value();
    config->productionStation2DeviceIndex = m_deviceIndexSpins[1]->value();
    config->productionHwVer =
        m_protocolMode == 0 ? m_hwVersionEdit->text().trimmed() : m_meituanHwVersion;
    config->productionHwVerLocked =
        m_protocolMode == 0 ? m_meituanLocked : config->productionHwVerLocked;
    config->qingjuProductionHwVer =
        m_protocolMode == 1 ? m_hwVersionEdit->text().trimmed() : m_qingjuHwVersion;
    config->qingjuProductionHwVerLocked =
        m_protocolMode == 1 ? m_qingjuLocked : config->qingjuProductionHwVerLocked;
    config->productionMatChange = m_materialChangeEdit->text().trimmed();
}

void DualProductionWidget::setDeviceType(quint32 deviceType)
{
    if (!devicesActive()) {
        m_deviceType = deviceType;
    }
}

void DualProductionWidget::setResistanceEnabled(bool enabled)
{
    if (!devicesActive()) {
        m_resistanceEnabled = enabled;
    }
}

void DualProductionWidget::setProtocolMode(int protocolMode)
{
    if ((protocolMode != 0 && protocolMode != 1) || devicesActive() ||
        protocolMode == m_protocolMode) {
        return;
    }
    storeCurrentProtocolSettings();
    m_protocolMode = protocolMode;
    for (ProductionStationController *station : m_stations) {
        station->setProtocolMode(protocolMode);
    }
    for (int stationIndex = 0; stationIndex < StationCount; ++stationIndex) {
        resetStationReadback(stationIndex);
    }
    restoreCurrentProtocolSettings();
    updateUiState();
}

void DualProductionWidget::setMainCanBusy(bool busy)
{
    m_mainCanBusy = busy;
    updateUiState();
}

void DualProductionWidget::setLogDirectory(const QString &directoryPath)
{
    m_productionLogService.setOutputDirectory(directoryPath);
    updateProductionStats();
    if (m_systemLogLabel != nullptr && !m_productionLogService.lastError().isEmpty()) {
        m_systemLogLabel->setText(
            QStringLiteral("%1 [日志] %2")
                .arg(QDateTime::currentDateTime().toString("HH:mm:ss"),
                     m_productionLogService.lastError()));
    }
}

bool DualProductionWidget::handleKeyEvent(QKeyEvent *event)
{
    if (event == nullptr || event->type() != QEvent::KeyPress) {
        return false;
    }
    if (event->key() == Qt::Key_F1 || event->key() == Qt::Key_F2) {
        setPreferredStation(event->key() == Qt::Key_F1 ? 0 : 1);
        return true;
    }
    if (!devicesActive() || !(m_meituanLocked || m_qingjuLocked)) {
        return false;
    }

    QWidget *focusWidget = QApplication::focusWidget();
    QLineEdit *focusedLineEdit = qobject_cast<QLineEdit *>(focusWidget);
    if (focusedLineEdit != nullptr && focusedLineEdit != m_scanEdit &&
        !focusedLineEdit->isReadOnly() && !isAnyStationRunning()) {
        return false;
    }
    if (m_scanEdit->hasFocus()) {
        return false;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (!m_scanBuffer.isEmpty()) {
            const QString scanText = m_scanBuffer;
            m_scanBuffer.clear();
            m_scanTimer->stop();
            processScan(scanText);
            return true;
        }
        return false;
    }
    const QString text = event->text();
    if (text.size() == 1 && text.at(0).isPrint()) {
        m_scanBuffer.append(text);
        m_scanTimer->start(ScanStableDelayMs);
        return true;
    }
    return false;
}

bool DualProductionWidget::devicesActive() const
{
    return m_stations[0]->isDeviceReady() || m_stations[1]->isDeviceReady();
}

bool DualProductionWidget::isAnyStationRunning() const
{
    return m_stations[0]->isRunning() || m_stations[1]->isRunning();
}

void DualProductionWidget::startConfiguredDevices()
{
    startStationOneDevice();
}

void DualProductionWidget::shutdown()
{
    if (m_shuttingDown) {
        return;
    }
    m_shuttingDown = true;
    m_scanTimer->stop();
    for (ProductionStationController *station : m_stations) {
        station->stopDevice();
    }
}

void DualProductionWidget::buildUi()
{
    const bool compactLayout = useCompactProductionLayout();
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(6, compactLayout ? 2 : 5, 6, compactLayout ? 2 : 5);
    mainLayout->setSpacing(compactLayout ? 2 : 4);

    QGroupBox *controlGroup = new QGroupBox(QStringLiteral("产线控制"), this);
    QGridLayout *controlLayout = new QGridLayout(controlGroup);
    controlLayout->setContentsMargins(8, compactLayout ? 3 : 6, 8, compactLayout ? 3 : 6);
    controlLayout->setHorizontalSpacing(6);
    controlLayout->setVerticalSpacing(compactLayout ? 2 : 4);

    QLabel *deviceSectionLabel = new QLabel(QStringLiteral("设备"), controlGroup);
    deviceSectionLabel->setStyleSheet(QStringLiteral("font-weight:600;color:#374151;"));
    controlLayout->addWidget(deviceSectionLabel, 0, 0);
    for (int stationIndex = 0; stationIndex < StationCount; ++stationIndex) {
        m_deviceIndexSpins[stationIndex] = new QSpinBox(controlGroup);
        m_deviceIndexSpins[stationIndex]->setRange(0, MaxDeviceIndex);
        m_deviceIndexSpins[stationIndex]->setValue(stationIndex);
        m_deviceIndexSpins[stationIndex]->setMaximumWidth(56);
        controlLayout->addWidget(
            new QLabel(QStringLiteral("工位%1索引").arg(stationIndex + 1), controlGroup),
            0, stationIndex * 2 + 1);
        controlLayout->addWidget(m_deviceIndexSpins[stationIndex], 0, stationIndex * 2 + 2);
        connect(m_deviceIndexSpins[stationIndex], QOverload<int>::of(&QSpinBox::valueChanged),
                this, &DualProductionWidget::configChanged);
    }
    m_startDevicesButton = new QPushButton(QStringLiteral("启动双工位"), controlGroup);
    m_stopDevicesButton = new QPushButton(QStringLiteral("关闭双工位"), controlGroup);
    m_protocolLabel = new QLabel(controlGroup);
    controlLayout->addWidget(m_protocolLabel, 0, 5);
    controlLayout->addWidget(m_startDevicesButton, 0, 6);
    controlLayout->addWidget(m_stopDevicesButton, 0, 7);
    controlLayout->setColumnStretch(5, 1);
    connect(m_startDevicesButton, &QPushButton::clicked,
            this, &DualProductionWidget::startDevices);
    connect(m_stopDevicesButton, &QPushButton::clicked, this, [this]() {
        stopDevices(true);
    });

    QLabel *statsSectionLabel = new QLabel(QStringLiteral("今日统计"), controlGroup);
    statsSectionLabel->setStyleSheet(QStringLiteral("font-weight:600;color:#374151;"));
    controlLayout->addWidget(statsSectionLabel, 1, 0);
    QWidget *statsWidget = new QWidget(controlGroup);
    QHBoxLayout *statsLayout = new QHBoxLayout(statsWidget);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->setSpacing(6);
    m_statsDateValue = new QLabel(statsWidget);
    m_statsTotalValue = new QLabel(QStringLiteral("0"), statsWidget);
    m_statsPassedValue = new QLabel(QStringLiteral("0"), statsWidget);
    m_statsFailedValue = new QLabel(QStringLiteral("0"), statsWidget);
    m_statsStoppedValue = new QLabel(QStringLiteral("0"), statsWidget);
    m_statsRateValue = new QLabel(QStringLiteral("0.00%"), statsWidget);
    m_statsRateValue->setToolTip(QStringLiteral("通过率按 PASS / (PASS + FAIL) 计算，不包含人工停止"));
    m_openLogDirectoryButton = new QPushButton(QStringLiteral("打开日志目录"), statsWidget);
    statsLayout->addWidget(m_statsDateValue);
    statsLayout->addSpacing(12);
    statsLayout->addWidget(new QLabel(QStringLiteral("总数"), statsWidget));
    statsLayout->addWidget(m_statsTotalValue);
    statsLayout->addWidget(new QLabel(QStringLiteral("通过"), statsWidget));
    statsLayout->addWidget(m_statsPassedValue);
    statsLayout->addWidget(new QLabel(QStringLiteral("失败"), statsWidget));
    statsLayout->addWidget(m_statsFailedValue);
    statsLayout->addWidget(new QLabel(QStringLiteral("停止"), statsWidget));
    statsLayout->addWidget(m_statsStoppedValue);
    statsLayout->addWidget(new QLabel(QStringLiteral("通过率"), statsWidget));
    statsLayout->addWidget(m_statsRateValue);
    statsLayout->addStretch();
    statsLayout->addWidget(m_openLogDirectoryButton);
    controlLayout->addWidget(statsWidget, 1, 1, 1, 7);
    connect(m_openLogDirectoryButton, &QPushButton::clicked, this, [this]() {
        const QString directory = m_productionLogService.productionDirectory();
        if (directory.isEmpty() || !QDir().mkpath(directory)) {
            QMessageBox::warning(this,
                                 QStringLiteral("打开日志目录"),
                                 QStringLiteral("产线日志目录未配置或无法创建。"));
            return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(directory))) {
            QMessageBox::warning(this,
                                 QStringLiteral("打开日志目录"),
                                 QStringLiteral("无法打开目录：%1").arg(directory));
        }
    });

    m_nextStationLabel = new QLabel(controlGroup);
    m_nextStationLabel->setAlignment(Qt::AlignCenter);
    m_nextStationLabel->setMinimumWidth(compactLayout ? 150 : 180);
    QFont targetFont = m_nextStationLabel->font();
    targetFont.setPointSize(compactLayout ? 12 : 13);
    targetFont.setBold(true);
    m_nextStationLabel->setFont(targetFont);

    m_scanEdit = new QLineEdit(controlGroup);
    m_scanEdit->setPlaceholderText(QStringLiteral("直接扫码，无需点击输入框（F1/F2指定工位）"));
    m_scanEdit->setMaxLength(32);
    m_scanEdit->setClearButtonEnabled(true);
    m_hwVersionEdit = new QLineEdit(controlGroup);
    m_hwVersionEdit->setPlaceholderText(QStringLiteral("例如 1.0.1 或 0x0101"));
    m_materialChangeEdit = new QLineEdit(controlGroup);
    m_materialChangeEdit->setPlaceholderText(QStringLiteral("例如 0x0001"));
    m_materialChangeLabel = new QLabel(QStringLiteral("物料变更"), controlGroup);
    m_passThresholdSpin = new QSpinBox(controlGroup);
    m_passThresholdSpin->setRange(0, 100);
    m_passThresholdSpin->setValue(95);
    m_passThresholdSpin->setSuffix(QStringLiteral(" %"));
    m_parameterLockButton = new QPushButton(QStringLiteral("确定参数"), controlGroup);
    m_startTestButton = new QPushButton(QStringLiteral("开始检测"), controlGroup);

    controlLayout->addWidget(m_nextStationLabel, 2, 0, 2, 1);
    controlLayout->addWidget(new QLabel(QStringLiteral("终端SN"), controlGroup), 2, 1);
    controlLayout->addWidget(m_scanEdit, 2, 2, 1, 5);
    controlLayout->addWidget(new QLabel(QStringLiteral("硬件版本"), controlGroup), 3, 1);
    controlLayout->addWidget(m_hwVersionEdit, 3, 2);
    controlLayout->addWidget(m_materialChangeLabel, 3, 3);
    controlLayout->addWidget(m_materialChangeEdit, 3, 4);
    controlLayout->addWidget(new QLabel(QStringLiteral("通过阈值"), controlGroup), 3, 5);
    controlLayout->addWidget(m_passThresholdSpin, 3, 6);
    controlLayout->addWidget(m_parameterLockButton, 3, 7);
    controlLayout->addWidget(m_startTestButton, 2, 7);
    controlLayout->setColumnStretch(2, 2);
    controlLayout->setColumnStretch(4, 1);
    connect(m_parameterLockButton, &QPushButton::clicked,
            this, &DualProductionWidget::toggleParameterLock);
    connect(m_startTestButton, &QPushButton::clicked, this, [this]() {
        processScan(m_scanEdit->text());
    });
    connect(m_scanEdit, &QLineEdit::returnPressed, this, [this]() {
        processScan(m_scanEdit->text());
    });
    connect(m_scanEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        QString error;
        const QString normalizedSn = text.trimmed().toUpper();
        if (!ProductionTestService::validateSn(normalizedSn, m_protocolMode, &error)) {
            return;
        }
        QTimer::singleShot(ScanStableDelayMs, this, [this, normalizedSn]() {
            if (m_scanEdit->text().trimmed().compare(
                    normalizedSn, Qt::CaseInsensitive) == 0) {
                processScan(normalizedSn);
            }
        });
    });

    QWidget *stationContainer = new QWidget(this);
    stationContainer->setMinimumHeight(compactLayout ? 200 : 240);
    stationContainer->setMaximumHeight(compactLayout ? 220 : 290);
    stationContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    QHBoxLayout *stationLayout = new QHBoxLayout(stationContainer);
    stationLayout->setContentsMargins(0, 0, 0, 0);
    stationLayout->setSpacing(8);
    for (int stationIndex = 0; stationIndex < StationCount; ++stationIndex) {
        stationLayout->addWidget(createStationGroup(stationIndex, &m_panels[stationIndex]));
    }

    QGroupBox *logGroup = new QGroupBox(QStringLiteral("过程日志"), this);
    QVBoxLayout *logLayout = new QVBoxLayout(logGroup);
    logLayout->setContentsMargins(6, compactLayout ? 2 : 4, 6, compactLayout ? 3 : 6);
    logLayout->setSpacing(compactLayout ? 1 : 3);

    QWidget *logToolbar = new QWidget(logGroup);
    QHBoxLayout *logToolbarLayout = new QHBoxLayout(logToolbar);
    logToolbarLayout->setContentsMargins(0, 0, 0, 0);
    QLabel *systemLogTitle = new QLabel(QStringLiteral("最新系统消息"), logToolbar);
    systemLogTitle->setStyleSheet(QStringLiteral("font-weight:600;color:#374151;"));
    m_systemLogLabel = new QLabel(QStringLiteral("-"), logToolbar);
    m_systemLogLabel->setStyleSheet(
        QStringLiteral("background:#F9FAFB;border:1px solid #D1D5DB;padding:3px 6px;"));
    m_systemLogLabel->setMinimumHeight(compactLayout ? 20 : 24);
    m_systemLogLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QPushButton *clearSystemLogButton = new QPushButton(QStringLiteral("清除系统"), logToolbar);
    QPushButton *clearAllLogsButton = new QPushButton(QStringLiteral("全部清除"), logToolbar);
    logToolbarLayout->addWidget(systemLogTitle);
    logToolbarLayout->addWidget(m_systemLogLabel, 1);
    logToolbarLayout->addWidget(clearSystemLogButton);
    logToolbarLayout->addWidget(clearAllLogsButton);
    logLayout->addWidget(logToolbar);

    QWidget *stationLogsContainer = new QWidget(logGroup);
    QHBoxLayout *stationLogsLayout = new QHBoxLayout(stationLogsContainer);
    stationLogsLayout->setContentsMargins(0, 0, 0, 0);
    stationLogsLayout->setSpacing(8);
    for (int stationIndex = 0; stationIndex < StationCount; ++stationIndex) {
        QGroupBox *stationLogGroup = new QGroupBox(
            QStringLiteral("工位%1日志").arg(stationIndex + 1), stationLogsContainer);
        QHBoxLayout *stationLogLayout = new QHBoxLayout(stationLogGroup);
        stationLogLayout->setContentsMargins(6, compactLayout ? 3 : 5, 6, compactLayout ? 3 : 6);
        QPushButton *clearStationLogButton = new QPushButton(
            QStringLiteral("清除"), stationLogGroup);
        m_stationLogTexts[stationIndex] = new QTextEdit(stationLogGroup);
        m_stationLogTexts[stationIndex]->setReadOnly(true);
        m_stationLogTexts[stationIndex]->setMinimumHeight(compactLayout ? 60 : 90);
        m_stationLogTexts[stationIndex]->setSizePolicy(
            QSizePolicy::Expanding, QSizePolicy::Expanding);
        stationLogLayout->addWidget(m_stationLogTexts[stationIndex], 1);
        stationLogLayout->addWidget(clearStationLogButton, 0, Qt::AlignTop);
        stationLogsLayout->addWidget(stationLogGroup, 1);
        connect(clearStationLogButton, &QPushButton::clicked,
                m_stationLogTexts[stationIndex], &QTextEdit::clear);
    }
    logLayout->addWidget(stationLogsContainer, 1);

    connect(clearSystemLogButton, &QPushButton::clicked, this, [this]() {
        m_systemLogLabel->setText(QStringLiteral("-"));
    });
    connect(clearAllLogsButton, &QPushButton::clicked, this, [this]() {
        m_systemLogLabel->setText(QStringLiteral("-"));
        for (QTextEdit *stationLog : m_stationLogTexts) {
            stationLog->clear();
        }
    });

    mainLayout->addWidget(controlGroup);
    mainLayout->addWidget(stationContainer, 0);
    mainLayout->addWidget(logGroup, 1);
}

QGroupBox *DualProductionWidget::createStationGroup(int stationIndex, StationPanel *panel)
{
    const bool compactLayout = useCompactProductionLayout();
    panel->group = new QGroupBox(QStringLiteral("工位%1").arg(stationIndex + 1), this);
    panel->group->setMinimumHeight(compactLayout ? 195 : 235);
    panel->group->setMaximumHeight(compactLayout ? 215 : 285);
    QGridLayout *layout = new QGridLayout(panel->group);
    layout->setContentsMargins(8, compactLayout ? 3 : 7, 8, compactLayout ? 3 : 7);
    layout->setHorizontalSpacing(8);
    layout->setVerticalSpacing(compactLayout ? 1 : 4);
    panel->deviceStatus = new QLabel(QStringLiteral("未启动"), panel->group);
    panel->deviceStatus->setStyleSheet(QStringLiteral("font-weight:600;color:#374151;"));
    panel->resultBanner = new QLabel(QStringLiteral("待扫码"), panel->group);
    panel->resultBanner->setAlignment(Qt::AlignCenter);
    panel->resultBanner->setMinimumHeight(compactLayout ? 34 : 42);
    panel->resultBanner->setMaximumHeight(compactLayout ? 34 : 42);
    QFont bannerFont = panel->resultBanner->font();
    bannerFont.setPointSize(compactLayout ? 15 : 17);
    bannerFont.setBold(true);
    panel->resultBanner->setFont(bannerFont);
    panel->progressBar = new QProgressBar(panel->group);
    panel->progressBar->setRange(0, TotalSamples);
    panel->progressBar->setMinimumHeight(compactLayout ? 15 : 18);
    panel->progressBar->setMaximumHeight(compactLayout ? 15 : 18);
    panel->snValue = new QLabel(QStringLiteral("-"), panel->group);
    panel->phaseValue = new QLabel(QStringLiteral("待扫码"), panel->group);
    panel->progressValue = new QLabel(QStringLiteral("0 / 100"), panel->group);
    panel->rateValue = new QLabel(QStringLiteral("0.00%"), panel->group);
    panel->failureCountValue = new QLabel(QStringLiteral("0"), panel->group);
    panel->outcomeReasonLabel = new QLabel(QStringLiteral("失败原因"), panel->group);
    panel->failureReasonValue = new QLabel(QStringLiteral("-"), panel->group);
    panel->failureReasonValue->setWordWrap(!compactLayout);
    panel->failureReasonValue->setMinimumHeight(compactLayout ? 16 : 20);
    panel->phaseValue->setStyleSheet(QStringLiteral("font-weight:600;"));
    panel->rateValue->setStyleSheet(QStringLiteral("font-weight:600;"));
    panel->hardwareVersionValue = new QLabel(QStringLiteral("等待广播"), panel->group);
    panel->softwareVersionValue = new QLabel(QStringLiteral("等待广播"), panel->group);
    panel->materialVersionValue = new QLabel(QStringLiteral("等待广播"), panel->group);
    panel->deviceIdValue = new QLabel(QStringLiteral("等待广播"), panel->group);
    panel->deviceIdValue->setTextInteractionFlags(Qt::TextSelectableByMouse);
    panel->deviceIdValue->setWordWrap(true);
    panel->stopButton = new QPushButton(QStringLiteral("停止"), panel->group);
    panel->clearButton = new QPushButton(QStringLiteral("清空"), panel->group);
    const int actionButtonWidth = qMax(panel->stopButton->sizeHint().width(),
                                       panel->clearButton->sizeHint().width());
    panel->stopButton->setFixedWidth(actionButtonWidth);
    panel->clearButton->setFixedWidth(actionButtonWidth);

    layout->addWidget(panel->deviceStatus, 0, 0, 1, 4);
    layout->addWidget(panel->resultBanner, 1, 0, 1, 4);
    layout->addWidget(panel->progressBar, 2, 0, 1, 4);
    layout->addWidget(new QLabel(QStringLiteral("SN"), panel->group), 3, 0);
    layout->addWidget(panel->snValue, 3, 1, 1, 3);
    layout->addWidget(new QLabel(QStringLiteral("状态"), panel->group), 4, 0);
    layout->addWidget(panel->phaseValue, 4, 1);
    layout->addWidget(new QLabel(QStringLiteral("进度"), panel->group), 4, 2);
    layout->addWidget(panel->progressValue, 4, 3);
    layout->addWidget(new QLabel(QStringLiteral("成功率"), panel->group), 5, 0);
    layout->addWidget(panel->rateValue, 5, 1);
    layout->addWidget(new QLabel(QStringLiteral("失败次数"), panel->group), 5, 2);
    layout->addWidget(panel->failureCountValue, 5, 3);
    layout->addWidget(panel->outcomeReasonLabel, 6, 0);
    layout->addWidget(panel->failureReasonValue, 6, 1, 1, 3);
    QWidget *versionRow = new QWidget(panel->group);
    QHBoxLayout *versionLayout = new QHBoxLayout(versionRow);
    versionLayout->setContentsMargins(0, 0, 0, 0);
    versionLayout->setSpacing(compactLayout ? 4 : 8);
    versionLayout->addWidget(new QLabel(QStringLiteral("当前硬件"), versionRow));
    versionLayout->addWidget(panel->hardwareVersionValue, 1);
    QLabel *softwareLabel = new QLabel(QStringLiteral("当前软件"), versionRow);
    softwareLabel->setObjectName(QStringLiteral("productionSoftwareLabel"));
    versionLayout->addWidget(softwareLabel);
    versionLayout->addWidget(panel->softwareVersionValue, 1);
    QLabel *materialLabel = new QLabel(QStringLiteral("当前物料"), versionRow);
    materialLabel->setObjectName(QStringLiteral("productionMaterialLabel"));
    versionLayout->addWidget(materialLabel);
    versionLayout->addWidget(panel->materialVersionValue, 1);
    layout->addWidget(versionRow, 7, 0, 1, 4);
    layout->addWidget(new QLabel(QStringLiteral("当前ID"), panel->group), 8, 0);
    layout->addWidget(panel->deviceIdValue, 8, 1);
    layout->addWidget(panel->stopButton, 8, 2);
    layout->addWidget(panel->clearButton, 8, 3);
    layout->setColumnStretch(1, 1);
    layout->setColumnStretch(3, 1);
    return panel->group;
}

void DualProductionWidget::connectStation(int stationIndex)
{
    ProductionStationController *station = m_stations[stationIndex];
    connect(station, &ProductionStationController::qingjuRfrSoftwareVersionUpdated,
            this, [this, stationIndex](const QString &version) {
        if (m_protocolMode != 1) {
            return;
        }
        QLabel *value = m_panels[stationIndex].materialVersionValue;
        value->setText(version.section(QChar(0xFF08), 0, 0));
        value->setToolTip(version);
    });
    connect(station, &ProductionStationController::deviceStateChanged,
            this, [this, stationIndex]() {
        if (!m_stations[stationIndex]->isDeviceReady()) {
            resetStationReadback(stationIndex);
        }
        updateUiState();
        emit activityChanged();
    });
    connect(station, &ProductionStationController::frameObserved,
            this, [this, stationIndex](const CanFrame &frame) {
        handleStationFrame(stationIndex, frame);
    });
    connect(station, &ProductionStationController::qingjuDeviceInfoUpdated,
            this, [this, stationIndex](const QString &softwareVersion,
                                      const QString &hardwareVersion,
                                      const QString &deviceSn) {
        if (m_protocolMode != 1) {
            return;
        }
        if (!softwareVersion.trimmed().isEmpty()) {
            const QString fullSoftwareVersion = softwareVersion.trimmed();
            m_panels[stationIndex].softwareVersionValue->setText(
                fullSoftwareVersion.section(QChar(0xFF08), 0, 0));
            m_panels[stationIndex].softwareVersionValue->setToolTip(fullSoftwareVersion);
        }
        if (!hardwareVersion.trimmed().isEmpty()) {
            m_panels[stationIndex].hardwareVersionValue->setText(hardwareVersion.trimmed());
        }
        if (!deviceSn.trimmed().isEmpty()) {
            m_panels[stationIndex].deviceIdValue->setText(deviceSn.trimmed());
        }
    });
    connect(station, &ProductionStationController::testStateChanged,
            this, [this, stationIndex](const ProductionTestState &state) {
        updateStationPanel(stationIndex, state);
        updateUiState();
        emit activityChanged();
    });
    connect(station, &ProductionStationController::logMessage,
            this, [this, stationIndex](const QString &message) {
        appendLog(stationIndex, message);
    });
    connect(station, &ProductionStationController::finished,
            this, [this, stationIndex](bool, const ProductionTestState &state) {
        saveProductionResult(stationIndex, state);
        updateUiState();
        emit activityChanged();
    });
    connect(m_panels[stationIndex].stopButton, &QPushButton::clicked,
            this, [this, stationIndex]() {
        m_stations[stationIndex]->stopTest(QStringLiteral("操作员停止"));
    });
    connect(m_panels[stationIndex].clearButton, &QPushButton::clicked,
            this, [this, stationIndex]() {
        m_stations[stationIndex]->resetTest();
        resetStationReadback(stationIndex);
        setPreferredStation(stationIndex);
        updateStationPanel(stationIndex, m_stations[stationIndex]->state());
        updateUiState();
    });
}

void DualProductionWidget::startStationOneDevice()
{
    if (m_mainCanBusy) {
        QMessageBox::warning(this, QStringLiteral("无法启动"),
                             QStringLiteral("请先关闭主界面的CAN设备。"));
        return;
    }
    const int firstIndex = m_deviceIndexSpins[0]->value();
    m_stations[0]->setProtocolMode(m_protocolMode);
    QString error;
    if (!m_stations[0]->startDevice(
            m_deviceType, firstIndex, ProductionCanChannel, m_resistanceEnabled, &error)) {
        QMessageBox::critical(this, QStringLiteral("工位1启动失败"), error);
        updateUiState();
        emit activityChanged();
        return;
    }
    appendLog(-1, QStringLiteral("单工位模式已启动：工位1=设备%1/CAN0（未访问设备1）")
                     .arg(firstIndex));
    setPreferredStation(0);
    updateUiState();
    emit activityChanged();
}

void DualProductionWidget::startDevices()
{
    if (m_mainCanBusy) {
        QMessageBox::warning(this, QStringLiteral("无法启动"),
                             QStringLiteral("请先关闭主界面的CAN设备。"));
        return;
    }
    const int firstIndex = m_deviceIndexSpins[0]->value();
    const int secondIndex = m_deviceIndexSpins[1]->value();
    if (firstIndex == secondIndex) {
        QMessageBox::warning(this, QStringLiteral("设备索引重复"),
                             QStringLiteral("两个工位必须选择不同的设备索引。"));
        return;
    }

    QString firstError;
    QString secondError;
    for (ProductionStationController *station : m_stations) {
        station->setProtocolMode(m_protocolMode);
    }
    const bool firstStarted = m_stations[0]->startDevice(
        m_deviceType, firstIndex, ProductionCanChannel, m_resistanceEnabled, &firstError);
    const bool secondStarted = m_stations[1]->startDevice(
        m_deviceType, secondIndex, ProductionCanChannel, m_resistanceEnabled, &secondError);
    if (!firstStarted || !secondStarted) {
        for (ProductionStationController *station : m_stations) {
            if (station->isDeviceReady()) {
                station->stopDevice();
            }
        }
        QMessageBox::critical(
            this,
            QStringLiteral("双工位启动失败"),
            QStringLiteral("工位1：%1\n工位2：%2\n\n只有一个CAN设备时，请点击左侧“启动产线设备”。")
                .arg(firstStarted ? QStringLiteral("已启动") : firstError,
                     secondStarted ? QStringLiteral("已启动") : secondError));
        updateUiState();
        emit activityChanged();
        return;
    }
    appendLog(-1, QStringLiteral("双工位设备已启动：设备%1/CAN0、设备%2/CAN0")
                     .arg(firstIndex)
                     .arg(secondIndex));
    setPreferredStation(0);
    updateUiState();
    emit activityChanged();
}

void DualProductionWidget::stopDevices(bool showConfirmation)
{
    if (showConfirmation && isAnyStationRunning()) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this,
            QStringLiteral("关闭双工位"),
            QStringLiteral("检测正在进行，确定停止并关闭两个CAN设备吗？"));
        if (answer != QMessageBox::Yes) {
            return;
        }
    }
    for (ProductionStationController *station : m_stations) {
        station->stopDevice();
    }
    appendLog(-1, QStringLiteral("双工位CAN设备已关闭"));
    updateUiState();
    emit activityChanged();
}

void DualProductionWidget::toggleParameterLock()
{
    const bool locked = m_protocolMode == 1 ? m_qingjuLocked : m_meituanLocked;
    if (locked) {
        if (m_protocolMode == 1) {
            m_qingjuLocked = false;
        } else {
            m_meituanLocked = false;
        }
        m_hwVersionEdit->setReadOnly(false);
        m_materialChangeEdit->setReadOnly(false);
        updateUiState();
        emit configChanged();
        return;
    }

    QString error;
    if (!validateParameters(&error)) {
        QMessageBox::warning(this, QStringLiteral("参数错误"), error);
        return;
    }
    if (m_protocolMode == 1) {
        m_qingjuLocked = true;
        m_qingjuHwVersion = m_hwVersionEdit->text().trimmed();
    } else {
        m_meituanLocked = true;
        m_meituanHwVersion = m_hwVersionEdit->text().trimmed();
        m_materialChange = m_materialChangeEdit->text().trimmed();
    }
    updateUiState();
    m_scanEdit->setFocus();
    emit configChanged();
}

void DualProductionWidget::processScan(const QString &text)
{
    const QString sn = text.trimmed().toUpper();
    m_scanBuffer.clear();
    m_scanTimer->stop();
    m_scanEdit->clear();
    if (sn.isEmpty()) {
        return;
    }
    QString error;
    if (!ProductionTestService::validateSn(sn, m_protocolMode, &error)) {
        appendLog(-1, QStringLiteral("扫码无效：%1").arg(error));
        return;
    }
    if (!devicesActive()) {
        QMessageBox::warning(this, QStringLiteral("设备未启动"),
                             QStringLiteral("请先启动至少一个工位的CAN设备。"));
        return;
    }
    if (!validateParameters(&error) ||
        !(m_protocolMode == 1 ? m_qingjuLocked : m_meituanLocked)) {
        QMessageBox::warning(this, QStringLiteral("参数未确认"),
                             error.isEmpty() ? QStringLiteral("请先确认生产参数。") : error);
        return;
    }
    for (int stationIndex = 0; stationIndex < StationCount; ++stationIndex) {
        if (m_stations[stationIndex]->isRunning() &&
            m_stations[stationIndex]->state().sn.compare(sn, Qt::CaseInsensitive) == 0) {
            appendLog(-1, QStringLiteral("拒绝重复SN：%1正在工位%2检测")
                              .arg(sn)
                              .arg(stationIndex + 1));
            return;
        }
    }
    const int stationIndex = selectAvailableStation();
    if (stationIndex < 0) {
        QString reason = QStringLiteral("无空闲工位");
        if (!m_stations[0]->isDeviceReady() || !m_stations[1]->isDeviceReady()) {
            reason = QStringLiteral("另一工位CAN设备未启动");
        }
        appendLog(-1, QStringLiteral("扫码已接收但未分配：SN=%1，原因=%2")
                          .arg(sn, reason));
        m_nextStationLabel->setText(QStringLiteral("扫码未分配：%1").arg(reason));
        m_nextStationLabel->setStyleSheet(
            QStringLiteral("background:#FEE2E2;color:#991B1B;border-radius:5px;padding:6px;"));
        return;
    }
    appendLog(-1, QStringLiteral("扫码已接收：SN=%1 → 工位%2")
                      .arg(sn)
                      .arg(stationIndex + 1));
    startStationTest(stationIndex, sn);
}

void DualProductionWidget::startStationTest(int stationIndex, const QString &sn)
{
    // 新工件开始前清除上一台终端的广播显示，避免早期失败结果携带旧设备信息。
    resetStationReadback(stationIndex);
    ProductionTestConfig config;
    config.totalSamples = TotalSamples;
    config.passRateThreshold = m_passThresholdSpin->value();
    const QString materialChange =
        m_protocolMode == 0 ? m_materialChangeEdit->text().trimmed() : QString();
    QString error;
    if (!m_stations[stationIndex]->startTest(
            m_protocolMode,
            sn,
            m_hwVersionEdit->text().trimmed(),
            materialChange,
            config,
            &error)) {
        QMessageBox::warning(this, QStringLiteral("无法开始检测"), error);
        return;
    }
    if (m_stations[stationIndex]->isRunning()) {
        appendLog(stationIndex, QStringLiteral("开始检测，SN=%1").arg(sn));
    }
    setPreferredStation(stationIndex == 0 ? 1 : 0);
    updateUiState();
    if (m_scanEdit->isEnabled()) {
        m_scanEdit->setFocus(Qt::OtherFocusReason);
    }
    emit activityChanged();
}

int DualProductionWidget::selectAvailableStation() const
{
    if (m_stations[m_preferredStation]->isDeviceReady() &&
        !m_stations[m_preferredStation]->isRunning()) {
        return m_preferredStation;
    }
    const int otherStation = m_preferredStation == 0 ? 1 : 0;
    return m_stations[otherStation]->isDeviceReady() &&
                   !m_stations[otherStation]->isRunning()
               ? otherStation
               : -1;
}

void DualProductionWidget::setPreferredStation(int stationIndex)
{
    if (stationIndex < 0 || stationIndex >= StationCount) {
        return;
    }
    m_preferredStation = stationIndex;
    updateUiState();
}

void DualProductionWidget::updateUiState()
{
    const bool active = devicesActive();
    const bool anyReady = devicesActive();
    const bool parametersLocked = m_protocolMode == 1 ? m_qingjuLocked : m_meituanLocked;
    m_protocolLabel->setText(QStringLiteral("当前协议：%1（两工位一致）")
                                 .arg(protocolName(m_protocolMode)));
    m_startDevicesButton->setEnabled(!active && !m_mainCanBusy);
    m_stopDevicesButton->setEnabled(active);
    for (QSpinBox *deviceSpin : m_deviceIndexSpins) {
        deviceSpin->setEnabled(!active);
    }
    m_hwVersionEdit->setReadOnly(parametersLocked);
    m_materialChangeEdit->setReadOnly(parametersLocked);
    m_materialChangeLabel->setVisible(m_protocolMode == 0);
    m_materialChangeEdit->setVisible(m_protocolMode == 0);
    m_parameterLockButton->setText(parametersLocked
                                       ? QStringLiteral("修改参数")
                                       : QStringLiteral("确定参数"));
    m_parameterLockButton->setEnabled(!isAnyStationRunning());
    m_scanEdit->setEnabled(parametersLocked && anyReady);
    m_startTestButton->setEnabled(parametersLocked && anyReady);
    m_passThresholdSpin->setEnabled(!isAnyStationRunning());

    const int availableStation = selectAvailableStation();
    if (!anyReady) {
        m_nextStationLabel->setText(QStringLiteral("请先启动产线设备"));
        m_nextStationLabel->setStyleSheet(
            QStringLiteral("background:#E5E7EB;color:#374151;border-radius:5px;padding:6px;"));
    } else if (availableStation < 0) {
        m_nextStationLabel->setText(QStringLiteral("已启动工位检测中"));
        m_nextStationLabel->setStyleSheet(
            QStringLiteral("background:#FEF3C7;color:#92400E;border-radius:5px;padding:6px;"));
    } else {
        m_nextStationLabel->setText(QStringLiteral("下一扫码 → 工位%1  [F%1]")
                                        .arg(availableStation + 1));
        m_nextStationLabel->setStyleSheet(
            QStringLiteral("background:#DBEAFE;color:#1D4ED8;border-radius:5px;padding:6px;"));
    }

    for (int stationIndex = 0; stationIndex < StationCount; ++stationIndex) {
        m_panels[stationIndex].deviceStatus->setText(
            m_stations[stationIndex]->deviceStatusText());
        m_panels[stationIndex].stopButton->setEnabled(m_stations[stationIndex]->isRunning());
        m_panels[stationIndex].clearButton->setEnabled(!m_stations[stationIndex]->isRunning());
    }
}

void DualProductionWidget::updateStationPanel(int stationIndex,
                                              const ProductionTestState &state)
{
    StationPanel &panel = m_panels[stationIndex];
    const QString normalizedResult = state.resultText.trimmed().toUpper();
    const bool passed = normalizedResult == QStringLiteral("PASS") ||
                        state.resultText.contains(QStringLiteral("通过"));
    const bool stopped = normalizedResult == QStringLiteral("STOPPED") ||
                         state.resultText.contains(QStringLiteral("停止"));
    panel.snValue->setText(state.sn.isEmpty() ? QStringLiteral("-") : state.sn);
    panel.phaseValue->setText(
        state.phaseText.isEmpty() ? QStringLiteral("待扫码") : state.phaseText);
    panel.progressBar->setRange(0, qMax(1, state.totalSamples));
    panel.progressBar->setValue(state.completedSamples);
    panel.progressValue->setText(
        QStringLiteral("%1 / %2").arg(state.completedSamples).arg(state.totalSamples));
    panel.rateValue->setText(QStringLiteral("%1%").arg(state.successRate, 0, 'f', 2));
    panel.failureCountValue->setText(QString::number(state.failureCount));
    panel.outcomeReasonLabel->setText(
        passed ? QStringLiteral("通过说明")
               : (stopped ? QStringLiteral("停止原因") : QStringLiteral("失败原因")));
    panel.failureReasonValue->setText(state.lastFailureReason.isEmpty()
                                          ? QStringLiteral("-")
                                          : state.lastFailureReason);

    QString bannerText = QStringLiteral("待扫码");
    QString bannerStyle =
        QStringLiteral("background:#E5E7EB;color:#374151;border-radius:6px;");
    if (state.running) {
        bannerText = QStringLiteral("检测中");
        bannerStyle =
            QStringLiteral("background:#DBEAFE;color:#1D4ED8;border-radius:6px;");
    } else if (!state.resultText.isEmpty() && state.resultText != QStringLiteral("-")) {
        bannerText = state.resultText;
        if (passed) {
            bannerStyle =
                QStringLiteral("background:#DCFCE7;color:#166534;border-radius:6px;");
        } else if (stopped) {
            bannerStyle =
                QStringLiteral("background:#FEF3C7;color:#92400E;border-radius:6px;");
        } else {
            bannerStyle =
                QStringLiteral("background:#FEE2E2;color:#991B1B;border-radius:6px;");
        }
    }
    panel.resultBanner->setText(bannerText);
    panel.resultBanner->setStyleSheet(bannerStyle);
}

void DualProductionWidget::handleStationFrame(int stationIndex, const CanFrame &frame)
{
    if (stationIndex < 0 || stationIndex >= StationCount ||
        m_protocolMode != 0 ||
        frame.direction != CanFrameDirection::Rx ||
        frame.protocol != CanFrameProtocol::ClassicCan ||
        frame.data.size() != RfidProtocol::ClassicCanDlc) {
        return;
    }

    StationPanel &panel = m_panels[stationIndex];
    if (frame.id == RfidProtocol::VersionFrameId) {
        const RfidVersion version = RfidProtocol::parseVersionFrame(frame.data);
        if (!version.valid) {
            return;
        }
        const int major = (version.hardwareVersion >> 8) & 0xFF;
        const int minor = version.hardwareVersion & 0xFF;
        const QString hardwareHex =
            QStringLiteral("%1").arg(version.hardwareVersion, 4, 16, QChar('0')).toUpper();
        panel.hardwareVersionValue->setText(
            QStringLiteral("%1.0.%2（0x%3）")
                .arg(major)
                .arg(minor)
                .arg(hardwareHex));
        const int softwareMajor = (version.softwareVersion >> 8) & 0xFF;
        const int softwareMinor = version.softwareVersion & 0xFF;
        const QString softwareHex =
            QStringLiteral("%1").arg(version.softwareVersion, 4, 16, QChar('0')).toUpper();
        const QString fullSoftwareVersion =
            QStringLiteral("v%1.%2（0x%3）")
                .arg(softwareMajor, 2, 16, QChar('0'))
                .arg(softwareMinor, 2, 16, QChar('0'))
                .arg(softwareHex);
        panel.softwareVersionValue->setText(
            fullSoftwareVersion.section(QChar(0xFF08), 0, 0));
        panel.softwareVersionValue->setToolTip(fullSoftwareVersion);
        panel.materialVersionValue->setText(
            QStringLiteral("0x%1")
                .arg(QStringLiteral("%1")
                         .arg(version.materialRecord, 4, 16, QChar('0'))
                         .toUpper()));
        return;
    }

    int partIndex = -1;
    if (frame.id == RfidProtocol::DeviceIdPart1FrameId) {
        partIndex = 0;
    } else if (frame.id == RfidProtocol::DeviceIdPart2FrameId) {
        partIndex = 1;
    }
    if (partIndex < 0) {
        return;
    }

    for (const char character : frame.data) {
        const quint8 value = static_cast<quint8>(character);
        if (value < 0x20 || value > 0x7E) {
            m_deviceIdParts[stationIndex][partIndex].clear();
            panel.deviceIdValue->setText(QStringLiteral("ID广播包含非ASCII数据"));
            return;
        }
    }
    m_deviceIdParts[stationIndex][partIndex] = QString::fromLatin1(frame.data);
    const QString deviceId = m_deviceIdParts[stationIndex][0] +
                             m_deviceIdParts[stationIndex][1];
    panel.deviceIdValue->setText(
        deviceId.size() == 16 ? deviceId : QStringLiteral("等待完整0x2C4/0x2C5"));
}

void DualProductionWidget::resetStationReadback(int stationIndex)
{
    if (stationIndex < 0 || stationIndex >= StationCount) {
        return;
    }
    m_deviceIdParts[stationIndex][0].clear();
    m_deviceIdParts[stationIndex][1].clear();
    StationPanel &panel = m_panels[stationIndex];
    panel.softwareVersionValue->setToolTip(QString());
    panel.materialVersionValue->setToolTip(QString());
    panel.group->findChild<QLabel *>(QStringLiteral("productionSoftwareLabel"))->setText(
        m_protocolMode == 1 ? QStringLiteral("NPK软件") : QStringLiteral("当前软件"));
    panel.group->findChild<QLabel *>(QStringLiteral("productionMaterialLabel"))->setText(
        m_protocolMode == 1 ? QStringLiteral("RFR软件") : QStringLiteral("当前物料"));
    if (m_protocolMode == 0) {
        panel.hardwareVersionValue->setText(QStringLiteral("等待广播"));
        panel.softwareVersionValue->setText(QStringLiteral("等待0x2C3"));
        panel.materialVersionValue->setText(QStringLiteral("等待广播"));
        panel.deviceIdValue->setText(QStringLiteral("等待广播"));
    } else {
        panel.hardwareVersionValue->setText(QStringLiteral("等待读取0xA004"));
        panel.softwareVersionValue->setText(QStringLiteral("等待读取0xA002"));
        panel.materialVersionValue->setText(QStringLiteral("等待读取"));
        panel.deviceIdValue->setText(QStringLiteral("等待读取SN"));
    }
}

void DualProductionWidget::appendLog(int stationIndex, const QString &message)
{
    const QString displayLine = QStringLiteral("%1 %2")
                                    .arg(QDateTime::currentDateTime().toString("HH:mm:ss"),
                                         message);
    if (stationIndex >= 0 && stationIndex < StationCount) {
        m_stationLogTexts[stationIndex]->append(displayLine);
    } else {
        m_systemLogLabel->setText(displayLine);
    }

    const ProductionTestState state = stationIndex >= 0 && stationIndex < StationCount
                                          ? m_stations[stationIndex]->state()
                                          : ProductionTestState();
    const quint32 deviceIndex = stationIndex >= 0 && stationIndex < StationCount
                                    ? m_stations[stationIndex]->deviceIndex()
                                    : 0;
    m_productionLogService.appendEvent(
        m_protocolMode,
        stationIndex >= 0 ? stationIndex + 1 : 0,
        deviceIndex,
        state,
        productionEventLevel(message),
        message);
}

void DualProductionWidget::updateProductionStats()
{
    if (m_statsDateValue == nullptr) {
        return;
    }
    const ProductionDailyStats stats = m_productionLogService.dailyStats();
    m_statsDateValue->setText(stats.date.toString(QStringLiteral("yyyy-MM-dd")));
    m_statsTotalValue->setText(QString::number(stats.total));
    m_statsPassedValue->setText(QString::number(stats.passed));
    m_statsFailedValue->setText(QString::number(stats.failed));
    m_statsStoppedValue->setText(QString::number(stats.stopped));
    m_statsRateValue->setText(QStringLiteral("%1%")
                                  .arg(stats.passRate(), 0, 'f', 2));
    m_openLogDirectoryButton->setEnabled(
        !m_productionLogService.productionDirectory().isEmpty());
}

void DualProductionWidget::saveProductionResult(int stationIndex,
                                                const ProductionTestState &state)
{
    if (stationIndex < 0 || stationIndex >= StationCount) {
        return;
    }
    const StationPanel &panel = m_panels[stationIndex];
    const QString readHardware = m_protocolMode == 0
                                     ? persistedReadbackValue(panel.hardwareVersionValue)
                                     : QString();
    const QString readMaterial = m_protocolMode == 0
                                     ? persistedReadbackValue(panel.materialVersionValue)
                                     : QString();
    const QString readDeviceId = m_protocolMode == 0
                                     ? persistedReadbackValue(panel.deviceIdValue)
                                     : QString();
    if (!m_productionLogService.appendResult(
            m_protocolMode,
            stationIndex + 1,
            m_stations[stationIndex]->deviceIndex(),
            state,
            readHardware,
            readMaterial,
            readDeviceId)) {
        m_stationLogTexts[stationIndex]->append(
            QStringLiteral("%1 [日志] 产线结果保存失败：%2")
                .arg(QDateTime::currentDateTime().toString("HH:mm:ss"),
                     m_productionLogService.lastError()));
    }
    updateProductionStats();
}

void DualProductionWidget::storeCurrentProtocolSettings()
{
    if (m_protocolMode == 1) {
        m_qingjuHwVersion = m_hwVersionEdit->text().trimmed();
        m_qingjuLocked = m_qingjuLocked && m_hwVersionEdit->isReadOnly();
    } else {
        m_meituanHwVersion = m_hwVersionEdit->text().trimmed();
        m_materialChange = m_materialChangeEdit->text().trimmed();
        m_meituanLocked = m_meituanLocked && m_hwVersionEdit->isReadOnly();
    }
}

void DualProductionWidget::restoreCurrentProtocolSettings()
{
    const bool qingjuMode = m_protocolMode == 1;
    m_hwVersionEdit->setText(qingjuMode ? m_qingjuHwVersion : m_meituanHwVersion);
    m_materialChangeEdit->setText(m_materialChange);
}

bool DualProductionWidget::validateParameters(QString *error) const
{
    if (!ProductionTestService::validateHwVersion(
            m_hwVersionEdit->text().trimmed(), nullptr, error)) {
        return false;
    }
    if (m_protocolMode == 0 &&
        !ProductionTestService::validateMaterialChange(
            m_materialChangeEdit->text().trimmed(), nullptr, error)) {
        return false;
    }
    return true;
}
