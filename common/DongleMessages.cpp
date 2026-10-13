#include "DongleMessages.hpp"

namespace dongle_recovered {

std::uint8_t CCANMsg::client_ = 0;
std::uint8_t CCANMsg::server_ = 0;

CRawMsg::CRawMsg() { ClearData(); }
CRawMsg::CRawMsg(const CUSBMsg& msg) { ClearData(); ExtractFrom(msg); }
CRawMsg::CRawMsg(const CCANMsg& msg) { ClearData(); ExtractFrom(msg); }

void CRawMsg::ClearData() {
    raw_.fill(0);
    numberBytes_ = 0;
}

bool CRawMsg::AddByte(std::uint8_t value) {
    if (numberBytes_ == RAW_MSG_LEN) return false;
    raw_[numberBytes_++] = value;
    return true;
}

std::uint8_t CRawMsg::operator[](int index) const { return raw_[static_cast<std::size_t>(index)]; }
std::uint8_t& CRawMsg::operator[](int index) { return raw_[static_cast<std::size_t>(index)]; }
const std::uint8_t* CRawMsg::GetMsgPointer() const { return raw_.data(); }
const std::uint8_t* CRawMsg::GetDataBufferPointer() const { return raw_.data() + 3; }
bool CRawMsg::IsComplete() const { return numberBytes_ == RAW_MSG_LEN; }
bool CRawMsg::IsCANMsg() const { return raw_[0] == CAN_TYP; }
bool CRawMsg::IsUSBMsg() const { return raw_[0] == USB_TYP; }
int CRawMsg::GetMessageLength() const { return static_cast<int>(RAW_MSG_LEN); }
int CRawMsg::GetNumberBytes() const { return static_cast<int>(numberBytes_); }

std::uint8_t CRawMsg::CalcChecksum(int count) const {
    std::uint8_t checksum = 0;
    for (int i = 0; i < count; ++i) checksum ^= raw_[static_cast<std::size_t>(i)];
    return checksum;
}

bool CRawMsg::IsVersion2Msg() const {
    return numberBytes_ == (RAW_MSG_LEN_V2 + 1) && CalcChecksum(static_cast<int>(RAW_MSG_LEN_V2)) == raw_[RAW_MSG_LEN_V2];
}

void CRawMsg::ExtractFrom(const CUSBMsg& msg) {
    raw_[0] = USB_TYP;
    raw_[1] = msg.GetCommand();
    raw_[2] = msg.GetSubCommand();
    for (std::size_t i = 0; i < CUSBMsg::DATA_LENGTH; ++i) raw_[3 + i] = msg[static_cast<int>(i)];
    // Original machine code deliberately does not change numberBytes_ here.
}

void CRawMsg::ExtractFrom(const CCANMsg& msg) {
    raw_.fill(0);
    raw_[0] = CAN_TYP;

    std::uint32_t packed = 0;
    if (msg.IsExtended()) {
        std::uint32_t high = (msg.GetID() & 0xffff0000U) | 0x00030000U;
        high <<= 1;
        const std::uint32_t low = msg.GetID() & 0x0003ffffU;
        high <<= 1;
        high |= low;
        packed = high << 1;
        if (msg.IsRemote()) packed |= 0x00000001U;
    } else {
        packed = msg.GetID() << 21;
        if (msg.IsRemote()) packed |= 0x00100000U;
    }

    raw_[1] = static_cast<std::uint8_t>(packed >> 24);
    raw_[2] = static_cast<std::uint8_t>(packed >> 16);
    raw_[3] = static_cast<std::uint8_t>(packed >> 8);
    raw_[4] = static_cast<std::uint8_t>(packed);

    const std::uint32_t dlc = std::min<std::uint32_t>(msg.GetDLC(), 8U);
    for (std::uint32_t i = 0; i < dlc; ++i) raw_[5 + i] = msg[static_cast<int>(i)];
    raw_[13] = static_cast<std::uint8_t>(dlc);
    // Original machine code deliberately does not change numberBytes_ here.
}

CCANMsg::CCANMsg() { ClearMsg(); }
CCANMsg::CCANMsg(const CRawMsg& raw) { ClearMsg(); Convert(raw); }

void CCANMsg::ClearMsg() {
    id_ = INVALID_ID;
    data_.fill(0);
    extended_ = false;
    remote_ = false;
    dlc_ = 0;
    filterType_ = FILTER_TYPE_4;
}

std::uint8_t CCANMsg::operator[](int index) const { return data_[static_cast<std::size_t>(index)]; }
std::uint8_t& CCANMsg::operator[](int index) { return data_[static_cast<std::size_t>(index)]; }

std::uint32_t CCANMsg::GetServiceID() const { return id_ & 0xfffff0ffU; }
void CCANMsg::SetID(std::uint32_t id) { id_ = id; }
std::uint32_t CCANMsg::GetID() const { return id_; }
std::uint32_t CCANMsg::GetStandardID() const { return extended_ ? (id_ >> 18) : id_; }
void CCANMsg::SetExtended(bool value) { extended_ = value; }
bool CCANMsg::IsExtended() const { return extended_; }
bool CCANMsg::IsStandard() const { return !extended_; }
void CCANMsg::SetRemote(bool value) { remote_ = value; }
bool CCANMsg::IsRemote() const { return remote_; }
bool CCANMsg::IsData() const { return !remote_; }
std::uint32_t CCANMsg::GetDLC() const { return dlc_; }
void CCANMsg::SetDLC(std::uint32_t value) { dlc_ = value; }
void CCANMsg::SetFilterType(FILTER_TYPE value) { filterType_ = value; }
CCANMsg::FILTER_TYPE CCANMsg::GetFilterType() const { return filterType_; }
bool CCANMsg::IsUnfilteredMsg() const { return filterType_ == FILTER_TYPE_1 || filterType_ == FILTER_TYPE_3; }

bool CCANMsg::IsPOPMsg() const {
    std::uint32_t value = id_;
    if (extended_) value >>= 18;
    return (value & POP_MASK) == POP_ID;
}

bool CCANMsg::StdIDMatches(std::uint32_t value) const {
    std::uint32_t id = id_;
    if (extended_) id >>= 18;
    return (id & ID_MATCH_MASK) == value;
}

std::uint8_t CCANMsg::GetNodeFromServiceMessage() const { return static_cast<std::uint8_t>((id_ >> 8) & 0x0fU); }

void CCANMsg::InsertNodeIntoServiceMessage(std::uint8_t node) {
    id_ = (id_ & 0xfffff0ffU) | (static_cast<std::uint32_t>(node) << 8);
}

bool CCANMsg::IsModeChangeMsg() const {
    return !extended_ && ((id_ & ID_MATCH_MASK) == MODE_CHANGE_ID) && data_[0] != 0x90;
}

bool CCANMsg::IsProfileChangeMsg() const { return !extended_ && ((id_ & ID_MATCH_MASK) == PROFILE_CHANGE_ID); }
bool CCANMsg::IsSlotChangedMsg() const { return GetStandardID() == SLOT_CHANGED_STD_ID; }

void CCANMsg::GetData(std::uint8_t& value) const { value = data_[0]; }
void CCANMsg::GetData(std::uint16_t& value) const {
    value = static_cast<std::uint16_t>(data_[0]) | (static_cast<std::uint16_t>(data_[1]) << 8);
}
void CCANMsg::GetData(std::uint32_t& value) const {
    value = static_cast<std::uint32_t>(data_[0]) |
            (static_cast<std::uint32_t>(data_[1]) << 8) |
            (static_cast<std::uint32_t>(data_[2]) << 16) |
            (static_cast<std::uint32_t>(data_[3]) << 24);
}

void CCANMsg::SetServerNode(std::uint8_t node) { server_ = node; }
void CCANMsg::SetClientNode(std::uint8_t node) { client_ = node; }
void CCANMsg::SetClientServerNodes(std::uint8_t client, std::uint8_t server) { client_ = client; server_ = server; }
std::uint8_t CCANMsg::GetClientNode() { return client_; }
std::uint8_t CCANMsg::GetServerNode() { return server_; }

bool CCANMsg::Convert(const CRawMsg& raw) {
    if (!raw.IsCANMsg()) return false;

    data_.fill(0);
    id_ = INVALID_ID;
    extended_ = (raw[CRawMsg::CAN_EXT_INDEX] & CRawMsg::CAN_EXT_MASK) == CRawMsg::CAN_EXT_MASK;
    remote_ = false;
    dlc_ = 0;
    filterType_ = FILTER_TYPE_4;

    if (extended_) {
        remote_ = (raw[CRawMsg::CAN_EXT_REM_INDEX] & CRawMsg::CAN_EXT_REM_MASK) == CRawMsg::CAN_EXT_REM_MASK;
        std::uint32_t encoded =
            (static_cast<std::uint32_t>(raw[1]) << 24) |
            (static_cast<std::uint32_t>(raw[2]) << 16) |
            (static_cast<std::uint32_t>(raw[3]) << 8) |
             static_cast<std::uint32_t>(raw[4]);
        const std::uint32_t upper = (encoded >> 2) & 0x3ff80000U;
        const std::uint32_t lower = encoded & 0x0007fffeU;
        id_ = (upper | lower) >> 1;
    } else {
        remote_ = (raw[CRawMsg::CAN_STD_REM_INDEX] & CRawMsg::CAN_STD_REM_MASK) == CRawMsg::CAN_STD_REM_MASK;
        id_ = (static_cast<std::uint32_t>(raw[1]) << 3) | (static_cast<std::uint32_t>(raw[2]) >> 5);
    }

    dlc_ = raw[13] & CRawMsg::CAN_DLC_MASK;
    for (std::uint32_t i = 0; i < dlc_; ++i) data_[i] = raw[5 + static_cast<int>(i)];

    if (raw[1] == 0x01) filterType_ = FILTER_TYPE_0;
    else if (raw[1] == 0x10) filterType_ = FILTER_TYPE_1;
    else filterType_ = ((raw[1] & 0xfc) == 0xf0) ? FILTER_TYPE_2 : FILTER_TYPE_3;
    return true;
}

CPOPMsg::CPOPMsg() : CCANMsg() { ClearMsg(); }
CPOPMsg::CPOPMsg(const CCANMsg& msg) : CCANMsg(msg) {}
CPOPMsg::CPOPMsg(POP_MSG_TYPE type) : CCANMsg() { SetType(type); }
CPOPMsg::CPOPMsg(POP_MSG_TYPE type, std::uint32_t odi, std::uint32_t size, std::uint32_t block) : CCANMsg() {
    SetType(type);
    SetODI(odi);
    SetSize(size);
    SetBlock(block);
}

void CPOPMsg::SetODI(std::uint32_t value) {
    data_[1] = static_cast<std::uint8_t>(value);
    data_[2] = static_cast<std::uint8_t>(value >> 8);
    data_[3] = static_cast<std::uint8_t>(value >> 16);
}
std::uint32_t CPOPMsg::GetODI() const {
    return static_cast<std::uint32_t>(data_[1]) |
           (static_cast<std::uint32_t>(data_[2]) << 8) |
           (static_cast<std::uint32_t>(data_[3]) << 16);
}
void CPOPMsg::SetSize(std::uint32_t value) {
    data_[4] = static_cast<std::uint8_t>(value);
    data_[5] = static_cast<std::uint8_t>(value >> 8);
    data_[6] = static_cast<std::uint8_t>(value >> 16);
}
std::uint32_t CPOPMsg::GetSize() const {
    return static_cast<std::uint32_t>(data_[4]) |
           (static_cast<std::uint32_t>(data_[5]) << 8) |
           (static_cast<std::uint32_t>(data_[6]) << 16);
}
void CPOPMsg::SetBlock(std::uint32_t value) { data_[7] = static_cast<std::uint8_t>(value); }
std::uint32_t CPOPMsg::GetBlock() const { return data_[7]; }

bool CPOPMsg::IsPOPSegMsg() const { return IsPOPMsg() && IsExtended(); }
bool CPOPMsg::IsSegMsg() const { return IsExtended(); }
bool CPOPMsg::IsServerMsg() const { return ((GetStandardID() >> 4) & 1U) != 0; }
bool CPOPMsg::IsClientMsg() const { return !IsServerMsg(); }

std::uint8_t CPOPMsg::GetTransferCode() const {
    if (!IsPOPMsg()) return TC_ILLEGAL;
    return IsExtended() ? static_cast<std::uint8_t>((id_ >> POP_SEG_TC_SHIFT) & 0x03U)
                        : static_cast<std::uint8_t>(data_[0] >> POP_QCK_TC_SHIFT);
}

bool CPOPMsg::SetTransferCode(std::uint8_t value) {
    if (value > 3) return false;
    if (IsExtended()) id_ = (id_ & ~POP_SEG_TC_MASK) | (static_cast<std::uint32_t>(value) << POP_SEG_TC_SHIFT);
    else data_[0] = static_cast<std::uint8_t>((data_[0] & POP_QCK_TC_MASK) | (value << POP_QCK_TC_SHIFT));
    return true;
}

bool CPOPMsg::IsLastSegment() const { return IsPOPMsg() && IsExtended() && GetTransferCode() == 3; }
bool CPOPMsg::IsAbortMsg() const { return !IsExtended() && IsPOPMsg() && GetTransferCode() == TC_ABORT; }

void CPOPMsg::SetQuickFlag(bool enabled) { if (enabled) data_[0] |= QUICK_BIT; else data_[0] &= static_cast<std::uint8_t>(~QUICK_BIT); }
bool CPOPMsg::GetQuickFlag() const { return (data_[0] & QUICK_BIT) == QUICK_BIT; }
void CPOPMsg::SetCRCFlag(bool enabled) { if (enabled) data_[0] |= CRC_BIT; else data_[0] &= static_cast<std::uint8_t>(~CRC_BIT); }
bool CPOPMsg::GetCRCFlag() const { return (data_[0] & CRC_BIT) == CRC_BIT; }

void CPOPMsg::SetCRC(std::uint16_t crc) {
    data_[4] = static_cast<std::uint8_t>(crc);
    data_[5] = static_cast<std::uint8_t>(crc >> 8);
}
std::uint32_t CPOPMsg::GetAbortCode() const {
    return static_cast<std::uint32_t>(data_[4]) |
           (static_cast<std::uint32_t>(data_[5]) << 8) |
           (static_cast<std::uint32_t>(data_[6]) << 16) |
           (static_cast<std::uint32_t>(data_[7]) << 24);
}

std::uint32_t CPOPMsg::GetSegmentNumber() const { return id_ & SEGMENT_MASK; }
void CPOPMsg::SetSegmentNumber(std::uint32_t segment) { id_ = (id_ & 0xffff0000U) | (segment & SEGMENT_MASK); }
const std::uint8_t* CPOPMsg::GetDataBuffer() const { return data_.data(); }
std::uint32_t CPOPMsg::GetDataPOPSeg(std::uint8_t* out) const {
    for (std::uint32_t i = 0; i < dlc_; ++i) out[i] = data_[i];
    return dlc_;
}
CPOPMsg& CPOPMsg::GetDataFrom(const CCANMsg& msg) { static_cast<CCANMsg&>(*this) = msg; return *this; }

std::uint8_t CPOPMsg::GetNodeFromID() const { return static_cast<std::uint8_t>(GetStandardID() & 0x0fU); }
void CPOPMsg::SetNodeToID(std::uint8_t node) {
    if (!IsExtended()) id_ ^= (id_ ^ static_cast<std::uint32_t>(node)) & 0x0fU;
    else id_ ^= (id_ ^ (static_cast<std::uint32_t>(node) << POP_SEG_NODE_SHIFT)) & POP_SEG_NODE_MASK;
}
std::uint8_t CPOPMsg::GetNodeFromData() const {
    if (IsPOPMsg() && !IsExtended()) return data_[0] & CODE_DATA_NODE_MASK;
    return 0xff;
}
void CPOPMsg::SetNodeToData(std::uint8_t node) {
    if (!IsExtended() && node < 16) data_[0] = static_cast<std::uint8_t>((data_[0] & 0xf0) | node);
}

void CPOPMsg::SetClientServerToMsg(std::uint8_t client, std::uint8_t server) {
    if (!IsPOPMsg()) return;
    if (IsClientMsg()) {
        SetNodeToID(client);
        if (!IsExtended()) SetNodeToData(server);
    } else {
        SetNodeToID(server);
        if (!IsExtended()) SetNodeToData(client);
    }
}
void CPOPMsg::SetClientToMsg(std::uint8_t client) {
    if (!IsPOPMsg() || client >= 16) return;
    if (IsClientMsg()) SetNodeToID(client);
    else if (!IsExtended()) SetNodeToData(client);
}
void CPOPMsg::SetServerToMsg(std::uint8_t server) {
    if (!IsPOPMsg()) return;
    if (IsServerMsg()) SetNodeToID(server);
    else if (!IsExtended() && server < 16) SetNodeToData(server);
}
std::uint8_t CPOPMsg::GetClientFromMsg() const {
    if (!IsPOPMsg()) return 0xff;
    if (IsClientMsg()) return GetNodeFromID();
    if (!IsExtended()) return GetNodeFromData();
    return 0xff;
}
std::uint8_t CPOPMsg::GetServerFromMsg() const {
    if (!IsPOPMsg()) return 0xff;
    if (IsServerMsg()) return GetNodeFromID();
    if (!IsExtended()) return GetNodeFromData();
    return 0xff;
}

void CPOPMsg::SetType(POP_MSG_TYPE type) {
    ClearMsg();
    switch (static_cast<std::uint32_t>(type)) {
    case 2:  data_[0] = static_cast<std::uint8_t>((data_[0] & 0x0f) | 0xc0); extended_ = false; id_ = 0x780; dlc_ = 8; break;
    case 3:  data_[0] = static_cast<std::uint8_t>((data_[0] & 0x0f) | 0x20); extended_ = false; id_ = 0x780; dlc_ = 8; break;
    case 5:  data_[0] = static_cast<std::uint8_t>((data_[0] & 0x0f) | 0x40); extended_ = false; id_ = 0x780; dlc_ = 8; break;
    case 7:  data_[0] = static_cast<std::uint8_t>((data_[0] & 0x0f) | 0x10); extended_ = false; id_ = 0x780; dlc_ = 8; break;
    case 9:  extended_ = true; id_ = 0x1e000000U; break;
    case 10: extended_ = true; id_ = 0x1e010000U; break;
    case 12: data_[0] = static_cast<std::uint8_t>((data_[0] & 0x0f) | 0x80); extended_ = false; id_ = 0x780; dlc_ = 8; break;
    case 14: data_[0] = static_cast<std::uint8_t>((data_[0] & 0x0f) | 0x50); extended_ = false; id_ = 0x780; dlc_ = 8; break;
    case 17: extended_ = true; id_ = 0x1e030000U; dlc_ = 0; break;
    case 20: data_[0] = static_cast<std::uint8_t>((data_[0] & 0x0f) | 0x80); extended_ = false; id_ = 0x780; dlc_ = 8; break;
    default: break;
    }
    SetClientServerToMsg(GetClientNode(), GetServerNode());
}

CUSBMsg::CUSBMsg() { ClearData(); }
CUSBMsg::CUSBMsg(const CRawMsg& raw) { ClearData(); GetFromRaw(raw); }

void CUSBMsg::ClearData() { command_ = 0; subCommand_ = 0; data_.fill(0); }
void CUSBMsg::GetFromRaw(const CRawMsg& raw) {
    command_ = raw[1];
    subCommand_ = raw[2];
    for (std::size_t i = 0; i < DATA_LENGTH; ++i) data_[i] = raw[static_cast<int>(3 + i)];
}
std::uint8_t CUSBMsg::operator[](int index) const { return data_[static_cast<std::size_t>(index)]; }
std::uint8_t& CUSBMsg::operator[](int index) { return data_[static_cast<std::size_t>(index)]; }
std::uint8_t CUSBMsg::GetCommand() const { return command_; }
void CUSBMsg::SetCommand(std::uint8_t value) { command_ = value; }
std::uint8_t CUSBMsg::GetSubCommand() const { return subCommand_; }
void CUSBMsg::SetSubCommand(std::uint8_t value) { subCommand_ = value; }
std::uint8_t CUSBMsg::GetByteData(int index) const { return data_[static_cast<std::size_t>(index)]; }
std::uint16_t CUSBMsg::GetWordData(int index) const {
    const auto i = static_cast<std::size_t>(index);
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(data_[i]) << 8) | data_[i + 1]);
}
std::uint32_t CUSBMsg::GetDWordData(int index) const {
    const auto i = static_cast<std::size_t>(index);
    return (static_cast<std::uint32_t>(data_[i]) << 24) |
           (static_cast<std::uint32_t>(data_[i + 1]) << 16) |
           (static_cast<std::uint32_t>(data_[i + 2]) << 8) |
            static_cast<std::uint32_t>(data_[i + 3]);
}
std::uint64_t CUSBMsg::GetQWordData(int index) const {
    std::uint64_t value = 0;
    const auto i = static_cast<std::size_t>(index);
    for (std::size_t n = 0; n < 8; ++n) value = (value << 8) | data_[i + n];
    return value;
}
const std::uint8_t* CUSBMsg::GetDataPointer() const { return data_.data(); }
bool CUSBMsg::MatchesCommand(std::uint8_t c, std::uint8_t s) const { return command_ == c && subCommand_ == s; }
bool CUSBMsg::IsStatus_0() const { return MatchesCommand(CMD_STATUS, SUB_STATUS_0); }
bool CUSBMsg::IsSystemConnected() const { return data_[1] == ATTACHED || data_[1] == ATTACHING; }
bool CUSBMsg::IsSystemAttached() const { return data_[1] == ATTACHED; }

