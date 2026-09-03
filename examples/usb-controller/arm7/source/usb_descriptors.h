#pragma once

enum
{
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_GAMEPAD_INTERFACE,
    STRID_MOUSE_INTERFACE
};

enum
{
    ITF_NUM_HID_GAMEPAD = 0,
    ITF_NUM_HID_MOUSE,
    ITF_NUM_TOTAL
};

/// @brief Hid instance carrying the gamepad reports.
#define CONTROLLER_HID_GAMEPAD  0
/// @brief Hid instance carrying the mouse reports.
#define CONTROLLER_HID_MOUSE    1

/// @brief Interrupt in endpoints of the two hid interfaces.
#define EPNUM_HID_GAMEPAD_IN    0x81
#define EPNUM_HID_MOUSE_IN      0x82

/// @brief Endpoint/buffer size of the hid endpoints.
#define CONTROLLER_HID_EP_SIZE  16
/// @brief Polling interval of the hid endpoints in (full speed) frames.
#define CONTROLLER_HID_INTERVAL 1
