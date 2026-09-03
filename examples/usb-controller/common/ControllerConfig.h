#pragma once
#include "ControllerIpc.h"

// Tuning knobs shared by the arm7 (which produces the reports) and the arm9
// (which explains the mapping on screen).

/// @brief Rate at which the buttons and the touch screen are sampled.
/// @note  The console only refreshes at 60 Hz, but nothing stops us from
///        polling the input registers faster than that, which noticeably
///        cuts down the input lag of the gamepad.
#define CONTROLLER_POLL_HZ              250

/// @brief Distance in pixels the stylus has to travel from the point where it
///        touched down for the virtual right stick to reach full deflection.
#define CONTROLLER_TOUCH_STICK_RADIUS   48

/// @brief Distance in pixels around the touch down point that is reported as
///        a centered stick.
#define CONTROLLER_TOUCH_DEAD_ZONE      4

//--------------------------------------------------------------------+
// Defaults of the runtime toggles
//--------------------------------------------------------------------+

/// @brief Touch screen mode the app starts in, one of the
///        CONTROLLER_TOUCH_MODE_* values from ControllerIpc.h.
///        Cycled at runtime with Select + L.
#define CONTROLLER_DEFAULT_TOUCH_MODE   CONTROLLER_TOUCH_MODE_STICK

/// @brief Whether the analog axes of the gamepad are reported at startup.
///        When they are off the d-pad only drives the hat switch and every
///        axis stays centered, which some games and frontends prefer over a
///        pad that reports both a hat and a stick.
///        Toggled at runtime with Select + R.
#define CONTROLLER_DEFAULT_ANALOG       1

//--------------------------------------------------------------------+
// Mouse mode
//--------------------------------------------------------------------+

/// @brief Pointer speed in percent, applied to the stylus movement.
#define CONTROLLER_MOUSE_SPEED          200

/// @brief Pixels of vertical drag per scroll wheel notch.
#define CONTROLLER_MOUSE_SCROLL_PIXELS  12

/// @brief Longest touch that still counts as a tap, in poll ticks.
#define CONTROLLER_MOUSE_TAP_TICKS      (CONTROLLER_POLL_HZ / 4)

/// @brief Total stylus travel a tap may have, in pixels.
#define CONTROLLER_MOUSE_TAP_SLOP       6

/// @brief How many polls a tap holds the left mouse button down for.
#define CONTROLLER_MOUSE_TAP_HOLD       (CONTROLLER_POLL_HZ / 50)

//--------------------------------------------------------------------+
// Button mapping
//--------------------------------------------------------------------+

/// @brief Maps the face buttons by their physical position (the DS layout
///        matches the SNES/xbox layout) instead of by their label.
///        With this enabled the DS B button is reported as gamepad button 1,
///        which is what most games expect from the "bottom" button.
///        Set this to 0 to report A as button 1, B as button 2, and so on.
#define CONTROLLER_MAP_FACE_BY_POSITION 1

