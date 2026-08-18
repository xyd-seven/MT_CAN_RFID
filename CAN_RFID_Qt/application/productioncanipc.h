#ifndef PRODUCTIONCANIPC_H
#define PRODUCTIONCANIPC_H

#include <QtGlobal>

namespace ProductionCanIpc {
constexpr quint8 StartDevice = 1;
constexpr quint8 StopDevice = 2;
constexpr quint8 SendFrame = 3;
constexpr quint8 SetPeriodicControl = 4;
constexpr quint8 SendManualControl = 5;
constexpr quint8 Response = 100;
constexpr quint8 ReceivedFrames = 101;
constexpr quint8 Diagnostic = 102;
constexpr int RequestTimeoutMs = 3000;
constexpr int WorkerStartupTimeoutMs = 5000;
constexpr quint32 MaxPacketBytes = 1024U * 1024U;
}

#endif // PRODUCTIONCANIPC_H
