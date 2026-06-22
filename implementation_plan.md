# 美团 CAN 协议 7.2 通用应用服务诊断指令实现设计方案

## 1. 需求目标

基于 `F:\TestTools\MT_CAN\协议文件\美团协议\CAN总线.pdf` 第 7.2 章“通用应用服务列表”，为现有 RFID 定位器上位机补充通用诊断服务的下发与响应展示能力。

本次范围限定为事件型 CAN 报文中的通用应用服务：

- `0x10`：BOOT 和 APP 之间跳转
- `0x11`：软件复位
- `0x28`：通信控制，使能/禁止周期广播，下电不保存
- `0x29`：广播报文周期值配置，下电保存
- `0x85`：启用或禁用通信故障诊断，下电不保存
- `0x2E`：写数据到非易失存储区，协议标注为预留

实现目标：

- 在“美团协议”RFID 监控页新增通用诊断控制面板。
- 通过 RFID 事件型请求 ID `0x07` 下发诊断请求。
- 解析 RFID 响应 ID `0x107` 的肯定/否定应答，并在现有“响应”标签展示。
- 保持现有 CAN/RFID 协议分层，不引入跨模块重构。

## 2. 协议依据

### 2.1 CAN 基础规则

来自 `CAN总线.pdf` 第 5.2、5.3、7.1 章：

- CAN 使用标准帧，500 kbit/s。
- 事件型报文采用 ISO15765-2，一问一答，接收者必须应答，应答超时时间为 3s。
- 数据排列使用大端模式，高位在前，低位在后。
- CAN DLC 总是设置为 8，未使用字节填充 `0x55`。
- 接收到 DLC 不是 8 byte 的报文应直接丢弃。
- RFID 读卡器事件型请求 ID 为 `0x07`，响应 ID 为 `0x107`。

### 2.2 ISO15765-2 单帧格式

本次 6 个服务默认按单帧发送：

```text
Byte0: 高 4 位 = 0，表示 Single Frame；低 4 位 = SF_DL
Byte1: SID
Byte2..Byte7: 有效数据或 0x55 填充
```

示例：软件复位请求发送数据为：

```text
01 11 55 55 55 55 55 55
```

### 2.3 通用响应规则

- 肯定响应 SID 为 `Request SID + 0x40`。
- 否定响应 SID 固定为 `0x7F`。
- 否定响应格式为 `7F [Request SID] [NRC]`。

否定响应码：

| NRC | 含义 |
| --- | --- |
| `0x10` | 拒绝执行请求动作 |
| `0x11` | 不支持请求服务 |
| `0x12` | 不支持请求服务参数 |
| `0x13` | 报文长度错误或格式非法 |
| `0x22` | 条件未满足 |
| `0x31` | 请求超出范围 |
| `0x78` | 请求动作未完成，发送方可延时后继续请求 |

## 3. 服务字段定义

### 3.1 `0x10` BOOT 和 APP 跳转

请求：

| 字节 | 字段 | 取值 |
| --- | --- | --- |
| #1 | SID | `0x10` |
| #2 | 跳转程序运行区类型 | `0x01` APP；`0x02` BOOT |

肯定响应：

| 字节 | 字段 | 取值 |
| --- | --- | --- |
| #1 | Positive SID | `0x50` |
| #2 | 跳转程序运行区类型 | 与请求一致 |

否定响应：`7F 10 [NRC]`。

请求帧示例：

```text
跳转 APP : 02 10 01 55 55 55 55 55
跳转 BOOT: 02 10 02 55 55 55 55 55
```

### 3.2 `0x11` 软件复位

请求：

| 字节 | 字段 | 取值 |
| --- | --- | --- |
| #1 | SID | `0x11` |

肯定响应：

| 字节 | 字段 | 取值 |
| --- | --- | --- |
| #1 | Positive SID | `0x51` |

否定响应：`7F 11 [NRC]`。

请求帧示例：

```text
01 11 55 55 55 55 55 55
```

### 3.3 `0x28` 通信控制

用于使能或禁止周期报文发送，下电不保存。

请求：

| 字节 | 字段 | 取值 |
| --- | --- | --- |
| #1 | SID | `0x28` |
| #2 | 周期报文发送控制 | `0x00` 禁止；`0x01` 使能 |

肯定响应：

| 字节 | 字段 | 取值 |
| --- | --- | --- |
| #1 | Positive SID | `0x68` |
| #2 | 周期报文发送控制 | 与请求一致 |