#define SET_CMD(name, cmd, sub) void CUSBMsg::name() { command_ = (cmd); subCommand_ = (sub); }
SET_CMD(SetCmdReadService, USBCMD_SERVICE, SVC_PARAM_R)
SET_CMD(SetCmdClearService, USBCMD_SERVICE, SVC_PARAM_W)
SET_CMD(SetCmdReadXYCount, USBCMD_SERVICE, SVC_XY_CNT_R)
SET_CMD(SetCmdReadKeys, USBCMD_SERVICE, SVC_KEYS_R)
SET_CMD(SetCmdReadLiveFaultOrganID, USBCMD_SERVICE, SVC_FAULT_R)
SET_CMD(SetCmdWriteLiveFaultOrganIDClear, USBCMD_SERVICE, SVC_FAULT_W)
SET_CMD(SetCmdReadErrors, USBCMD_MODES, MDE_ERRS_R)
SET_CMD(SetCmdClearErrors, USBCMD_MODES, MDE_ERRS_W)
SET_CMD(SetCmdReadUSBTickleTime, USBCMD_MODES, MDE_AUTOTX_R)
SET_CMD(SetCmdPing, USBCMD_MODES, MDE_PING)
SET_CMD(SetCmdReadLEDTimeout, USBCMD_LED, LED_TIMEOUT_R)
SET_CMD(SetCmdReadFW, USBCMD_CPU, CPU_FWVER_R)
SET_CMD(SetCmdReadStatus, USBCMD_CPU, CPU_STATUS_R)
SET_CMD(SetCmdReadSleepDisable, USBCMD_CPU, CPU_SLP_DISABLE_R)
SET_CMD(SetCmdReadRealTimeClock, USBCMD_SERVICE, SVC_RTC_R)
SET_CMD(SetCmdReadPassThrough, USBCMD_CPU, CPU_PASSTHRU_R)
SET_CMD(SetCmdReadFlashOverCAN, USBCMD_CPU, CPU_FLASH_R)
SET_CMD(SetCmdReadFilterRegisters, USBCMD_FILTER, FLT_FLT_R)
SET_CMD(SetCmdReadFilterMode, USBCMD_FILTER, FLT_MODE_R)
SET_CMD(SetCmdCPUReset, USBCMD_CPU, CPU_RESET_W)
SET_CMD(SetCmdCANReadSleep, USBCMD_CPU, CPU_CANSLP_R)
SET_CMD(SetCmdCANWriteSleep, USBCMD_CPU, CPU_CANSLP_W)
SET_CMD(SetCmdReadAwake, USBCMD_CPU, CPU_CANSLP_R)
SET_CMD(SetCmdReadConduit, USBCMD_CONDUIT, CONDUIT_R)
#undef SET_CMD

