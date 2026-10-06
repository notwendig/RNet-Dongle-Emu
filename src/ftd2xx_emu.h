#pragma once
#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef PVOID FT_HANDLE;
typedef ULONG FT_STATUS;
typedef ULONG FT_DEVICE;

enum {
    FT_OK = 0,
    FT_INVALID_HANDLE,
    FT_DEVICE_NOT_FOUND,
    FT_DEVICE_NOT_OPENED,
    FT_IO_ERROR,
    FT_INSUFFICIENT_RESOURCES,
    FT_INVALID_PARAMETER,
    FT_INVALID_BAUD_RATE,
    FT_DEVICE_NOT_OPENED_FOR_ERASE,
    FT_DEVICE_NOT_OPENED_FOR_WRITE,
    FT_FAILED_TO_WRITE_DEVICE,
    FT_EEPROM_READ_FAILED,
    FT_EEPROM_WRITE_FAILED,
    FT_EEPROM_ERASE_FAILED,
    FT_EEPROM_NOT_PRESENT,
    FT_EEPROM_NOT_PROGRAMMED,
    FT_INVALID_ARGS,
    FT_NOT_SUPPORTED,
    FT_OTHER_ERROR,
    FT_DEVICE_LIST_NOT_READY
};

#define FT_OPEN_BY_SERIAL_NUMBER 1
#define FT_OPEN_BY_DESCRIPTION   2
#define FT_OPEN_BY_LOCATION      4

#define FT_EVENT_RXCHAR          1
#define FT_EVENT_MODEM_STATUS    2
#define FT_EVENT_LINE_STATUS     4

#define FT_PURGE_RX               1
#define FT_PURGE_TX               2

#define FT_LIST_NUMBER_ONLY       0x80000000u
#define FT_LIST_BY_INDEX          0x40000000u
#define FT_LIST_ALL               0x20000000u

#define FT_FLAGS_OPENED          1
#define FT_FLAGS_HISPEED         2

enum {
    FT_DEVICE_BM,
    FT_DEVICE_AM,
    FT_DEVICE_100AX,
    FT_DEVICE_UNKNOWN,
    FT_DEVICE_2232C,
    FT_DEVICE_232R,
    FT_DEVICE_2232H,
    FT_DEVICE_4232H,
    FT_DEVICE_232H,
    FT_DEVICE_X_SERIES
};

typedef struct _ft_device_list_info_node {
    ULONG Flags;
    ULONG Type;
    ULONG ID;
    DWORD LocId;
    char SerialNumber[16];
    char Description[64];
    FT_HANDLE ftHandle;
} FT_DEVICE_LIST_INFO_NODE;

