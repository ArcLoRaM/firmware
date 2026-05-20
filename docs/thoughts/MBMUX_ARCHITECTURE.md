# MbMux Dual-Core Communication Architecture
## STM32WL55 — SubGHz_Phy_PingPong_DualCore

---

## 1. Dual-Binary Model

The STM32WL55 embeds two independent cores:

| Core | Role | Flash origin | RAM |
|------|------|-------------|-----|
| **CPU1** (Cortex-M4) | User application | `0x08000000` | SRAM1 (private) + SRAM2 (shared) |
| **CPU2** (Cortex-M0+) | Radio driver / low-level stack | `0x08020000` | SRAM2 (shared + private) |

Two separate binaries are compiled and flashed independently. A function address from one binary is meaningless to the other binary. The MbMux system is the bridge: it lets CPU1 call functions that live on CPU2, and vice versa, using only integer IDs and shared SRAM.

---

## 2. Physical Memory Map

Derived from the two STM32CubeIDE linker scripts.

```
Address         Region              Owner       Section tags       Contents
─────────────────────────────────────────────────────────────────────────────────
0x08000000      FLASH 128 KB        CM4         .text/.rodata      CM4 code + constants
0x08020000      FLASH 128 KB        CM0+        .text/.rodata      CM0+ code + constants

0x20000000      SRAM1  32 KB        CM4 only    RAM1               CM4 .data / .bss / stack / heap
0x20007FFF

0x20008000      SRAM2   4 KB        CM4 owns    RAM_SHARED (CM4)   MAPPING_TABLE  ← MBSYS_RefTable
                                                                   MB_MEM1        ← parameter buffers
0x20008FFF

0x20009000      SRAM2   4 KB        CM0+ owns   RAM_SHARED (CM0+)  MB_MEM2        ← Feat_Info_Table
                                                                   MB_MEM3        ← trace buffer, Rx payload buf
0x20009FFF

0x2000A000      SRAM2  24 KB        CM0+ only   RAM2               CM0+ .data / .bss / stack / heap
0x2000FFFF
```

**Key rules enforced by the linker scripts:**
- Both cores can read/write the entire SRAM2 range (`0x20008000–0x2000FFFF`).
- CM4 cannot safely write into CM0+'s private SRAM2 range and vice versa — not hardware-enforced, but an architectural contract.
- Pointers passed across cores are valid only if they point into the shared SRAM2 region (or are passed as integer IDs, not pointer values).

---

## 3. The IPCCDBA Option Byte — Critical Hardware Constraint

### What it is

The IPCC hardware peripheral needs to know the physical address of `MBSYS_RefTable` (the MAPPING_TABLE) before any software runs. It reads this from a non-volatile **option byte** in Flash called `IPCCDBA`.

The formula that converts the option byte value to an address is:

```
IPCCdataBufAddr = SRAM1_BASE + (IPCCDBA × 16)
                = 0x20000000 + (IPCCDBA × 16)
```

The factory default `IPCCDBA = 0x800` resolves to:

```
0x20000000 + (0x800 × 16) = 0x20000000 + 0x8000 = 0x20008000
```

This matches the start of `RAM_SHARED` in the CM4 linker, which is exactly where `MAPPING_TABLE` is placed.

### The auto-reprogram safeguard

`MBMUXIF_SystemInit()` checks at runtime whether the option byte matches the actual linker placement:

```c
// CM4/MbMux/mbmuxif_sys.c
if (OptionsBytesStruct.IPCCdataBufAddr != (uint32_t) pMb_RefTable)
{
    // reprogram the option byte → triggers a hardware reset
}
```

**If you use the provided linker files unmodified**, `MAPPING_TABLE` lands at `0x20008000`, which equals the factory-default IPCCDBA. The condition is false and the block never executes.

**The block executes only if** you use a linker script (e.g. a raw CubeMX output) that does not define a `MAPPING_TABLE` section, causing `MBSYS_RefTable` to land at an arbitrary address. In that case: the code reprograms the option byte, the chip resets once, and from the second boot onwards everything works. The chip revision check (`ChipRevId == 0x1001`) before reprogramming is a silicon errata workaround for cut 1.1 — it temporarily lowers the MSI clock to ≤16 MHz before writing Flash option bytes.

> **Rule:** Never move `RAM_SHARED` in the CM4 linker without also updating `IPCCDBA`. The simplest approach is to leave the linker files exactly as provided.