void CUSBMsg::SetCmdWriteXYCount(std::uint8_t value) { command_ = USBCMD_SERVICE; subCommand_ = SVC_XY_CNT_W; data_[0] = value; }
void CUSBMsg::SetCmdWriteFilterRegisters(std::uint8_t a0, std::uint8_t a1, std::uint8_t a2, std::uint8_t a3,
                                         std::uint8_t a4, std::uint8_t a5, std::uint8_t a6, std::uint8_t a7, std::uint8_t a8) {
    command_ = USBCMD_FILTER; subCommand_ = FLT_FLT_W;
    data_[0]=a0; data_[1]=a1; data_[2]=a2; data_[3]=a3; data_[4]=a4; data_[5]=a5; data_[6]=a6; data_[7]=a7; data_[8]=a8;
}
void CUSBMsg::SetCmdWriteFilterMode(std::uint8_t value) { command_ = USBCMD_FILTER; subCommand_ = FLT_MODE_W; data_[0] = value; }
void CUSBMsg::SetCmdWriteConduit(bool enabled) { command_ = USBCMD_CONDUIT; subCommand_ = CONDUIT_W; data_[0] = enabled ? 1 : 0; }
void CUSBMsg::SetCmdWriteAwake(bool enabled) { command_ = USBCMD_CPU; subCommand_ = CPU_CANSLP_W; data_[0] = enabled ? 1 : 0; }
void CUSBMsg::SetCmdReadEEPROM(std::uint8_t address) { command_ = USBCMD_E2; subCommand_ = E2_ACCESS_R; data_[0] = address; }
void CUSBMsg::SetCmdWriteUSBTickleTime(std::uint16_t value) { command_ = USBCMD_MODES; subCommand_ = MDE_AUTOTX_W; data_[0]=static_cast<std::uint8_t>(value>>8); data_[1]=static_cast<std::uint8_t>(value); }
void CUSBMsg::SetCmdWriteSleepDisable(bool enabled) { command_ = USBCMD_CPU; subCommand_ = CPU_SLP_DISABLE_W; data_[0] = enabled ? 1 : 0; }
void CUSBMsg::SetCmdWritePassThrough(bool enabled) { command_ = USBCMD_CPU; subCommand_ = CPU_PASSTHRU_W; data_[0] = enabled ? 1 : 0; }
void CUSBMsg::SetCmdWriteFlashOverCAN(bool enabled) { command_ = USBCMD_CPU; subCommand_ = CPU_FLASH_W; data_[0] = enabled ? 1 : 0; }