FT_STATUS WINAPI FT_Open(int deviceNumber, FT_HANDLE *pHandle);
FT_STATUS WINAPI FT_ListDevices(PVOID pArg1, PVOID pArg2, DWORD Flags);
FT_STATUS WINAPI FT_Close(FT_HANDLE ftHandle);
FT_STATUS WINAPI FT_Read(FT_HANDLE ftHandle, LPVOID lpBuffer, DWORD dwBytesToRead, LPDWORD lpBytesReturned);
FT_STATUS WINAPI FT_Write(FT_HANDLE ftHandle, LPVOID lpBuffer, DWORD dwBytesToWrite, LPDWORD lpBytesWritten);
FT_STATUS WINAPI FT_IoCtl(FT_HANDLE ftHandle, DWORD dwIoControlCode, LPVOID lpInBuf, DWORD nInBufSize, LPVOID lpOutBuf, DWORD nOutBufSize, LPDWORD lpBytesReturned, LPOVERLAPPED lpOverlapped);
FT_STATUS WINAPI FT_SetBaudRate(FT_HANDLE ftHandle, ULONG BaudRate);
FT_STATUS WINAPI FT_SetDataCharacteristics(FT_HANDLE ftHandle, UCHAR WordLength, UCHAR StopBits, UCHAR Parity);
FT_STATUS WINAPI FT_SetFlowControl(FT_HANDLE ftHandle, USHORT FlowControl, UCHAR XonChar, UCHAR XoffChar);
FT_STATUS WINAPI FT_ResetDevice(FT_HANDLE ftHandle);
FT_STATUS WINAPI FT_SetDtr(FT_HANDLE ftHandle);
FT_STATUS WINAPI FT_ClrDtr(FT_HANDLE ftHandle);
FT_STATUS WINAPI FT_SetRts(FT_HANDLE ftHandle);
FT_STATUS WINAPI FT_ClrRts(FT_HANDLE ftHandle);
FT_STATUS WINAPI FT_GetModemStatus(FT_HANDLE ftHandle, ULONG *pModemStatus);
FT_STATUS WINAPI FT_SetChars(FT_HANDLE ftHandle, UCHAR EventChar, UCHAR EventCharEnabled, UCHAR ErrorChar, UCHAR ErrorCharEnabled);
FT_STATUS WINAPI FT_Purge(FT_HANDLE ftHandle, ULONG Mask);
FT_STATUS WINAPI FT_SetTimeouts(FT_HANDLE ftHandle, ULONG ReadTimeout, ULONG WriteTimeout);
FT_STATUS WINAPI FT_GetQueueStatus(FT_HANDLE ftHandle, DWORD *dwRxBytes);
FT_STATUS WINAPI FT_SetEventNotification(FT_HANDLE ftHandle, DWORD Mask, PVOID Param);
FT_STATUS WINAPI FT_GetStatus(FT_HANDLE ftHandle, DWORD *dwRxBytes, DWORD *dwTxBytes, DWORD *dwEventDWord);
FT_STATUS WINAPI FT_GetEventStatus(FT_HANDLE ftHandle, DWORD *dwEventDWord);
FT_STATUS WINAPI FT_SetBreakOn(FT_HANDLE ftHandle);
FT_STATUS WINAPI FT_SetBreakOff(FT_HANDLE ftHandle);
FT_STATUS WINAPI FT_SetWaitMask(FT_HANDLE ftHandle, DWORD Mask);
FT_STATUS WINAPI FT_WaitOnMask(FT_HANDLE ftHandle, DWORD *Mask);
FT_STATUS WINAPI FT_SetDivisor(FT_HANDLE ftHandle, USHORT Divisor);
FT_STATUS WINAPI FT_OpenEx(PVOID pArg1, DWORD Flags, FT_HANDLE *pHandle);
FT_STATUS WINAPI FT_SetLatencyTimer(FT_HANDLE ftHandle, UCHAR ucLatency);
FT_STATUS WINAPI FT_GetLatencyTimer(FT_HANDLE ftHandle, PUCHAR pucLatency);
FT_STATUS WINAPI FT_SetBitMode(FT_HANDLE ftHandle, UCHAR ucMask, UCHAR ucEnable);
FT_STATUS WINAPI FT_GetBitMode(FT_HANDLE ftHandle, PUCHAR pucMode);
FT_STATUS WINAPI FT_SetUSBParameters(FT_HANDLE ftHandle, ULONG ulInTransferSize, ULONG ulOutTransferSize);
FT_STATUS WINAPI FT_EE_UARead(FT_HANDLE ftHandle, PUCHAR pucData, DWORD dwDataLen, LPDWORD lpdwBytesRead);
FT_STATUS WINAPI FT_EE_UASize(FT_HANDLE ftHandle, LPDWORD lpdwSize);
FT_STATUS WINAPI FT_EE_UAWrite(FT_HANDLE ftHandle, PUCHAR pucData, DWORD dwDataLen);
FT_STATUS WINAPI FT_StopInTask(FT_HANDLE ftHandle);
FT_STATUS WINAPI FT_RestartInTask(FT_HANDLE ftHandle);
FT_STATUS WINAPI FT_ResetPort(FT_HANDLE ftHandle);
FT_STATUS WINAPI FT_CyclePort(FT_HANDLE ftHandle);
FT_STATUS WINAPI FT_CreateDeviceInfoList(LPDWORD lpdwNumDevs);
FT_STATUS WINAPI FT_GetDeviceInfoList(FT_DEVICE_LIST_INFO_NODE *pDest, LPDWORD lpdwNumDevs);
FT_STATUS WINAPI FT_GetDriverVersion(FT_HANDLE ftHandle, LPDWORD lpdwVersion);
FT_STATUS WINAPI FT_GetLibraryVersion(LPDWORD lpdwVersion);

#ifdef __cplusplus
}
#endif