---

## 4. Shared Memory Structure

### 4.1 The MAPPING_TABLE — `MBMUX_ComTable_t`

Declared in `CM4/MbMux/mbmuxif_sys.c:60` and placed at the start of `RAM_SHARED` (address `0x20008000`):

```c
UTIL_MEM_PLACE_IN_SECTION("MAPPING_TABLE")
static MBMUX_ComTable_t MBSYS_RefTable UTIL_MEM_ALIGN(16);
```

The struct layout (`Common/MbMux/mbmux_table.h`):

```c
typedef struct {
    MBMUX_ComParam_t  MBCmdRespParam[6];   // one slot per IPCC channel, Cmd direction
    MBMUX_ComParam_t  MBNotifAckParam[6];  // one slot per IPCC channel, Notif direction
    uint8_t           MBMUXMapping[11][2]; // [featureId][direction] → IPCC channel index
    __IO uint16_t     SynchronizeCpusAtBoot;
    uint16_t          ChipRevId;
} MBMUX_ComTable_t;   // 316 bytes, placed at 0x20008000
```

`MBMUXMapping` is the routing table. After registration, `MBMUXMapping[FEAT_INFO_RADIO_ID][MBMUX_CMD_RESP]` contains the IPCC channel number (e.g. `1`) that carries Radio Cmd/Resp traffic. This is how MBMUX knows which IPCC channel to signal for a given feature.

### 4.2 The Communication Envelope — `MBMUX_ComParam_t`

Every inter-core call — in both directions — is carried by exactly one of these slots:

```c
typedef struct {
    uint32_t  MsgId;       // integer identifying which function to call
    void (*MsgCm4Cb)(void *ComObj);      // CM4 ISR callback (set at registration)
    void (*MsgCm0plusCb)(void *ComObj);  // not used directly; moved to local table
    uint16_t  BufSize;     // total byte capacity of ParamBuf
    uint16_t  ParamCnt;    // number of words written into ParamBuf for this call
    uint32_t *ParamBuf;    // pointer into MB_MEM1 — the flat argument array
    uint32_t  ReturnVal;   // return value written by the receiver
} MBMUX_ComParam_t;
```

`ParamBuf` points to a statically allocated `uint32_t[]` array that lives in `MB_MEM1` (shared SRAM2). This is where function arguments travel.

### 4.3 MB_MEM1 — Parameter Buffers

Declared in CM4 sources, placed in shared SRAM2 immediately after `MAPPING_TABLE`:

```c
// CM4/MbMux/mbmuxif_radio.c
UTIL_MEM_PLACE_IN_SECTION("MB_MEM1") uint32_t aRadioCmdRespBuff[15];  // 60 B
UTIL_MEM_PLACE_IN_SECTION("MB_MEM1") uint32_t aRadioNotifAckBuff[4];  // 16 B

// CM4/MbMux/mbmuxif_sys.c
UTIL_MEM_PLACE_IN_SECTION("MB_MEM1") uint32_t aSystemCmdRespBuff[7];  // 28 B
UTIL_MEM_PLACE_IN_SECTION("MB_MEM1") uint32_t aSystemNotifAckBuff[5]; // 20 B
// ... priority sub-channels, trace buffer ...
```

Each feature gets its own dedicated `uint32_t[]` arrays. The array size determines the maximum number of arguments a single call can carry.

### 4.4 MB_MEM2 and MB_MEM3 — CM0+ Owned Shared Memory

Declared on the CM0+ side, placed in CM0+'s `RAM_SHARED` region (`0x20009000`):

```c
// CM0PLUS/MbMux/features_info.c
UTIL_MEM_PLACE_IN_SECTION("MB_MEM2") FEAT_INFO_Param_t Feat_Info_Table[];  // 80 B
UTIL_MEM_PLACE_IN_SECTION("MB_MEM2") FEAT_INFO_List_t  Feat_Info_List;     //  8 B

// CM0PLUS/MbMux/radio_mbwrapper.c
UTIL_MEM_PLACE_IN_SECTION("MB_MEM3") uint8_t aRadioMbWrapRxBuffer[256];   // actual RX payload
```

CM1 reads `Feat_Info_Table` at boot to discover which features CPU2 supports and their version numbers.

---

## 5. Features and Message IDs

### 5.1 Feature IDs — `FEAT_INFO_IdTypeDef`

