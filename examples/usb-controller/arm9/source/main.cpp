#include <nds.h>
#include <stdio.h>
#include <libtwl/gfx/gfxStatus.h>
#include <libtwl/mem/memExtern.h>
#include <libtwl/rtos/rtosIrq.h>
#include <libtwl/rtos/rtosThread.h>
#include <libtwl/rtos/rtosEvent.h>
#include <libtwl/ipc/ipcSync.h>
#include <libtwl/ipc/ipcFifoSystem.h>
#include "ControllerIpc.h"
#include "ControllerConfig.h"

#define SCREEN_W            256
#define SCREEN_H            192

#define COLOR_BACKGROUND    ARGB16(1, 2, 3, 6)
#define COLOR_GRID          ARGB16(1, 4, 6, 11)
#define COLOR_CENTER        ARGB16(1, 7, 10, 16)
#define COLOR_DISCONNECTED  ARGB16(1, 26, 20, 4)
#define COLOR_MOUNTED       ARGB16(1, 6, 28, 12)
#define COLOR_SUSPENDED     ARGB16(1, 28, 10, 4)
#define COLOR_STYLUS        ARGB16(1, 31, 31, 31)

static rtos_event_t sVBlankEvent;

/// @brief Latest controller state received from the arm7.
static volatile u32 sControllerState;

static void vblankIrq(u32 irqMask)
{
    rtos_signalEvent(&sVBlankEvent);
}

static void controllerStateHandler(u32 channel, u32 data, void* arg)
{
    sControllerState = data;
}

//--------------------------------------------------------------------+
// Touch screen visualization (main engine, bottom screen)
//--------------------------------------------------------------------+

static void fillRect(int x, int y, int width, int height, u16 color)
{
    if (x < 0)
    {
        width += x;
        x = 0;
    }
    if (y < 0)
    {
        height += y;
        y = 0;
    }
    if (x + width > SCREEN_W)
    {
        width = SCREEN_W - x;
    }
    if (y + height > SCREEN_H)
    {
        height = SCREEN_H - y;
    }

    for (int row = 0; row < height; row++)
    {
        u16* dst = &VRAM_A[(y + row) * SCREEN_W + x];
        for (int col = 0; col < width; col++)
        {
            *dst++ = color;
        }
    }
}

static u16 getStateColor(u32 usbState)
{
    switch (usbState)
    {
        case CONTROLLER_USB_MOUNTED:    return COLOR_MOUNTED;
        case CONTROLLER_USB_SUSPENDED:  return COLOR_SUSPENDED;
        default:                        return COLOR_DISCONNECTED;
    }
}

static void drawTouchScreen(u32 state)
{
    const u16 stateColor = getStateColor(controller_getUsbState(state));

    dmaFillWords(COLOR_BACKGROUND | (COLOR_BACKGROUND << 16), VRAM_A, SCREEN_W * SCREEN_H * 2);

    // grid
    for (int x = 32; x < SCREEN_W; x += 32)
    {
        fillRect(x, 0, 1, SCREEN_H, COLOR_GRID);
    }
    for (int y = 32; y < SCREEN_H; y += 32)
    {
        fillRect(0, y, SCREEN_W, 1, COLOR_GRID);
    }

    // border, colored by the usb state
    fillRect(0, 0, SCREEN_W, 2, stateColor);
    fillRect(0, SCREEN_H - 2, SCREEN_W, 2, stateColor);
    fillRect(0, 0, 2, SCREEN_H, stateColor);
    fillRect(SCREEN_W - 2, 0, 2, SCREEN_H, stateColor);

    if (state & CONTROLLER_STATE_PEN_DOWN)
    {
        const int x = (int)controller_getTouchX(state);
        const int y = (int)controller_getTouchY(state);

        // crosshair through the stylus position
        fillRect(0, y, SCREEN_W, 1, stateColor);
        fillRect(x, 0, 1, SCREEN_H, stateColor);

        if (controller_getTouchMode(state) == CONTROLLER_TOUCH_MODE_STICK)
        {
            // the area the stylus has to stay in for the stick to be centered
            fillRect(x - CONTROLLER_TOUCH_DEAD_ZONE, y - CONTROLLER_TOUCH_DEAD_ZONE,
                CONTROLLER_TOUCH_DEAD_ZONE * 2 + 1, CONTROLLER_TOUCH_DEAD_ZONE * 2 + 1,
                COLOR_CENTER);
        }

        fillRect(x - 3, y - 3, 7, 7, COLOR_STYLUS);
    }
}

