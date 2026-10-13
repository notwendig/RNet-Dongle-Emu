#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace dongle_recovered {

class CUSBMsg;
class CCANMsg;

class CRawMsg {
public:
    static constexpr std::size_t RAW_MSG_LEN = 19;
    static constexpr std::size_t RAW_MSG_LEN_V2 = 13;
    static constexpr std::uint8_t USB_TYP = 0;
    static constexpr std::uint8_t CAN_TYP = 1;
    static constexpr std::uint32_t MAX_CAN_IDENT = 0x1fffffffU;
    static constexpr std::uint32_t MAX_DLC = 8;
    static constexpr std::uint8_t CAN_DLC_MASK = 0x0f;
    static constexpr int CAN_EXT_INDEX = 2;
    static constexpr std::uint8_t CAN_EXT_MASK = 0x08;
    static constexpr int CAN_EXT_REM_INDEX = 4;
    static constexpr std::uint8_t CAN_EXT_REM_MASK = 0x01;
    static constexpr int CAN_STD_REM_INDEX = 2;
    static constexpr std::uint8_t CAN_STD_REM_MASK = 0x10;
    static constexpr std::uint8_t CAN_FILTER_MASK = 0xf0;
    static constexpr int MSG_TYPE_INDEX = 0;

    CRawMsg();
    explicit CRawMsg(const CUSBMsg& msg);
    explicit CRawMsg(const CCANMsg& msg);

    void ClearData();
    bool AddByte(std::uint8_t value);
    std::uint8_t operator[](int index) const;
    std::uint8_t& operator[](int index);
    const std::uint8_t* GetMsgPointer() const;
    const std::uint8_t* GetDataBufferPointer() const;
    bool IsComplete() const;
    bool IsCANMsg() const;
    bool IsUSBMsg() const;
    int GetMessageLength() const;
    int GetNumberBytes() const;
    std::uint8_t CalcChecksum(int count) const;
    bool IsVersion2Msg() const;

    void ExtractFrom(const CUSBMsg& msg);
    void ExtractFrom(const CCANMsg& msg);

private:
    std::array<std::uint8_t, RAW_MSG_LEN> raw_{};
    std::uint32_t numberBytes_{};
};

class CCANMsg {
public:
    enum FILTER_TYPE : std::uint32_t {
        FILTER_TYPE_0 = 0,
        FILTER_TYPE_1 = 1,
        FILTER_TYPE_2 = 2,
        FILTER_TYPE_3 = 3,
        FILTER_TYPE_4 = 4,
    };

    static constexpr std::size_t DATA_LENGTH = 8;
    static constexpr std::uint32_t INVALID_ID = 0xffffffffU;
    static constexpr std::uint32_t ID_MATCH_MASK = 0x00000ff0U;
    static constexpr std::uint32_t MODE_CHANGE_ID = 0x60U;
    static constexpr std::uint32_t PROFILE_CHANGE_ID = 0x50U;
    static constexpr std::uint32_t NODE_MASK = 0xf00U;
    static constexpr std::uint32_t POP_ID = 0x780U;
    static constexpr std::uint32_t POP_MASK = 0x7e0U;
    static constexpr std::uint32_t SLOT_CHANGED_STD_ID = 0x540U;

    CCANMsg();
    explicit CCANMsg(const CRawMsg& raw);
    virtual ~CCANMsg() = default;

    void ClearMsg();
    bool Convert(const CRawMsg& raw);

    std::uint8_t operator[](int index) const;
    std::uint8_t& operator[](int index);

    std::uint32_t GetServiceID() const;
    void SetID(std::uint32_t id);
    std::uint32_t GetID() const;
    std::uint32_t GetStandardID() const;

    void SetExtended(bool extended);
    bool IsExtended() const;
    bool IsStandard() const;

    void SetRemote(bool remote);
    bool IsRemote() const;
    bool IsData() const;

    std::uint32_t GetDLC() const;
    void SetDLC(std::uint32_t dlc);

    void SetFilterType(FILTER_TYPE type);
    FILTER_TYPE GetFilterType() const;
    bool IsUnfilteredMsg() const;

    bool IsPOPMsg() const;
    bool StdIDMatches(std::uint32_t value) const;
    std::uint8_t GetNodeFromServiceMessage() const;
    void InsertNodeIntoServiceMessage(std::uint8_t node);
    bool IsModeChangeMsg() const;
    bool IsProfileChangeMsg() const;
    bool IsSlotChangedMsg() const;