Defined in `Common/MbMux/features_info.h`. **Positions are frozen** — both cores are compiled separately and must agree on the integer values:

```c
typedef enum {
    FEAT_INFO_SYSTEM_ID             = 0,   // always active, always on IPCC channel 0
    FEAT_INFO_SYSTEM_CMD_PRIO_A_ID  = 1,   // high-priority RTC sub-channel
    FEAT_INFO_SYSTEM_NOTIF_PRIO_A_ID= 2,
    FEAT_INFO_SYSTEM_CMD_PRIO_B_ID  = 3,
    FEAT_INFO_SYSTEM_NOTIF_PRIO_B_ID= 4,
    FEAT_INFO_KMS_ID                = 5,   // key management (inactive here)
    FEAT_INFO_TRACE_ID              = 6,   // Notif-only: CM0+ → CM4 log strings
    FEAT_INFO_RADIO_ID              = 7,   // active in this project
    FEAT_INFO_LORAWAN_ID            = 8,   // inactive in this project
    FEAT_INFO_SIGFOX_ID             = 9,
    FEAT_INFO_WMBUS_ID              = 10,
    // USER CODE: add new IDs at the bottom only
    FEAT_INFO_CNT                          // must remain last
} FEAT_INFO_IdTypeDef;
```

### 5.2 Feature Capability Table — `features_info.c` (CM0+ side)

CM0+ declares which features it supports in `CM0PLUS/MbMux/features_info.c`. This table lives in `MB_MEM2` and is read by CM4 at boot:

```c
UTIL_MEM_PLACE_IN_SECTION("MB_MEM2") FEAT_INFO_Param_t Feat_Info_Table[] = {
    { FEAT_INFO_SYSTEM_ID,              APP_VERSION,       0, NULL },
    { FEAT_INFO_SYSTEM_NOTIF_PRIO_A_ID, APP_VERSION,       0, NULL },
    { FEAT_INFO_TRACE_ID,               APP_VERSION,       0, NULL },
    { FEAT_INFO_RADIO_ID,               SUBGHZ_PHY_VERSION,0, NULL },
    // add your new feature here
};
```

A feature absent from this table will cause `MBMUX_RegisterFeature()` on CM4 to return `-2`.

### 5.3 Message IDs — `msg_id.h`

Each feature defines its own integer enum of message IDs in `Common/MbMux/msg_id.h`. The two directions are grouped within the same enum:

```c
typedef enum {
    // ---- Cmd/Resp (CM4 → CM0+) ----
    RADIO_INIT_ID = 0,
    RADIO_GET_STATUS_ID,
    RADIO_SET_MODEM_ID,
    RADIO_SET_CHANNEL_ID,
    RADIO_SEND_ID,
    RADIO_RX_ID,
    // ... (30 total Cmd IDs)

    // ---- Notif/Ack (CM0+ → CM4) ----
    RADIO_TX_DONE_CB_ID,
    RADIO_TX_TIMEOUT_CB_ID,
    RADIO_RX_DONE_CB_ID,
    RADIO_RX_TIMEOUT_CB_ID,
    RADIO_RX_ERROR_CB_ID,
    // ... (7 total Notif IDs)

    RADIO_MSGID_LAST
} Radio_MsgIdTypeDef;
```

The MsgId is only meaningful within its feature. `RADIO_INIT_ID = 0` and (hypothetically) `MYFEATURE_INIT_ID = 0` can coexist because they live on different IPCC channels.

---

## 6. Runtime Data Flow

### 6.1 Cmd/Resp — CM4 calls a function on CM0+

CM4 **blocks** (enters `UTIL_SEQ_WaitEvt`) until CM0+ responds.