否定响应：`7F 28 [NRC]`。

请求帧示例：

```text
使能周期发送: 02 28 01 55 55 55 55 55
禁止周期发送: 02 28 00 55 55 55 55 55
```

### 3.4 `0x29` 广播报文周期值配置

用于配置指定 CAN 广播 ID 的发送周期，掉电不丢失，下次上电使用配置值。

请求：

| 字节 | 字段 | 取值 |
| --- | --- | --- |
| #1 | SID | `0x29` |
| #2-#3 | CAN 报文 ID | 大端 |
| #4-#5 | CAN 报文 ID 的周期 | 大端，单位 ms |

周期范围：

- `0x0010~0xFFFE`：按指定 ms 周期发送。
- `0xFFFF`：不发送。

肯定响应：

| 字节 | 字段 | 取值 |
| --- | --- | --- |
| #1 | Positive SID | `0x69` |
| #2-#3 | CAN 报文 ID | 与请求一致 |
| #4-#5 | CAN 报文 ID 的周期 | 与请求一致 |

否定响应：`7F 29 [NRC]`。

请求帧示例：

```text
设置 0x2C0 周期为 100 ms: 05 29 02 C0 00 64 55 55
设置 0x2C0 不发送      : 05 29 02 C0 FF FF 55 55
```

### 3.5 `0x85` 启用或禁用通信故障诊断

用于启用或禁用零部件检测通信故障功能，下电不保存。

请求：

| 字节 | 字段 | 取值 |
| --- | --- | --- |
| #1 | SID | `0x85` |
| #2 | 通信故障诊断控制 | `0x00` 禁用；`0x01` 启用 |

肯定响应：

| 字节 | 字段 | 取值 |
| --- | --- | --- |
| #1 | Positive SID | `0xC5` |
| #2 | 通信故障诊断控制 | 与请求一致 |

否定响应：`7F 85 [NRC]`。

请求帧示例：

```text
启用通信故障诊断: 02 85 01 55 55 55 55 55
禁用通信故障诊断: 02 85 00 55 55 55 55 55
```

### 3.6 `0x2E` 写数据到非易失存储区

协议标注为预留。实现时建议默认隐藏或加二次确认，避免误写设备持久化配置。

请求：

| 字节 | 字段 | 取值 |
| --- | --- | --- |
| #1 | SID | `0x2E` |
| #2-#3 | 数据标识 DID | 大端 |
| #4-#? | 数据 | 长度依据 DID 确定 |

肯定响应：

| 字节 | 字段 | 取值 |
| --- | --- | --- |
| #1 | Positive SID | `0x6E` |
| #2-#3 | 数据标识 DID | 与请求一致 |

否定响应：`7F 2E [NRC]`。

单帧限制：

```text
SF_DL = 3 + data.size()
SF_DL <= 7
data.size() <= 4
```

请求帧示例：

```text
DID 0x1234 写入 0xABCD: 05 2E 12 34 AB CD 55 55
```

## 4. 影响范围

### 4.1 修改文件

- `CAN_RFID_Qt/rfidprotocol.h`
- `CAN_RFID_Qt/rfidprotocol.cpp`
- `CAN_RFID_Qt/mainwindow.h`
- `CAN_RFID_Qt/mainwindow.cpp`

### 4.2 不修改范围

- 不修改 OTA 协议实现。
- 不修改 RS485 线程模型。
- 不修改 CAN 底层发送接口。
- 不改认证、密钥、生产配置。
- 不引入数据库或外部依赖。

## 5. 设计方案

### 5.0 当前代码现状

基于 `qingju` 分支当前实现：

- `RfidProtocol::buildControlFrame()` 发送 `0x207` 控制帧，不属于本次 7.2 事件型诊断服务。
- `RfidProtocol::buildSetScanPeriodFrame()` 和 `buildRestartFrame()` 已经在用 `0x07` 请求 ID 发送类似事件型请求，但当前实现不是完整通用诊断面板。
- `RfidProtocol::parseResponseFrame()` 当前只把 `0x41/0x42` 识别为肯定响应，无法识别 `0x50/0x51/0x68/0x69/0xC5/0x6E`。
- `MainWindow::createRfidMonitorTab()` 已有“控制”“TAG”“状态信息”“设备信息”四个区域，新增面板应放在“控制”附近，避免影响状态展示。
- `MainWindow::sendRfidFrame()` 已负责 CAN 发送、错误日志和实时日志入表，本次继续复用。

