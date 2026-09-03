#include "common.h"
#include <string.h>
#include <libtwl/rtos/rtosIrq.h>
#include <libtwl/rtos/rtosThread.h>
#include <libtwl/rtos/rtosEvent.h>
#include <libtwl/timer/timer.h>
#include <libtwl/ipc/ipcSync.h>
#include <libtwl/ipc/ipcFifoSystem.h>
#include <libtwl/sys/sysPower.h>
#include <libtwl/sio/sioRtc.h>
#include <libtwl/sio/sio.h>
#include <libtwl/gfx/gfxStatus.h>
#include <libtwl/mem/memSwap.h>
#include <libtwl/i2c/i2cMcu.h>
#include <libtwl/spi/spiPmic.h>
#include "ExitMode.h"
#include "Arm7State.h"
#include "tusb.h"
#include "usb_descriptors.h"
#include "ControllerInput.h"
#include "ControllerIpc.h"

/// @brief Timer used to pace the input polling.
#define INPUT_TIMER                 0
/// @brief Frequency of the system clock divided by 64, in Hz.
#define TIMER_CLK_SYS_DIV_64_HZ     523656
/// @brief Reload value of the input timer.
#define INPUT_TIMER_RELOAD          ((u16)-(TIMER_CLK_SYS_DIV_64_HZ / CONTROLLER_POLL_HZ))

#define USB_THREAD_PRIORITY         3
#define INPUT_THREAD_PRIORITY       5

rtos_mutex_t gCardMutex;

static rtos_thread_t sUsbThread;
static u32 sUsbThreadStack[512];

static rtos_thread_t sInputThread;
static u32 sInputThreadStack[512];
static rtos_event_t sInputEvent;

static rtos_event_t sVBlankEvent;
static ExitMode sExitMode;
static Arm7State sState;
static volatile u8 sMcuIrqFlag = false;

/// @brief Latest sampled controller state, published to the arm9 every frame.
static volatile u32 sIpcState;
/// @brief Last report that was accepted by the usb stack.
static hid_gamepad_report_t sLastReport;
/// @brief Forces the next report to be sent, even if nothing changed.
static volatile bool sForceReport;

static void vblankIrq(u32 irqMask)
{
    rtos_signalEvent(&sVBlankEvent);
}

static void inputTimerIrq(u32 irqMask)
{
    rtos_signalEvent(&sInputEvent);
}

static void mcuIrq(u32 irq2Mask)
{
    sMcuIrqFlag = true;
}

static void checkMcuIrq(void)
{
    // mcu only exists in DSi mode
    if (isDSiMode())
    {
        // check and ack the flag atomically
        if (mem_swapByte(false, &sMcuIrqFlag))
        {
            // check the irq mask
            u32 irqMask = mcu_getIrqMask();
            if (irqMask & MCU_IRQ_RESET)
            {
                // power button was released
                sExitMode = ExitMode::Reset;
                sState = Arm7State::ExitRequested;
            }
            else if (irqMask & MCU_IRQ_POWER_OFF)
            {
                // power button was held long to trigger a power off
                sExitMode = ExitMode::PowerOff;
                sState = Arm7State::ExitRequested;
            }
        }
    }
}

static void initializeVBlankIrq(void)
{
    rtos_createEvent(&sVBlankEvent);
    rtos_setIrqFunc(RTOS_IRQ_VBLANK, vblankIrq);
    rtos_enableIrqMask(RTOS_IRQ_VBLANK);
    gfx_setVBlankIrqEnabled(true);
}

static void usbThreadMain(void* arg)
{
    while (true)
    {
        tud_task();
    }
}

static void inputThreadMain(void* arg)
{
    while (true)
    {
        rtos_waitEvent(&sInputEvent, false, true);

        hid_gamepad_report_t report;
        u32 ipcState;
        controller_updateInput(&report, &ipcState);

        u32 usbState = CONTROLLER_USB_DISCONNECTED;
        if (tud_suspended())
        {
            usbState = CONTROLLER_USB_SUSPENDED;
        }
        else if (tud_mounted())
        {
            usbState = CONTROLLER_USB_MOUNTED;
        }
        ipcState |= usbState << CONTROLLER_STATE_USB_SHIFT;

        // Only send a report when something actually changed. Every transfer
        // costs a couple of card bus commands, so there is no point in filling
        // the bus with reports the host already has.
        if (tud_hid_n_ready(CONTROLLER_HID_GAMEPAD))
        {
            ipcState |= CONTROLLER_STATE_REPORTING;

            if (sForceReport || memcmp(&report, &sLastReport, sizeof(report)) != 0)
            {
                if (tud_hid_n_report(CONTROLLER_HID_GAMEPAD, 0, &report, sizeof(report)))
                {
                    sLastReport = report;
                    sForceReport = false;
                }
            }
        }

        // The mouse accumulates its movement until the endpoint is free, so it
        // is only drained when a report can actually be queued.
        if (tud_hid_n_ready(CONTROLLER_HID_MOUSE))
        {
            hid_mouse_report_t mouse;
            if (controller_takeMouseReport(&mouse))
            {
                tud_hid_n_report(CONTROLLER_HID_MOUSE, 0, &mouse, sizeof(mouse));
            }
        }

        sIpcState = ipcState;
    }
}

