#pragma once
#include "common.h"
#include "tusb.h"
#include "ControllerConfig.h"

//--------------------------------------------------------------------+
// Button mapping
//--------------------------------------------------------------------+

#if CONTROLLER_MAP_FACE_BY_POSITION
#define CONTROLLER_BUTTON_DS_A      GAMEPAD_BUTTON_1    // east
#define CONTROLLER_BUTTON_DS_B      GAMEPAD_BUTTON_0    // south
#define CONTROLLER_BUTTON_DS_X      GAMEPAD_BUTTON_3    // north
#define CONTROLLER_BUTTON_DS_Y      GAMEPAD_BUTTON_2    // west
#else
#define CONTROLLER_BUTTON_DS_A      GAMEPAD_BUTTON_0
#define CONTROLLER_BUTTON_DS_B      GAMEPAD_BUTTON_1
#define CONTROLLER_BUTTON_DS_X      GAMEPAD_BUTTON_2
#define CONTROLLER_BUTTON_DS_Y      GAMEPAD_BUTTON_3
#endif

#define CONTROLLER_BUTTON_DS_L      GAMEPAD_BUTTON_4
#define CONTROLLER_BUTTON_DS_R      GAMEPAD_BUTTON_5
#define CONTROLLER_BUTTON_DS_SELECT GAMEPAD_BUTTON_6
#define CONTROLLER_BUTTON_DS_START  GAMEPAD_BUTTON_7
/// @brief Reported while the stylus touches the screen, like a stick click.
///        Only used in the stick touch mode.
#define CONTROLLER_BUTTON_DS_TOUCH  GAMEPAD_BUTTON_8

//--------------------------------------------------------------------+
// API
//--------------------------------------------------------------------+

/// @brief Initializes the touch screen. Must be called after readUserSettings,
///        since the touch screen calibration lives in the user settings.
void controller_initInput(void);

/// @brief Samples the buttons and the touch screen.
/// @param report Receives the gamepad report to send to the host.
/// @param ipcState Receives the touch/button/mode bits of the arm9 status message.
/// @note  Mouse movement is accumulated internally, pick it up with
///        controller_takeMouseReport.
void controller_updateInput(hid_gamepad_report_t* report, u32* ipcState);

/// @brief Builds the next mouse report from the movement accumulated since the
///        previous call and consumes it.
/// @param report Receives the mouse report to send to the host.
/// @return True if there is anything to report, false if the mouse is idle.
/// @note  Only call this when the mouse endpoint can actually take a report,
///        so that nothing is dropped while it is busy.
bool controller_takeMouseReport(hid_mouse_report_t* report);

/// @brief Forgets which mouse button state the host was last told about, so
///        that the next report resyncs it. Call this whenever the device is
///        (re)mounted.
void controller_invalidateMouseState(void);