```
CM4 application
  └─ myFunc(a, b, c)
       │
       ▼  CM4/MbMux/myfeature_mbwrapper.c
       com_obj = MBMUXIF_GetMyFeatureCmdComPtr()    // get the ComParam slot
       com_obj->MsgId    = MYFEAT_DO_SOMETHING_ID   // which function
       com_obj->ParamBuf[0] = (uint32_t) a          // arg 1 (scalar or pointer)
       com_obj->ParamBuf[1] = (uint32_t) b          // arg 2
       com_obj->ParamBuf[2] = (uint32_t) c          // arg 3
       com_obj->ParamCnt = 3
       MBMUXIF_MyFeatureSendCmd()
         │
         ▼  CM4/MbMux/mbmux.c
         MBMUX_CommandSnd(FEAT_INFO_MYFEAT_ID)
           IPCC_IF_SendCmd(channel)
             HAL_IPCC_NotifyCPU2()     ←── IPCC HW fires C2 RX interrupt
             UTIL_SEQ_WaitEvt(...)     ←── CM4 suspends
                                                    │
                                                    ▼  CM0PLUS/Core/Src/ipcc_if.c
                                                    IpccIfIsrRxCb(channel)
                                                    IpccCommandRcv(channel)
                                                      │
                                                      ▼  CM0PLUS/MbMux/mbmux.c
                                                      MsgCm0plusCb(ComObj)
                                                        │
                                                        ▼  CM0PLUS/MbMux/mbmuxif_myfeat.c
                                                        UTIL_SEQ_SetTask(Process task)
                                                          │
                                                          ▼  CM0PLUS/MbMux/myfeature_mbwrapper.c
                                                          Process_MyFeature_Cmd(ComObj)
                                                            switch (ComObj->MsgId)
                                                            case MYFEAT_DO_SOMETHING_ID:
                                                              a = com_buffer[0]
                                                              b = com_buffer[1]
                                                              c = com_buffer[2]
                                                              result = real_DoSomething(a,b,c)
                                                              ComObj->ReturnVal = result
                                                            MBMUXIF_MyFeatureSendResp()
                                                              IPCC_IF_NotifyCpu1(channel)
                                                                HAL_IPCC fires C1 RX interrupt
             UTIL_SEQ_SetEvt(...)     ←── CM4 wakes
       result = com_obj->ReturnVal    // read return value
       return result
```

### 6.2 Notif/Ack — CM0+ calls a callback on CM4

CM0+ drives this (e.g. radio reception completed). CM0+ does **not** block.

```
CM0+ radio ISR / timer
  └─ RadioRxDone_mbwrapper(payload, size, rssi, snr)
       │
       ▼  CM0PLUS/MbMux/radio_mbwrapper.c
       com_obj = MBMUX_GetFeatureComPtr(FEAT_INFO_RADIO_ID, MBMUX_NOTIF_ACK)
       com_obj->MsgId       = RADIO_RX_DONE_CB_ID
       com_obj->ParamBuf[0] = (uint32_t) payload  // pointer (must be in shared SRAM)
       com_obj->ParamBuf[1] = (uint32_t) size
       com_obj->ParamBuf[2] = (uint32_t) rssi
       com_obj->ParamBuf[3] = (uint32_t) snr
       MBMUXIF_RadioSendNotif()
         IPCC fires C1 RX interrupt
                                       CM4 ISR: MBMUXIF_IsrRadioNotifRcvCb()
                                         UTIL_SEQ_SetTask(MbRadioNotifRcv)
                                       [task runs]:
                                       Process_Radio_Notif(ComObj)   // radio_mbwrapper.c (CM4)
                                         case RADIO_RX_DONE_CB_ID:
                                           radioevents_wrap.RxDone(
                                               (uint8_t*) buf, size, rssi, snr)
                                           ComObj->ReturnVal = 0
                                       MBMUX_AcknowledgeSnd(FEAT_INFO_RADIO_ID)
                                         IPCC_IF_NotifyCpu2()
```

> **Pointer rule:** If you pass a pointer as an argument (as in `RADIO_RX_DONE_CB_ID` above), the data it points to **must** reside in shared SRAM2 (`0x20008000–0x2000FFFF`). A pointer into CM4-private SRAM1 would be dereferenced by CM0+ with undefined results. For this reason `aRadioMbWrapRxBuffer` is placed in `MB_MEM3`.

---

## 7. Adding a Custom Feature — Step-by-Step

The following checklist adds a feature called `MYFEAT` with two messages: a Cmd/Resp (`MYFEAT_COMPUTE_ID`) and a Notif/Ack (`MYFEAT_RESULT_READY_ID`).

### Step 1 — Assign a Feature ID

In `Common/MbMux/features_info.h`, append to `FEAT_INFO_IdTypeDef` **before** `FEAT_INFO_CNT`:

```c
/* USER CODE BEGIN FEAT_INFO_IdTypeDef */
FEAT_INFO_MYFEAT_ID,   /* = 11 : do not change position after first release */
/* USER CODE END FEAT_INFO_IdTypeDef */
FEAT_INFO_CNT
```