static void initializeArm7(void)
{
    rtos_initIrq();
    rtos_startMainThread();
    ipc_initFifoSystem();

    // the controller does not produce any sound
    pmic_setAmplifierEnable(false);
    sys_setSoundPower(false);

    readUserSettings();
    pmic_setPowerLedBlink(PMIC_CONTROL_POWER_LED_BLINK_NONE);

    sio_setGpioSiIrq(false);
    sio_setGpioMode(RCNT0_L_MODE_GPIO);

    rtc_init();

    controller_initInput();

    initializeVBlankIrq();

    if (isDSiMode())
    {
        rtos_setIrq2Func(RTOS_IRQ2_MCU, mcuIrq);
        rtos_enableIrq2Mask(RTOS_IRQ2_MCU);
    }

    rtos_createMutex(&gCardMutex);

    tusb_rhport_init_t dev_init =
    {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO
    };
    tusb_init(0, &dev_init);

    rtos_createThread(&sUsbThread, USB_THREAD_PRIORITY, usbThreadMain, NULL,
        sUsbThreadStack, sizeof(sUsbThreadStack));
    rtos_wakeupThread(&sUsbThread);

    // poll the buttons and the touch screen faster than the display refreshes
    rtos_createEvent(&sInputEvent);
    rtos_setIrqFunc(RTOS_IRQ_TIMER(INPUT_TIMER), inputTimerIrq);
    rtos_ackIrqMask(RTOS_IRQ_TIMER(INPUT_TIMER));
    rtos_enableIrqMask(RTOS_IRQ_TIMER(INPUT_TIMER));
    tmr_configure(INPUT_TIMER, TMCNT_H_CLK_SYS_DIV_64, INPUT_TIMER_RELOAD, true);

    rtos_createThread(&sInputThread, INPUT_THREAD_PRIORITY, inputThreadMain, NULL,
        sInputThreadStack, sizeof(sInputThreadStack));
    rtos_wakeupThread(&sInputThread);

    tmr_start(INPUT_TIMER);

    ipc_setArm7SyncBits(7);
}

static void updateArm7IdleState(void)
{
    checkMcuIrq();

    // publish the state for the arm9 status screen, dropping the update if the
    // fifo happens to be full
    ipc_trySendFifoMessage(CONTROLLER_IPC_CHANNEL_STATE, sIpcState);
}

static bool performExit(ExitMode exitMode)
{
    switch (exitMode)
    {
        case ExitMode::Reset:
        {
            mcu_setWarmBootFlag(true);
            mcu_hardReset();
            break;
        }
        case ExitMode::PowerOff:
        {
            pmic_shutdown();
            break;
        }
    }

    while (true); // wait infinitely for exit
}

static void updateArm7ExitRequestedState(void)
{
    performExit(sExitMode);
}

static void updateArm7(void)
{
    switch (sState)
    {
        case Arm7State::Idle:
        {
            updateArm7IdleState();
            break;
        }
        case Arm7State::ExitRequested:
        {
            updateArm7ExitRequestedState();
            break;
        }
    }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

void tud_mount_cb(void)
{
    sForceReport = true;
    controller_invalidateMouseState();
}

void tud_umount_cb(void)
{
    sForceReport = true;
}

void tud_resume_cb(void)
{
    sForceReport = true;
    controller_invalidateMouseState();
}

//--------------------------------------------------------------------+
// Hid callbacks
//--------------------------------------------------------------------+

u16 tud_hid_get_report_cb(u8 instance, u8 report_id, hid_report_type_t report_type,
    u8* buffer, u16 reqlen)
{
    (void)report_id;

    if (report_type != HID_REPORT_TYPE_INPUT || instance != CONTROLLER_HID_GAMEPAD
        || reqlen < sizeof(hid_gamepad_report_t))
    {
        // the mouse is a purely relative device, there is no state to hand out
        return 0;
    }

    memcpy(buffer, &sLastReport, sizeof(hid_gamepad_report_t));
    return sizeof(hid_gamepad_report_t);
}

void tud_hid_set_report_cb(u8 instance, u8 report_id, hid_report_type_t report_type,
    const u8* buffer, u16 bufsize)
{
    // the gamepad has no outputs (no rumble, no leds)
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

int main()
{
    sState = Arm7State::Idle;
    initializeArm7();

    while (true)
    {
        rtos_waitEvent(&sVBlankEvent, true, true);
        updateArm7();
    }

    return 0;
}