### 5.1 协议编解码层

在 `RfidProtocol` 中新增单帧构造能力。

建议新增私有辅助函数：

```cpp
static QByteArray buildSingleFrame(const QByteArray &serviceData);
```

规则：

- `serviceData.size()` 范围为 `1..7`。
- Byte0 写入 `serviceData.size()`，即 SF 类型为 0、`SF_DL` 为有效数据长度。
- Byte1 起复制 `serviceData`。
- 剩余字节使用现有 `FillByte = 0x55` 填充。

新增公开构造函数：

```cpp
static QByteArray buildBootAppJumpFrame(quint8 targetMode);
static QByteArray buildSoftwareResetFrame();
static QByteArray buildCommunicationControlFrame(bool enableBroadcast);
static QByteArray buildSetBroadcastPeriodFrame(quint16 canId, quint16 periodMs);
static QByteArray buildCommunicationDiagnosticFrame(bool enableDiag);
static QByteArray buildWriteNonVolatileFrame(quint16 dataId, const QByteArray &data);
```

字段编码：

| 函数 | serviceData |
| --- | --- |
| `buildBootAppJumpFrame` | `10 [01/02]` |
| `buildSoftwareResetFrame` | `11` |
| `buildCommunicationControlFrame` | `28 [00/01]` |
| `buildSetBroadcastPeriodFrame` | `29 [canId_H] [canId_L] [period_H] [period_L]` |
| `buildCommunicationDiagnosticFrame` | `85 [00/01]` |
| `buildWriteNonVolatileFrame` | `2E [did_H] [did_L] [data...]` |

建议实现细节：

```cpp
QByteArray RfidProtocol::buildSingleFrame(const QByteArray &serviceData)
{
    QByteArray payload = buildFilledFrame();
    if (serviceData.isEmpty() || serviceData.size() > 7) {
        return payload;
    }

    payload[0] = static_cast<char>(serviceData.size());
    for (int index = 0; index < serviceData.size(); ++index) {
        payload[index + 1] = serviceData[index];
    }
    return payload;
}
```

说明：

- 该辅助函数只负责 ISO15765-2 单帧包装，不做业务参数合法性弹窗。
- 业务参数范围优先在 UI 层校验，协议层仍需对 `0x2E` 数据长度做保护。
- 如果 `buildWriteNonVolatileFrame()` 收到超过 4 byte 的数据，建议返回空 `QByteArray()` 或 `buildFilledFrame()` 前由调用方拒绝。推荐 UI 先拒绝，协议层再兜底返回空数组，避免误发无效写入帧。

### 5.2 响应解析

修改 `parseResponseFrame`：

- 校验 Classic CAN DLC 为 8。
- 校验 Byte0 为 ISO15765-2 单帧：`payload[0] >> 4 == 0`。
- 读取 `SF_DL = payload[0] & 0x0F`，要求 `1 <= SF_DL <= 7`。
- `payload[1]` 为响应 SID。
- `0x7F` 按否定响应解析，要求 `SF_DL >= 3`。
- 非 `0x7F` 且 `0x40 <= SID <= 0xEF` 按肯定响应解析。

注意：`0x85` 的肯定响应是 `0xC5`，超出旧逻辑 `0x41~0x7E` 的范围，所以不能只按 7.2 总表中的 `0x41-0x7E` 判断，应按“请求 SID + 0x40”的通用规则放宽到 `0x40~0xEF`，并排除 `0x7F`。

可选增强：

- 在 `RfidResponse` 中增加 `QByteArray data`，保存响应 SID 后面的参数，用于展示 `Positive SID=0x69 data=02 C0 00 64`。
- 如果保持最小修改，可以暂不增加字段，只显示现有 `Positive SID=0xXX`。

推荐本次实现采用“轻量增强”：给 `RfidResponse` 增加 `QByteArray data`，但不改变其他业务流程。

结构体建议：

```cpp
struct RfidResponse
{
    quint8 sid = 0;
    quint8 originalSid = 0;
    quint8 negativeCode = 0;
    QByteArray data;
    bool positive = false;
    bool negative = false;
    bool valid = false;
};
```

解析逻辑建议：