    void GetData(std::uint8_t& value) const;
    void GetData(std::uint16_t& value) const;
    void GetData(std::uint32_t& value) const;

    static void SetServerNode(std::uint8_t node);
    static void SetClientNode(std::uint8_t node);
    static void SetClientServerNodes(std::uint8_t client, std::uint8_t server);
    static std::uint8_t GetClientNode();
    static std::uint8_t GetServerNode();

protected:
    std::uint32_t id_{INVALID_ID};
    std::array<std::uint8_t, DATA_LENGTH> data_{};
    bool extended_{};
    bool remote_{};
    std::uint32_t dlc_{};
    FILTER_TYPE filterType_{FILTER_TYPE_4};

    static std::uint8_t client_;
    static std::uint8_t server_;
};

class CPOPMsg : public CCANMsg {
public:
    // The original DLL exported the enum type name but not its member names.
    // Numeric values are therefore preserved verbatim instead of guessed labels.
    enum POP_MSG_TYPE : std::uint32_t {
        POP_TYPE_2  = 2,
        POP_TYPE_3  = 3,
        POP_TYPE_4  = 4,
        POP_TYPE_5  = 5,
        POP_TYPE_6  = 6,
        POP_TYPE_7  = 7,
        POP_TYPE_8  = 8,
        POP_TYPE_9  = 9,
        POP_TYPE_10 = 10,
        POP_TYPE_11 = 11,
        POP_TYPE_12 = 12,
        POP_TYPE_13 = 13,
        POP_TYPE_14 = 14,
        POP_TYPE_15 = 15,
        POP_TYPE_16 = 16,
        POP_TYPE_17 = 17,
        POP_TYPE_18 = 18,
        POP_TYPE_19 = 19,
        POP_TYPE_20 = 20,
    };

    static constexpr std::uint8_t CODE_DATA_NODE_MASK = 0x0f;
    static constexpr std::uint8_t CRC_BIT = 0x10;
    static constexpr std::uint8_t QUICK_BIT = 0x20;
    static constexpr std::uint32_t POP_CODE_DATA_INDEX = 0;
    static constexpr std::uint32_t POP_QCK_NODE_MASK = 0x0f;
    static constexpr std::uint32_t POP_QCK_REQ_ID = 0x780;
    static constexpr std::uint32_t POP_QCK_RES_ID = 0x790;
    static constexpr std::uint32_t POP_QCK_TC_INDEX = 0;
    static constexpr std::uint8_t POP_QCK_TC_MASK = 0x3f;
    static constexpr std::uint32_t POP_QCK_TC_SHIFT = 6;
    static constexpr std::uint32_t POP_SEG_NODE_MASK = 0x003c0000U;
    static constexpr std::uint32_t POP_SEG_NODE_SHIFT = 18;
    static constexpr std::uint32_t POP_SEG_REQ_ID = 0x1e400000U;
    static constexpr std::uint32_t POP_SEG_RES_ID = 0x1e000000U;
    static constexpr std::uint32_t POP_SEG_TC_MASK = 0x00030000U;
    static constexpr std::uint32_t POP_SEG_TC_SHIFT = 16;
    static constexpr std::uint32_t SEGMENT_MASK = 0xffffU;

    static constexpr std::uint8_t TC_DLS_REQ = 0;
    static constexpr std::uint8_t TC_DLS_REQ_LAST = 1;
    static constexpr std::uint8_t TC_DLS_RES = 1;
    static constexpr std::uint8_t TC_DL_REQ = 0;
    static constexpr std::uint8_t TC_DL_RES = 1;
    static constexpr std::uint8_t TC_END = 2;
    static constexpr std::uint8_t TC_ABORT = 3;
    static constexpr std::uint8_t TC_ILLEGAL = 0xff;
    static constexpr std::uint8_t TC_ULS_REQ = 2;
    static constexpr std::uint8_t TC_ULS_REQ_LAST = 3;
    static constexpr std::uint8_t TC_ULS_RES = 3;
    static constexpr std::uint8_t TC_UL_REQ = 1;
    static constexpr std::uint8_t TC_UL_RES = 0;

    CPOPMsg();
    explicit CPOPMsg(const CCANMsg& msg);
    explicit CPOPMsg(POP_MSG_TYPE type);
    CPOPMsg(POP_MSG_TYPE type, std::uint32_t odi, std::uint32_t size, std::uint32_t block);

    void SetODI(std::uint32_t value);
    std::uint32_t GetODI() const;
    void SetSize(std::uint32_t value);
    std::uint32_t GetSize() const;
    void SetBlock(std::uint32_t value);
    std::uint32_t GetBlock() const;