> Never insert in the middle. The integer values are baked into both binaries at compile time.

### Step 2 — Declare Message IDs

In `Common/MbMux/msg_id.h`, add a new enum:

```c
typedef enum {
    /* CmdResp */
    MYFEAT_COMPUTE_ID = 0,
    /* NotifAck */
    MYFEAT_RESULT_READY_ID,
    MYFEAT_MSGID_LAST
} MyFeat_MsgIdTypeDef;
```

Also add the parameter-count constants in `Common/MbMux/features_info.h`:

```c
#define MAX_PARAM_OF_MYFEAT_CMD_FUNCTIONS    4  // max args for any Cmd
#define MAX_PARAM_OF_MYFEAT_NOTIF_FUNCTIONS  2  // max args for any Notif
```

### Step 3 — Declare CM0+ Support

In `CM0PLUS/MbMux/features_info.c`, add to `Feat_Info_Table[]`:

```c
/* USER CODE BEGIN PV */
{
    .Feat_Info_Feature_Id      = FEAT_INFO_MYFEAT_ID,
    .Feat_Info_Feature_Version = APP_VERSION,
    .Feat_Info_Config_Size     = 0,
    .Feat_Info_Config_Ptr      = NULL
},
/* USER CODE END PV */
```

### Step 4 — Allocate Parameter Buffers

Add two arrays in `MB_MEM1` (owned by CM4). A natural place is alongside the other buffers in `CM4/MbMux/mbmuxif_sys.c` or in a new `mbmuxif_myfeat.c`:

```c
UTIL_MEM_PLACE_IN_SECTION("MB_MEM1")
uint32_t aMyFeatCmdRespBuff[MAX_PARAM_OF_MYFEAT_CMD_FUNCTIONS];    // shared

UTIL_MEM_PLACE_IN_SECTION("MB_MEM1")
uint32_t aMyFeatNotifAckBuff[MAX_PARAM_OF_MYFEAT_NOTIF_FUNCTIONS]; // shared
```

If your total `MB_MEM1` usage exceeds the `4K RAM_SHARED` budget, enlarge `LENGTH` in both linker files and adjust accordingly.

### Step 5 — CM4 MBMUXIF Layer

Create `CM4/MbMux/mbmuxif_myfeat.c` (and `.h`). Model it on `mbmuxif_radio.c`:

```c
static MBMUX_ComParam_t *MyFeatComObj;

int8_t MBMUXIF_MyFeatInit(void)
{
    int8_t ret;

    ret = MBMUX_RegisterFeature(FEAT_INFO_MYFEAT_ID, MBMUX_CMD_RESP,
                                MBMUXIF_IsrMyFeatRespRcvCb,
                                aMyFeatCmdRespBuff, sizeof(aMyFeatCmdRespBuff));
    if (ret >= 0)
        ret = MBMUX_RegisterFeature(FEAT_INFO_MYFEAT_ID, MBMUX_NOTIF_ACK,
                                    MBMUXIF_IsrMyFeatNotifRcvCb,
                                    aMyFeatNotifAckBuff, sizeof(aMyFeatNotifAckBuff));
    if (ret >= 0)
        UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_MbMyFeatNotifRcv),
                         UTIL_SEQ_RFU, MBMUXIF_TaskMyFeatNotifRcv);
    if (ret >= 0)
        ret = MBMUXIF_SystemSendCm0plusRegistrationCmd(FEAT_INFO_MYFEAT_ID);

    return ret;
}

MBMUX_ComParam_t *MBMUXIF_GetMyFeatCmdComPtr(void)
{
    return MBMUX_GetFeatureComPtr(FEAT_INFO_MYFEAT_ID, MBMUX_CMD_RESP);
}

void MBMUXIF_MyFeatSendCmd(void)
{
    if (MBMUX_CommandSnd(FEAT_INFO_MYFEAT_ID) == 0)
        UTIL_SEQ_WaitEvt(1 << CFG_SEQ_Evt_MbMyFeatRespRcv);
    else
        Error_Handler();
}

void MBMUXIF_MyFeatSendAck(void)
{
    MBMUX_AcknowledgeSnd(FEAT_INFO_MYFEAT_ID);
}

// ISR: just release the waiting task
static void MBMUXIF_IsrMyFeatRespRcvCb(void *ComObj)
{
    UTIL_SEQ_SetEvt(1 << CFG_SEQ_Evt_MbMyFeatRespRcv);
}

// ISR: schedule the task to process the Notif outside ISR context
static void MBMUXIF_IsrMyFeatNotifRcvCb(void *ComObj)
{
    MyFeatComObj = (MBMUX_ComParam_t *) ComObj;
    UTIL_SEQ_SetTask((1 << CFG_SEQ_Task_MbMyFeatNotifRcv), CFG_SEQ_Prio_0);
}

static void MBMUXIF_TaskMyFeatNotifRcv(void)
{
    Process_MyFeat_Notif(MyFeatComObj);
}
```