```cpp
const quint8 frameType = static_cast<quint8>(payload[0]) >> 4;
const quint8 serviceLength = static_cast<quint8>(payload[0]) & 0x0F;
if (frameType != 0x00 || serviceLength < 1 || serviceLength > 7) {
    return response;
}

response.sid = static_cast<quint8>(payload[1]);
if (response.sid == 0x7F) {
    if (serviceLength < 3) {
        return response;
    }
    response.originalSid = static_cast<quint8>(payload[2]);
    response.negativeCode = static_cast<quint8>(payload[3]);
    response.negative = true;
    response.valid = true;
    return response;
}

if (response.sid >= 0x40 && response.sid <= 0xEF) {
    response.data = payload.mid(2, serviceLength - 1);
    response.positive = true;
    response.valid = true;
}
```

`RfidService::handleResponseFrame()` 展示建议：

- 肯定响应无参数：`Positive SID=0x51`
- 肯定响应带参数：`Positive SID=0x69 Data=02 C0 00 64`
- 否定响应保留现有格式：`Negative SID=0x29 NRC=0x31`

### 5.3 UI 控制层

在 `MainWindow::createRfidMonitorTab()` 中，于现有“控制”分组下方新增 `QGroupBox("通用诊断控制")`。

建议控件：

| 功能 | 控件 | 默认值/范围 |
| --- | --- | --- |
| 跳转控制 | `QComboBox` + `QPushButton("执行跳转")` | APP=`0x01`，BOOT=`0x02` |
| 软件复位 | `QPushButton("软件复位")` | 无参数 |
| 广播控制 | `QComboBox` + `QPushButton("应用广播控制")` | 使能=`0x01`，禁止=`0x00` |
| 周期配置 | `QLineEdit("目标ID")` + `QSpinBox` + `QPushButton("设置广播周期")` | ID 十六进制；周期 `16~65535` ms |
| 故障诊断 | `QComboBox` + `QPushButton("应用诊断控制")` | 启用=`0x01`，禁用=`0x00` |
| 非易失写入 | `QLineEdit("DID")` + `QLineEdit("数据HEX")` + `QPushButton("写入")` | DID 十六进制；数据 0~4 byte |

发送路径：

```cpp
sendRfidFrame(RfidProtocol::RequestFrameId, payload);
```

发送前校验：

- CAN 已启动，否则弹出 `QMessageBox::warning`。
- CAN ID 输入必须为 `0x000~0x7FF` 标准帧范围，建议周期配置默认限制在 RFID 周期 ID 范围 `0x2C0~0x2DF`，允许用户手动输入其他标准 ID 时给出确认。
- `0x29` 周期值范围为 `16~65535`，其中 `65535` 表示不发送。
- DID 输入必须为 `0x0000~0xFFFF`。
- HEX 数据必须由完整字节组成，允许空格分隔，例如 `AB CD 01`。
- `0x2E` 数据长度最大 4 byte；超过时拒绝发送并提示“当前仅支持 ISO15765-2 单帧，写入数据最多 4 字节”。
- `0x2E` 为预留且会写非易失存储区，发送前二次确认。

### 5.4 MainWindow 具体落点

`mainwindow.h` 建议新增 1 个私有辅助方法和 2 个解析方法，减少 `createRfidMonitorTab()` 里的 lambda 过长：

```cpp
bool ensureCanStartedForRfidDiagnostic() const;
bool parseHexUInt16Input(const QString &text, quint16 *value, const QString &fieldName);
bool parseHexByteArrayInput(const QString &text, QByteArray *data, const QString &fieldName);
```

如果希望最小改动，也可以把解析逻辑写成 `mainwindow.cpp` 文件内 `static` 函数，不暴露到头文件。推荐文件内 `static` 函数，减少类成员膨胀：

```cpp
static QString normalizeHexText(QString text)
static bool parseHexUInt16(const QString &text, quint16 *value)
static bool parseHexByteArray(const QString &text, QByteArray *data)
```

新增控件是否写入 `mainwindow.h`：

- 若只在按钮 lambda 中读取控件值，可以使用局部变量，不必作为成员。
- 若需要在 `updateControlsState()` 中统一启停新增按钮，再把按钮指针保存为成员。
- 本次推荐先使用局部变量，发送前通过 `canStarted` 兜底校验，保持改动小。

UI 布局建议：

```text
RFID定位器
├─ 控制
├─ 通用诊断控制
├─ TAG
├─ 状态信息
└─ 设备信息
```

`layout->addWidget()` 建议调整为：

```cpp
layout->addWidget(controlGroup, 0, 0);
layout->addWidget(diagnosticGroup, 1, 0);
layout->addWidget(tagGroup, 2, 0);
layout->addWidget(statusGroup, 0, 1);
layout->addWidget(deviceGroup, 1, 1, 2, 1);
```