    bool IsPOPSegMsg() const;
    bool IsSegMsg() const;
    bool IsServerMsg() const;
    bool IsClientMsg() const;
    std::uint8_t GetTransferCode() const;
    bool SetTransferCode(std::uint8_t value);
    bool IsLastSegment() const;
    bool IsAbortMsg() const;

    void SetQuickFlag(bool enabled);
    bool GetQuickFlag() const;
    void SetCRCFlag(bool enabled);
    bool GetCRCFlag() const;

    void SetCRC(std::uint16_t crc);
    std::uint32_t GetAbortCode() const;

    std::uint32_t GetSegmentNumber() const;
    void SetSegmentNumber(std::uint32_t segment);
    const std::uint8_t* GetDataBuffer() const;
    std::uint32_t GetDataPOPSeg(std::uint8_t* out) const;
    CPOPMsg& GetDataFrom(const CCANMsg& msg);

    void SetClientServerToMsg(std::uint8_t client, std::uint8_t server);
    void SetClientToMsg(std::uint8_t client);
    void SetServerToMsg(std::uint8_t server);
    std::uint8_t GetClientFromMsg() const;
    std::uint8_t GetServerFromMsg() const;

    void SetType(POP_MSG_TYPE type);

private:
    std::uint8_t GetNodeFromID() const;
    void SetNodeToID(std::uint8_t node);
    std::uint8_t GetNodeFromData() const;
    void SetNodeToData(std::uint8_t node);
};

class CUSBMsg {
public:
    static constexpr std::size_t DATA_LENGTH = 16;

    static constexpr std::uint8_t NOT_ATTACHED = 0;
    static constexpr std::uint8_t ATTACHING = 1;
    static constexpr std::uint8_t ATTACHED = 2;
    static constexpr std::uint8_t CMD_STATUS = 0xff;
    static constexpr std::uint8_t SUB_STATUS_0 = 0;

    static constexpr std::uint8_t USBCMD_SERVICE = 1;
    static constexpr std::uint8_t USBCMD_MODES = 2;
    static constexpr std::uint8_t USBCMD_LED = 3;
    static constexpr std::uint8_t USBCMD_FILTER = 4;
    static constexpr std::uint8_t USBCMD_CPU = 5;
    static constexpr std::uint8_t USBCMD_POP = 6;
    static constexpr std::uint8_t USBCMD_E2 = 7;
    static constexpr std::uint8_t USBCMD_CONDUIT = 8;

    static constexpr std::uint8_t SVC_PARAM_R = 0;
    static constexpr std::uint8_t SVC_PARAM_W = 1;
    static constexpr std::uint8_t SVC_XY_CNT_R = 2;
    static constexpr std::uint8_t SVC_XY_CNT_W = 3;
    static constexpr std::uint8_t SVC_KEYS_R = 4;
    static constexpr std::uint8_t SVC_FAULT_R = 5;
    static constexpr std::uint8_t SVC_FAULT_W = 6;
    static constexpr std::uint8_t SVC_RTC_R = 7;

    static constexpr std::uint8_t MDE_ERRS_R = 0;
    static constexpr std::uint8_t MDE_ERRS_W = 1;
    static constexpr std::uint8_t MDE_AUTOTX_R = 2;
    static constexpr std::uint8_t MDE_AUTOTX_W = 3;
    static constexpr std::uint8_t MDE_PING = 4;

    static constexpr std::uint8_t LED_TIMEOUT_R = 0;
    static constexpr std::uint8_t LED_TIMEOUT_W = 1;

    static constexpr std::uint8_t FLT_FLT_R = 0;
    static constexpr std::uint8_t FLT_FLT_W = 1;
    static constexpr std::uint8_t FLT_MODE_R = 2;
    static constexpr std::uint8_t FLT_MODE_W = 3;

    static constexpr std::uint8_t CPU_RESET_W = 0;
    static constexpr std::uint8_t CPU_CANSLP_R = 1;
    static constexpr std::uint8_t CPU_CANSLP_W = 2;
    static constexpr std::uint8_t CPU_FWVER_R = 3;
    static constexpr std::uint8_t CPU_STATUS_R = 4;
    static constexpr std::uint8_t CPU_SLP_DISABLE_W = 5;
    static constexpr std::uint8_t CPU_SLP_DISABLE_R = 6;
    static constexpr std::uint8_t CPU_PASSTHRU_W = 7;
    static constexpr std::uint8_t CPU_PASSTHRU_R = 8;
    static constexpr std::uint8_t CPU_FLASH_R = 9;
    static constexpr std::uint8_t CPU_FLASH_W = 10;