Register sequencer task/event IDs in `app_conf.h` alongside the existing ones.

### Step 6 — CM4 MbWrapper Layer

Create `CM4/MbMux/myfeat_mbwrapper.c`. This is the API surface your application code calls:

```c
// Cmd direction: CM4 calls CM0+
int32_t MyFeat_Compute(uint32_t inputA, uint32_t inputB)
{
    MBMUX_ComParam_t *com_obj = MBMUXIF_GetMyFeatCmdComPtr();
    com_obj->MsgId        = MYFEAT_COMPUTE_ID;
    com_obj->ParamBuf[0]  = inputA;
    com_obj->ParamBuf[1]  = inputB;
    com_obj->ParamCnt     = 2;
    MBMUXIF_MyFeatSendCmd();               // blocks until CM0+ responds
    return (int32_t) com_obj->ReturnVal;
}

// Notif direction: CM0+ calls CM4 — handle the incoming notification
void Process_MyFeat_Notif(MBMUX_ComParam_t *ComObj)
{
    switch (ComObj->MsgId)
    {
        case MYFEAT_RESULT_READY_ID:
            uint32_t result = ComObj->ParamBuf[0];
            MyApp_OnResultReady(result);   // your application callback
            ComObj->ReturnVal = 0;
            break;
        default:
            break;
    }
    MBMUXIF_MyFeatSendAck();
}
```

### Step 7 — CM0+ MBMUXIF Layer

Create `CM0PLUS/MbMux/mbmuxif_myfeat.c`. Model it on `CM0PLUS/MbMux/mbmuxif_radio.c`:

```c
int8_t MBMUXIF_MyFeatInit(void)
{
    int8_t ret;
    ret = MBMUX_RegisterFeatureCallback(FEAT_INFO_MYFEAT_ID, MBMUX_CMD_RESP,
                                        MBMUXIF_IsrMyFeatCmdRcvCb);
    if (ret >= 0)
        ret = MBMUX_RegisterFeatureCallback(FEAT_INFO_MYFEAT_ID, MBMUX_NOTIF_ACK,
                                            MBMUXIF_IsrMyFeatAckRcvCb);
    if (ret >= 0)
        UTIL_SEQ_RegTask((1 << CFG_SEQ_Task_MbMyFeatCmdRcv),
                         UTIL_SEQ_RFU, MBMUXIF_TaskMyFeatCmdRcv);
    return ret;
}

void MBMUXIF_MyFeatSendResp(void)
{
    MBMUX_ResponseSnd(FEAT_INFO_MYFEAT_ID);
}

void MBMUXIF_MyFeatSendNotif(void)
{
    MBMUX_NotificationSnd(FEAT_INFO_MYFEAT_ID);
}
```

### Step 8 — CM0+ MbWrapper Layer

Create `CM0PLUS/MbMux/myfeat_mbwrapper.c`:

```c
// Cmd direction: receive the call from CM4, execute the real function
void Process_MyFeat_Cmd(MBMUX_ComParam_t *ComObj)
{
    uint32_t *buf = ComObj->ParamBuf;

    switch (ComObj->MsgId)
    {
        case MYFEAT_COMPUTE_ID:
        {
            uint32_t a = buf[0];
            uint32_t b = buf[1];
            int32_t result = RealImpl_Compute(a, b);  // your CM0+ implementation
            ComObj->ParamCnt  = 0;
            ComObj->ReturnVal = (uint32_t) result;
            break;
        }
        default:
            break;
    }
    MBMUXIF_MyFeatSendResp();
}

// Notif direction: CM0+ wants to notify CM4 of an asynchronous event
void MyFeat_NotifyResultReady(uint32_t result)
{
    MBMUX_ComParam_t *com_obj =
        MBMUX_GetFeatureComPtr(FEAT_INFO_MYFEAT_ID, MBMUX_NOTIF_ACK);

    com_obj->MsgId       = MYFEAT_RESULT_READY_ID;
    com_obj->ParamBuf[0] = result;
    com_obj->ParamCnt    = 1;
    MBMUXIF_MyFeatSendNotif();
}
```