这样新增诊断面板不会挤压右侧状态区域太多，且用户能在左侧连续完成控制类操作。

### 5.5 参数解析规则

十六进制输入统一规则：

- 允许 `0x2C0`、`2C0`、`02 C0` 三种形式用于 ID/DID，其中 `02 C0` 需先去空格再解析。
- 不允许负数、小数、超过字段宽度的值。
- HEX 数据允许空格、逗号、`0x` 前缀，例如 `AB CD`、`0xAB 0xCD`。
- HEX 数据必须为完整字节，清理分隔符后字符数必须为偶数。

解析失败提示要带字段名，例如：

```text
目标ID格式无效，请输入 0x000~0x7FF 范围内的十六进制标准帧ID。
DID格式无效，请输入 0x0000~0xFFFF 范围内的十六进制值。
写入数据格式无效，请输入完整HEX字节，例如 AB CD 01。
```

### 5.6 操作确认策略

为降低实机误操作风险，建议下列按钮增加确认框：

- `0x10` 跳转 BOOT/APP：会改变设备运行区。
- `0x11` 软件复位：设备会短时离线。
- `0x2E` 写非易失：可能持久化改变设备配置。

确认文案示例：

```text
确认执行 BOOT/APP 跳转吗？设备可能短时离线。
确认执行软件复位吗？设备会重启并短时无响应。
确认写入非易失存储区吗？该操作可能改变设备持久化配置。
```

`0x28/0x29/0x85` 可不强制确认，但 `0x29` 配置非 RFID 周期 ID 时建议提示确认。

## 6. 实现步骤

1. 在 `rfidprotocol.h/.cpp` 添加单帧构造函数和 6 个服务构造函数。
2. 调整 `parseResponseFrame` 的肯定响应判断，支持 `0x50/0x51/0x68/0x69/0xC5/0x6E`。
3. 视是否展示响应参数，决定是否给 `RfidResponse` 增加 `data` 字段，并同步调整 `RfidService::handleResponseFrame()`。
4. 在 `mainwindow.cpp` 新增通用诊断 UI 控件和按钮绑定。
5. 为输入解析补充本地校验和错误提示，避免非法帧下发。
6. 对危险操作增加确认框。
7. 编译 Release，确认无语法、链接和 warning 问题。
8. 使用实时日志人工核对 Tx/Rx 帧。

### 6.1 建议提交顺序

建议拆成 3 个小提交，便于审查和回滚：

1. `feat: add mt can diagnostic frame builders`
   - 只改 `rfidprotocol.h/.cpp`
   - 包含单帧构造和响应解析
2. `feat: add mt rfid diagnostic controls`
   - 只改 `mainwindow.cpp`，必要时少量改 `mainwindow.h`
   - 新增 UI、参数校验和发送绑定
3. `test: document mt diagnostic verification cases`
   - 如项目暂无 Qt 单测，可只补手工验证记录或保持本设计文档作为验证依据

### 6.2 开发自检清单

编码完成后逐项检查：

- `0x85` 的响应 `0xC5` 能被识别为肯定响应。
- `0x2E` 超过 4 byte 数据不会发送。
- `0x29` 使用大端编码 ID 和周期。
- 所有新增请求帧长度为 8 byte，尾部填充 `0x55`。
- CAN 未启动时不会调用 `sendClassicData()`。
- 新增 UI 不影响原“开始检测”“停止检测”“设置周期”“模块重启”按钮。
- 关闭窗口、切换协议页、清空日志等原有流程不受影响。

## 7. 验证计划

### 7.1 编码验证矩阵

| 操作 | 输入 | 预期发送 ID | 预期发送数据 |
| --- | --- | --- | --- |
| 跳转 APP | APP | `0x07` | `02 10 01 55 55 55 55 55` |
| 跳转 BOOT | BOOT | `0x07` | `02 10 02 55 55 55 55 55` |
| 软件复位 | - | `0x07` | `01 11 55 55 55 55 55 55` |
| 使能广播 | 使能 | `0x07` | `02 28 01 55 55 55 55 55` |
| 禁止广播 | 禁止 | `0x07` | `02 28 00 55 55 55 55 55` |
| 设置周期 | ID=`0x2C0`，周期=`100` | `0x07` | `05 29 02 C0 00 64 55 55` |
| 禁止指定 ID 发送 | ID=`0x2C0`，周期=`65535` | `0x07` | `05 29 02 C0 FF FF 55 55` |
| 启用诊断 | 启用 | `0x07` | `02 85 01 55 55 55 55 55` |
| 禁用诊断 | 禁用 | `0x07` | `02 85 00 55 55 55 55 55` |
| 写非易失 | DID=`0x1234`，数据=`AB CD` | `0x07` | `05 2E 12 34 AB CD 55 55` |