//--------------------------------------------------------------------+
// Status console (sub engine, top screen)
//--------------------------------------------------------------------+

/// @brief Width the dynamic mapping lines are padded to, so that a shorter
///        line always wipes the one it replaces.
#define MAPPING_WIDTH   28

static void printMappingLine(int row, const char* text)
{
    iprintf("\x1b[%d;2H%-*s", row, MAPPING_WIDTH, text);
}

static void drawStaticText(void)
{
    iprintf("\x1b[2J");
    iprintf("\x1b[1;2H"  "DSpico USB Controller");
    iprintf("\x1b[2;2H"  "---------------------");

    iprintf("\x1b[4;2H"  "USB    :");
    iprintf("\x1b[5;2H"  "Reports:");
    iprintf("\x1b[6;2H"  "Touch  :");
    iprintf("\x1b[7;2H"  "Analog :");

    iprintf("\x1b[9;2H"  "Buttons: A B X Y L R s S");
    iprintf("\x1b[11;2H" "D-Pad  : U D L R");
    iprintf("\x1b[13;2H" "Stylus :");

    iprintf("\x1b[15;2H" "Mapping");

    iprintf("\x1b[22;2H" "Select+L: touch screen mode");
    iprintf("\x1b[23;2H" "Select+R: analog axes on/off");
}

/// @brief Redraws the mapping block, which depends on the current modes.
static void drawMapping(u32 state)
{
    const bool analog = (state & CONTROLLER_STATE_ANALOG_ENABLED) != 0;

#if CONTROLLER_MAP_FACE_BY_POSITION
    printMappingLine(16, " B A Y X    -> buttons 1-4");
#else
    printMappingLine(16, " A B X Y    -> buttons 1-4");
#endif
    printMappingLine(17, " L R s S    -> buttons 5-8");
    printMappingLine(18, analog ? " d-pad      -> hat + stick" : " d-pad      -> hat only");

    switch (controller_getTouchMode(state))
    {
        case CONTROLLER_TOUCH_MODE_MOUSE:
        {
            printMappingLine(19, " stylus drag-> move pointer");
            printMappingLine(20, " tap/L/R = click, Y = wheel");
            break;
        }
        case CONTROLLER_TOUCH_MODE_OFF:
        {
            printMappingLine(19, " stylus     -> ignored");
            printMappingLine(20, "");
            break;
        }
        default:
        {
            printMappingLine(19, " stylus     -> button 9");
            printMappingLine(20, analog ? " stylus drag-> right stick"
                : " stylus drag-> nothing");
            break;
        }
    }
}