static void EncodeLedTimeout(std::array<std::uint8_t, CUSBMsg::DATA_LENGTH>& data, std::size_t base, std::uint16_t value) {
    if (value == 0) { data[base]=0; data[base+1]=0; data[base+2]=0; }
    else if (value == 0xffff) { data[base]=0; data[base+1]=0; data[base+2]=1; }
    else { data[base]=static_cast<std::uint8_t>(value>>8); data[base+1]=static_cast<std::uint8_t>(value); data[base+2]=0; }
}
void CUSBMsg::SetCmdWriteLEDTimeout(std::uint16_t first, std::uint16_t second) {
    command_ = USBCMD_LED; subCommand_ = LED_TIMEOUT_W;
    EncodeLedTimeout(data_, 0, first);
    EncodeLedTimeout(data_, 3, second);
}
void CUSBMsg::SetCmdWriteEEPROM(std::uint8_t address, std::uint16_t value) {
    command_ = USBCMD_E2; subCommand_ = E2_ACCESS_W;
    data_[0] = address; data_[1] = static_cast<std::uint8_t>(value >> 8); data_[2] = static_cast<std::uint8_t>(value);
}

CCANMsgResetTimer::CCANMsgResetTimer() : CCANMsg() {
    id_ = 0x1c240f01U;
    extended_ = true;
    remote_ = false;
    dlc_ = 0;
}

} // namespace dongle_recovered