### 7.2 响应验证矩阵

| 模拟接收 ID | 模拟接收数据 | 预期显示 |
| --- | --- | --- |
| `0x107` | `02 50 01 55 55 55 55 55` | `Positive SID=0x50` |
| `0x107` | `01 51 55 55 55 55 55 55` | `Positive SID=0x51` |
| `0x107` | `02 68 00 55 55 55 55 55` | `Positive SID=0x68` |
| `0x107` | `05 69 02 C0 00 64 55 55` | `Positive SID=0x69` 或 `Positive SID=0x69 Data=02 C0 00 64` |
| `0x107` | `02 C5 01 55 55 55 55 55` | `Positive SID=0xc5` |
| `0x107` | `03 6E 12 34 55 55 55 55` | `Positive SID=0x6e` 或 `Positive SID=0x6e Data=12 34` |
| `0x107` | `03 7F 10 22 55 55 55 55` | `Negative SID=0x10 NRC=0x22` |
| `0x107` | `03 7F 29 31 55 55 55 55` | `Negative SID=0x29 NRC=0x31` |

### 7.3 手工验证

1. 启动 CAN，进入“美团协议”RFID 监控页。
2. 点击新增诊断面板的每个按钮。
3. 在实时日志中确认 Tx 帧 ID 和数据与 7.1 矩阵一致。
4. 使用实机或模拟器回放响应帧，确认“响应”标签与 7.2 矩阵一致。
5. 对非法输入做负向验证：
   - 周期小于 16。
   - DID 超出 `0xFFFF`。
   - HEX 数据含非法字符。
   - `0x2E` 数据超过 4 byte。
   - CAN 未启动时点击发送。

### 7.4 验收标准

交付给测试或实施人员时，至少满足：

- Release 构建通过。
- 新增 6 类诊断请求都能从 UI 发出。
- 实时日志中的 Tx 帧与 7.1 矩阵一致。
- 模拟或实机 Rx 肯定/否定响应均能更新“响应”标签。
- 非法输入不会发送 CAN 帧，并给出明确提示。
- 原有 RFID 监控、压测、OTA、RS485 功能入口可正常打开，不出现启动崩溃。

## 8. 风险与处理

| 风险 | 影响 | 处理 |
| --- | --- | --- |
| `0x2E` 为预留且写非易失存储区 | 可能改变设备持久化配置 | 默认谨慎开放，发送前二次确认 |
| 当前仅支持单帧 | `0x2E` 长数据无法发送 | 限制数据最大 4 byte，后续再评估多帧发送 |
| 部分设备未实现某些通用服务 | 返回 NRC 或无响应 | UI 只展示响应，不做业务状态强假设 |
| `0x29` 周期配置下电保存 | 错误配置会持续影响设备广播 | 输入校验并建议默认 RFID 广播 ID 范围 |
| 跳转 BOOT/APP 或软件复位会改变设备运行状态 | 设备可能短时离线 | 操作按钮建议配确认提示，实机验证时谨慎使用 |

## 9. 后续建议

- 若后续需要完整支持 `0x2E` 长数据，复用项目已有 ISO15765-2 多帧能力或新增专用发送状态机。
- 若需要更友好的应答展示，可扩展 `RfidResponse` 保存响应参数，并将 `0x69/0x6E` 的回显字段展示出来。
- 实机验证通过后，可补充 Qt 单元测试覆盖 `RfidProtocol` 的帧构造与响应解析。

## 10. 交付给实施人员的注意事项

- 以 `qingju` 分支最新代码为基准实施。
- 先阅读 `HANDOFF.md`，不要改动 RS485 线程化和 OTA 稳定性相关代码。
- 本需求只针对美团协议 RFID 页，不要改青桔协议页。
- 本需求只实现 `CAN总线.pdf` 第 7.2 章，不要把 OTA PDF 中的 `0xA1~0xA4` 纳入本次 UI。
- 如发现设备实际响应与本设计不一致，优先记录实机帧和协议差异，不要直接扩大实现范围。
- 若实现修改超过 3 个文件或需要引入多帧发送，应暂停并重新评审方案。