    static constexpr std::uint8_t POP_CLIENT_R = 0;
    static constexpr std::uint8_t POP_CLIENT_W = 1;
    static constexpr std::uint8_t POP_SERVER_R = 2;
    static constexpr std::uint8_t POP_SERVER_W = 3;

    static constexpr std::uint8_t E2_ACCESS_R = 0;
    static constexpr std::uint8_t E2_ACCESS_W = 1;
    static constexpr std::uint8_t CONDUIT_R = 0;
    static constexpr std::uint8_t CONDUIT_W = 1;

    CUSBMsg();
    explicit CUSBMsg(const CRawMsg& raw);
    virtual ~CUSBMsg() = default;

    void ClearData();
    void GetFromRaw(const CRawMsg& raw);
    CUSBMsg& operator=(const CUSBMsg&) = default;

    std::uint8_t operator[](int index) const;
    std::uint8_t& operator[](int index);

    std::uint8_t GetCommand() const;
    void SetCommand(std::uint8_t value);
    std::uint8_t GetSubCommand() const;
    void SetSubCommand(std::uint8_t value);
    std::uint8_t GetByteData(int index) const;
    std::uint16_t GetWordData(int index) const;
    std::uint32_t GetDWordData(int index) const;
    std::uint64_t GetQWordData(int index) const;
    const std::uint8_t* GetDataPointer() const;
    static constexpr std::uint32_t GetDataLength() { return DATA_LENGTH; }

    template <typename T> void SetData(int index, T value) = delete;
    template <typename T> void GetData(int index, T& value) const = delete;

    bool MatchesCommand(std::uint8_t command, std::uint8_t subCommand) const;
    bool IsStatus_0() const;
    bool IsSystemConnected() const;
    bool IsSystemAttached() const;

    void SetCmdReadService();
    void SetCmdClearService();
    void SetCmdReadXYCount();
    void SetCmdWriteXYCount(std::uint8_t value);
    void SetCmdReadKeys();
    void SetCmdReadLiveFaultOrganID();
    void SetCmdWriteLiveFaultOrganIDClear();
    void SetCmdReadErrors();
    void SetCmdClearErrors();
    void SetCmdReadUSBTickleTime();
    void SetCmdPing();
    void SetCmdReadLEDTimeout();
    void SetCmdReadFW();
    void SetCmdReadStatus();
    void SetCmdReadSleepDisable();
    void SetCmdReadRealTimeClock();
    void SetCmdReadPassThrough();
    void SetCmdReadFlashOverCAN();
    void SetCmdReadFilterRegisters();
    void SetCmdWriteFilterRegisters(std::uint8_t a0, std::uint8_t a1, std::uint8_t a2,
                                    std::uint8_t a3, std::uint8_t a4, std::uint8_t a5,
                                    std::uint8_t a6, std::uint8_t a7, std::uint8_t a8);
    void SetCmdReadFilterMode();
    void SetCmdWriteFilterMode(std::uint8_t value);
    void SetCmdCPUReset();
    void SetCmdCANReadSleep();
    void SetCmdCANWriteSleep();
    void SetCmdReadAwake();
    void SetCmdReadConduit();
    void SetCmdWriteConduit(bool enabled);
    void SetCmdWriteAwake(bool enabled);
    void SetCmdReadEEPROM(std::uint8_t address);
    void SetCmdWriteUSBTickleTime(std::uint16_t value);
    void SetCmdWriteSleepDisable(bool enabled);
    void SetCmdWritePassThrough(bool enabled);
    void SetCmdWriteFlashOverCAN(bool enabled);
    void SetCmdWriteLEDTimeout(std::uint16_t first, std::uint16_t second);
    void SetCmdWriteEEPROM(std::uint8_t address, std::uint16_t value);

private:
    std::uint8_t command_{};
    std::uint8_t subCommand_{};
    std::array<std::uint8_t, DATA_LENGTH> data_{};
};

template <>
inline void CUSBMsg::SetData<bool>(int index, bool value) {
    data_[static_cast<std::size_t>(index)] = value ? 1 : 0;
}

template <>
inline void CUSBMsg::GetData<bool>(int index, bool& value) const {
    value = data_[static_cast<std::size_t>(index)] != 0;
}

class CCANMsgResetTimer : public CCANMsg {
public:
    CCANMsgResetTimer();
};

} // namespace dongle_recovered
