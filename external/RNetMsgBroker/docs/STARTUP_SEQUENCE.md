# Observed R-Net startup sequence

The exact sender is not present in candump logs. `quelle` and `senke` are therefore best-effort roles inferred from frame families.

```mermaid
sequenceDiagram
    participant JSM as JSM / joystick module
    participant PM as PM / power module
    participant MOD as other modules
    participant BUS as CAN bus

    JSM->>BUS: RNetJsmCanConnectionTest
    JSM->>BUS: RNetJsmSerialHeartbeat
    JSM->>BUS: RNetJsmRequestSerialExchange
    PM->>BUS: RNetPmCommencingSerialExchangeRtr
    PM->>BUS: RNetSerialExchangeSeq0..7 RTR
    PM->>BUS: RNetSerialChallengeGenericRtr
    MOD-->>BUS: RNetSerialExchangeGeneric
    PM->>BUS: Config discovery / memory requests
    JSM->>BUS: POP parameter requests
    MOD-->>BUS: POP / profile / mode responses
    JSM->>BUS: UI active / joystick neutral
    PM-->>BUS: Heartbeat, motor, battery and runtime status
```

## Phases used in JSON

- `startup.serial_auth`
- `startup.config_discovery`
- `startup.profile_mode`
- `startup.parameter_exchange`
- `runtime.normal_operation`
- `runtime.drive`
- `runtime.ui`
- `unknown.observed`

## Tooling note

Mit v1.0.0 kann die verwendete Library-Version eindeutig dokumentiert werden:

```bash
RNetMsgBrokerTest --libversion
```