static void drawStatus(u32 state, u32 keys)
{
    const char* usbText;
    switch (controller_getUsbState(state))
    {
        case CONTROLLER_USB_MOUNTED:    usbText = "connected     "; break;
        case CONTROLLER_USB_SUSPENDED:  usbText = "suspended     "; break;
        default:                        usbText = "waiting for pc"; break;
    }

    const char* touchText;
    switch (controller_getTouchMode(state))
    {
        case CONTROLLER_TOUCH_MODE_MOUSE:   touchText = "mouse"; break;
        case CONTROLLER_TOUCH_MODE_OFF:     touchText = "off  "; break;
        default:                            touchText = "stick"; break;
    }

    iprintf("\x1b[4;11H%s", usbText);
    iprintf("\x1b[5;11H%s", (state & CONTROLLER_STATE_REPORTING) ? "on " : "off");
    iprintf("\x1b[6;11H%s", touchText);
    iprintf("\x1b[7;11H%s", (state & CONTROLLER_STATE_ANALOG_ENABLED) ? "on " : "off");

    char pressed[25];
    const bool buttonStates[8] =
    {
        (keys & KEY_A) != 0,
        (keys & KEY_B) != 0,
        (state & CONTROLLER_STATE_BUTTON_X) != 0,
        (state & CONTROLLER_STATE_BUTTON_Y) != 0,
        (keys & KEY_L) != 0,
        (keys & KEY_R) != 0,
        (keys & KEY_SELECT) != 0,
        (keys & KEY_START) != 0
    };
    for (int i = 0; i < 8; i++)
    {
        pressed[i * 2] = buttonStates[i] ? '^' : ' ';
        pressed[i * 2 + 1] = ' ';
    }
    pressed[16] = '\0';
    iprintf("\x1b[10;11H%s", pressed);

    const bool dpadStates[4] =
    {
        (keys & KEY_UP) != 0,
        (keys & KEY_DOWN) != 0,
        (keys & KEY_LEFT) != 0,
        (keys & KEY_RIGHT) != 0
    };
    for (int i = 0; i < 4; i++)
    {
        pressed[i * 2] = dpadStates[i] ? '^' : ' ';
        pressed[i * 2 + 1] = ' ';
    }
    pressed[8] = '\0';
    iprintf("\x1b[12;11H%s", pressed);

    if (state & CONTROLLER_STATE_PEN_DOWN)
    {
        iprintf("\x1b[13;11Hx=%3u y=%3u", (unsigned)controller_getTouchX(state),
            (unsigned)controller_getTouchY(state));
    }
    else
    {
        iprintf("\x1b[13;11H%s", "not touched");
    }
}

//--------------------------------------------------------------------+
// Entry point
//--------------------------------------------------------------------+

static void initializeVideo(void)
{
    powerOn(POWER_ALL_2D);
    // the main engine draws the touch visualization, so it belongs on the
    // bottom screen, which leaves the top screen for the status console
    lcdMainOnBottom();

    REG_MASTER_BRIGHT = 0;
    REG_MASTER_BRIGHT_SUB = 0;

    // the bottom screen is a plain 16 bit framebuffer in vram bank A
    vramSetBankA(VRAM_A_LCD);
    videoSetMode(MODE_FB0);

    consoleDemoInit();
    drawStaticText();
}

int main(int argc, char* argv[])
{
    // the arm7 talks to the DSpico over the card bus
    mem_setDsCartridgeCpu(EXMEMCNT_SLOT1_CPU_ARM7);

    initializeVideo();

    rtos_initIrq();
    rtos_startMainThread();
    ipc_initFifoSystem();

    rtos_createEvent(&sVBlankEvent);
    ipc_setChannelHandler(CONTROLLER_IPC_CHANNEL_STATE, controllerStateHandler, nullptr);

    while (ipc_getArm7SyncBits() != 7);

    ipc_setArm9SyncBits(6);

    rtos_setIrqFunc(RTOS_IRQ_VBLANK, vblankIrq);
    rtos_enableIrqMask(RTOS_IRQ_VBLANK);
    gfx_setVBlankIrqEnabled(true);

    u32 lastState = ~0u;
    u32 lastKeys = ~0u;

    while (true)
    {
        rtos_waitEvent(&sVBlankEvent, true, true);

        const u32 state = sControllerState;
        const u32 keys = ~REG_KEYINPUT & 0x3FF;

        if (state != lastState || keys != lastKeys)
        {
            drawStatus(state, keys);
            lastKeys = keys;
        }

        if (state != lastState)
        {
            const u32 modeBits = (CONTROLLER_STATE_TOUCH_MODE_MASK << CONTROLLER_STATE_TOUCH_MODE_SHIFT)
                | CONTROLLER_STATE_ANALOG_ENABLED;
            if ((state ^ lastState) & modeBits)
            {
                drawMapping(state);
            }

            drawTouchScreen(state);
            lastState = state;
        }
    }

    return 0;
}
