#pragma once
#include <nds/ndstypes.h>

/// @brief Ipc fifo channel the arm7 uses to publish the controller state to the arm9.
#define CONTROLLER_IPC_CHANNEL_STATE    1

// The libtwl ipc fifo system reserves the low 5 bits of a message for the
// channel number, so a single message can carry 27 bits of payload.
//
//  bits  0 -  7 : touch x position (0 - 255)
//  bits  8 - 15 : touch y position (0 - 191)
//  bit  16      : pen is down
//  bit  17      : X button is pressed
//  bit  18      : Y button is pressed
//  bits 19 - 20 : usb state (see the CONTROLLER_USB_* defines)
//  bit  21      : the host has enabled the gamepad (reports are being sent)
//  bits 22 - 23 : touch screen mode (see the CONTROLLER_TOUCH_MODE_* defines)
//  bit  24      : the analog axes of the gamepad are enabled

#define CONTROLLER_STATE_TOUCH_X_SHIFT      0
#define CONTROLLER_STATE_TOUCH_X_MASK       0xFF
#define CONTROLLER_STATE_TOUCH_Y_SHIFT      8
#define CONTROLLER_STATE_TOUCH_Y_MASK       0xFF
#define CONTROLLER_STATE_PEN_DOWN           (1 << 16)
#define CONTROLLER_STATE_BUTTON_X           (1 << 17)
#define CONTROLLER_STATE_BUTTON_Y           (1 << 18)
#define CONTROLLER_STATE_USB_SHIFT          19
#define CONTROLLER_STATE_USB_MASK           0x3
#define CONTROLLER_STATE_REPORTING          (1 << 21)
#define CONTROLLER_STATE_TOUCH_MODE_SHIFT   22
#define CONTROLLER_STATE_TOUCH_MODE_MASK    0x3
#define CONTROLLER_STATE_ANALOG_ENABLED     (1 << 24)

/// @brief Usb connection states reported in CONTROLLER_STATE_USB_SHIFT.
#define CONTROLLER_USB_DISCONNECTED     0   ///< No host connected (or no cable plugged in).
#define CONTROLLER_USB_MOUNTED          1   ///< The gamepad has been enumerated by the host.
#define CONTROLLER_USB_SUSPENDED        2   ///< The bus is suspended by the host.

/// @brief What the touch screen is wired up to.
#define CONTROLLER_TOUCH_MODE_STICK     0   ///< Virtual right stick on the gamepad.
#define CONTROLLER_TOUCH_MODE_MOUSE     1   ///< Relative pointer on the mouse interface.
#define CONTROLLER_TOUCH_MODE_OFF       2   ///< The touch screen is ignored.
#define CONTROLLER_TOUCH_MODE_COUNT     3

static inline u32 controller_getTouchX(u32 state)
{
    return (state >> CONTROLLER_STATE_TOUCH_X_SHIFT) & CONTROLLER_STATE_TOUCH_X_MASK;
}

static inline u32 controller_getTouchY(u32 state)
{
    return (state >> CONTROLLER_STATE_TOUCH_Y_SHIFT) & CONTROLLER_STATE_TOUCH_Y_MASK;
}

static inline u32 controller_getUsbState(u32 state)
{
    return (state >> CONTROLLER_STATE_USB_SHIFT) & CONTROLLER_STATE_USB_MASK;
}

static inline u32 controller_getTouchMode(u32 state)
{
    return (state >> CONTROLLER_STATE_TOUCH_MODE_SHIFT) & CONTROLLER_STATE_TOUCH_MODE_MASK;
}