### Step 9 — Wire Up Initialization

**CM4** — in `CM4/Core/Src/sys_app.c`, inside the private `MBMUXIF_Init()`:

```c
init_status = MBMUXIF_MyFeatInit();
if (init_status < 0) Error_Handler();
```

Call this **after** `MBMUXIF_SystemInit()` and `WaitCm0MbmuxIsInitialized()`, but before `MBMUXIF_TraceInit()`/`MBMUXIF_RadioInit()` (order doesn't matter among non-system features).

**CM0+** — in `CM0PLUS/MbMux/mbmuxif_sys.c` inside the system registration handler, or in `CM0PLUS/Core/Src/sys_app.c`:

```c
init_status = MBMUXIF_MyFeatInit();
if (init_status < 0) Error_Handler();
```

---

## 8. Checklist Summary

| # | File | Change |
|---|------|--------|
| 1 | `Common/MbMux/features_info.h` | Add `FEAT_INFO_MYFEAT_ID` to enum, add `MAX_PARAM_OF_MYFEAT_*` constants |
| 2 | `Common/MbMux/msg_id.h` | Add `MyFeat_MsgIdTypeDef` enum |
| 3 | `CM0PLUS/MbMux/features_info.c` | Add entry in `Feat_Info_Table[]` |
| 4 | `CM4/MbMux/mbmuxif_myfeat.c/.h` | Registration, `SendCmd`, `SendAck`, ISR callbacks, SEQ task |
| 5 | `CM4/MbMux/myfeat_mbwrapper.c/.h` | Serialise Cmd args; `Process_MyFeat_Notif()` deserialises Notif |
| 6 | `CM0PLUS/MbMux/mbmuxif_myfeat.c/.h` | `RegisterFeatureCallback`, `SendResp`, `SendNotif`, SEQ task |
| 7 | `CM0PLUS/MbMux/myfeat_mbwrapper.c/.h` | `Process_MyFeat_Cmd()` deserialises; `MyFeat_NotifyResultReady()` serialises Notif |
| 8 | `CM4/Core/Src/sys_app.c` | Call `MBMUXIF_MyFeatInit()` |
| 9 | `CM0PLUS/Core/Src/sys_app.c` | Call `MBMUXIF_MyFeatInit()` |
| 10 | `app_conf.h` (both cores) | Add `CFG_SEQ_Task_MbMyFeat*` and `CFG_SEQ_Evt_MbMyFeat*` bit IDs |
| 11 | Linker scripts (if buffers exceed 4 KB) | Increase `LENGTH` of `RAM_SHARED` in both `.ld` files and verify IPCCDBA |

---

## 9. Constraints and Pitfalls

| Constraint | Detail |
|---|---|
| **Feature ID positions are frozen** | Never reorder `FEAT_INFO_IdTypeDef`. Both binaries are compiled independently; the integers must match. Only append. |
| **MsgId scoping** | MsgIds are per-feature only. The same integer can appear in different feature enums. |
| **Pointer arguments must be in shared SRAM2** | If you pass a `uint8_t *` across cores, the buffer must live in `0x20008000–0x2000FFFF`. Declare it with `UTIL_MEM_PLACE_IN_SECTION("MB_MEM1")` or `"MB_MEM3"`. |
| **One call at a time per feature** | There is one `ComParam` slot per feature per direction. Do not call `MBMUXIF_MyFeatSendCmd()` from two tasks concurrently. |
| **All arguments are `uint32_t`** | Cast everything to/from `uint32_t`. Sign-extend manually for signed types (`(int16_t) com_buffer[2]`). |
| **Notif handlers run in a SEQ task, not ISR** | `MBMUXIF_IsrXxxNotifRcvCb` runs in ISR context — only call `UTIL_SEQ_SetTask` there. Do real work in the task. |
| **IPCCDBA and linker coherence** | `MAPPING_TABLE` must always start at `0x20008000` (CM4 `RAM_SHARED` origin). Do not change `RAM_SHARED` in the CM4 linker without recalculating IPCCDBA. |
