#include "ControllerInput.h"
#include "ControllerIpc.h"

#define KEYXY_X                 (1 << 0)
#define KEYXY_Y                 (1 << 1)

/// @brief Full deflection of an axis of the hid gamepad report.
#define AXIS_MAX                127

/// @brief Keys that take part in the mode switching combos.
#define COMBO_KEYS              (KEY_SELECT | KEY_L | KEY_R)

//--------------------------------------------------------------------+
// State
//--------------------------------------------------------------------+

static u8 sTouchMode;
static bool sAnalogEnabled;

/// @brief Keys of the previous poll, used to find the rising edges.
static u32 sPrevKeys;
/// @brief Set while a mode switching combo is held, so that the keys taking
///        part in it are kept out of the gamepad report.
static bool sComboLatched;

/// @brief Set once the pen has been detected as down for two polls in a row.
/// @note  The first sample after a touch down tends to be noisy, and since the
///        very first sample is what anchors the stick (or the pointer) we
///        simply throw it away, like the libnds arm7 input code does.
static bool sPenSettled;
static bool sPenWasDown;

// stick mode
static u16 sStickOriginX;
static u16 sStickOriginY;

// mouse mode
static u16 sLastTouchX;
static u16 sLastTouchY;
/// @brief Pending pointer movement in hundredths of a pixel.
static int sMouseAccumX;
static int sMouseAccumY;
/// @brief Pending scroll movement in pixels of stylus travel.
static int sMouseScrollAccum;
static u8 sMouseButtons;
static u8 sMouseSentButtons;
/// @brief Polls since the stylus touched down, saturating.
static u16 sTouchTicks;
/// @brief Stylus travel since it touched down, in pixels.
static u16 sTouchTravel;
/// @brief Polls the left button is still held down for by a tap.
static u8 sTapHoldTicks;

//--------------------------------------------------------------------+
// Helpers
//--------------------------------------------------------------------+

static void resetTouchState(void)
{
    sPenSettled = false;
    sPenWasDown = false;
    sMouseAccumX = 0;
    sMouseAccumY = 0;
    sMouseScrollAccum = 0;
    sTapHoldTicks = 0;
    sTouchTicks = 0;
    sTouchTravel = 0;
}

void controller_initInput(void)
{
    touchInit();
    sTouchMode = CONTROLLER_DEFAULT_TOUCH_MODE;
    sAnalogEnabled = CONTROLLER_DEFAULT_ANALOG != 0;
    sPrevKeys = 0;
    sComboLatched = false;
    sMouseButtons = 0;
    sMouseSentButtons = 0;
    resetTouchState();
}

static u8 getHat(u32 keys)
{
    const bool up = (keys & KEY_UP) != 0;
    const bool down = (keys & KEY_DOWN) != 0;
    const bool left = (keys & KEY_LEFT) != 0;
    const bool right = (keys & KEY_RIGHT) != 0;

    if (up && !down)
    {
        if (left && !right)
        {
            return GAMEPAD_HAT_UP_LEFT;
        }
        else if (right && !left)
        {
            return GAMEPAD_HAT_UP_RIGHT;
        }
        return GAMEPAD_HAT_UP;
    }
    else if (down && !up)
    {
        if (left && !right)
        {
            return GAMEPAD_HAT_DOWN_LEFT;
        }
        else if (right && !left)
        {
            return GAMEPAD_HAT_DOWN_RIGHT;
        }
        return GAMEPAD_HAT_DOWN;
    }
    else if (left && !right)
    {
        return GAMEPAD_HAT_LEFT;
    }
    else if (right && !left)
    {
        return GAMEPAD_HAT_RIGHT;
    }
    return GAMEPAD_HAT_CENTERED;
}

static s8 getStickAxis(int delta)
{
    if (delta > -CONTROLLER_TOUCH_DEAD_ZONE && delta < CONTROLLER_TOUCH_DEAD_ZONE)
    {
        return 0;
    }

    int value = (delta * AXIS_MAX) / CONTROLLER_TOUCH_STICK_RADIUS;
    if (value > AXIS_MAX)
    {
        value = AXIS_MAX;
    }
    else if (value < -AXIS_MAX)
    {
        value = -AXIS_MAX;
    }
    return (s8)value;
}

/// @brief Handles the Select + L / Select + R mode switching combos.
/// @return The keys to report, with the combo keys removed while one is used.
static u32 updateModes(u32 keys)
{
    const u32 rising = keys & ~sPrevKeys;
    sPrevKeys = keys;

    if (keys & KEY_SELECT)
    {
        if (rising & KEY_L)
        {
            sTouchMode++;
            if (sTouchMode >= CONTROLLER_TOUCH_MODE_COUNT)
            {
                sTouchMode = 0;
            }
            // a stylus that is already down must not jump into the new mode
            resetTouchState();
            sComboLatched = true;
        }
        if (rising & KEY_R)
        {
            sAnalogEnabled = !sAnalogEnabled;
            sComboLatched = true;
        }
    }

    if (sComboLatched)
    {
        if (keys & COMBO_KEYS)
        {
            // hide the combo from the host until every key of it is released
            return keys & ~COMBO_KEYS;
        }
        sComboLatched = false;
    }

    return keys;
}

/// @brief Feeds a stylus sample to the mouse accumulators.
static void updateMouse(u16 touchX, u16 touchY, bool scrolling)
{
    const int dx = (int)touchX - (int)sLastTouchX;
    const int dy = (int)touchY - (int)sLastTouchY;
    sLastTouchX = touchX;
    sLastTouchY = touchY;

    int travel = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
    if (sTouchTravel < 0xFFFF - travel)
    {
        sTouchTravel += travel;
    }

    if (scrolling)
    {
        sMouseScrollAccum += dy;
    }
    else
    {
        sMouseAccumX += dx * CONTROLLER_MOUSE_SPEED;
        sMouseAccumY += dy * CONTROLLER_MOUSE_SPEED;
    }
}

/// @brief Turns a release of the stylus into a click if it was a short tap.
static void checkTap(void)
{
    if (sPenSettled && sTouchTicks <= CONTROLLER_MOUSE_TAP_TICKS
        && sTouchTravel <= CONTROLLER_MOUSE_TAP_SLOP)
    {
        sTapHoldTicks = CONTROLLER_MOUSE_TAP_HOLD;
    }
}

//--------------------------------------------------------------------+
// Input sampling
//--------------------------------------------------------------------+

void controller_updateInput(hid_gamepad_report_t* report, u32* ipcState)
{
    // the key registers are active low
    const u32 rawKeys = ~REG_KEYINPUT & 0x3FF;
    const u32 keysXY = ~REG_KEYXY & (KEYXY_X | KEYXY_Y);
    const u32 keys = updateModes(rawKeys);

    u32 buttons = 0;
    if (keys & KEY_A)       buttons |= CONTROLLER_BUTTON_DS_A;
    if (keys & KEY_B)       buttons |= CONTROLLER_BUTTON_DS_B;
    if (keysXY & KEYXY_X)   buttons |= CONTROLLER_BUTTON_DS_X;
    if (keysXY & KEYXY_Y)   buttons |= CONTROLLER_BUTTON_DS_Y;
    if (keys & KEY_L)       buttons |= CONTROLLER_BUTTON_DS_L;
    if (keys & KEY_R)       buttons |= CONTROLLER_BUTTON_DS_R;
    if (keys & KEY_SELECT)  buttons |= CONTROLLER_BUTTON_DS_SELECT;
    if (keys & KEY_START)   buttons |= CONTROLLER_BUTTON_DS_START;

    // The d-pad drives both the hat switch and the left stick, so that games
    // that only look at one of the two still work.
    report->hat = getHat(keys);
    report->x = 0;
    report->y = 0;
    report->z = 0;
    report->rz = 0;
    report->rx = 0;
    report->ry = 0;

    if (sAnalogEnabled)
    {
        report->x = (keys & KEY_RIGHT) ? AXIS_MAX : ((keys & KEY_LEFT) ? -AXIS_MAX : 0);
        report->y = (keys & KEY_DOWN) ? AXIS_MAX : ((keys & KEY_UP) ? -AXIS_MAX : 0);
    }

    u32 touchX = 0;
    u32 touchY = 0;
    bool penDown = false;

    if (sTouchMode != CONTROLLER_TOUCH_MODE_OFF && touchPenDown())
    {
        if (!sPenWasDown)
        {
            // skip the first sample after the touch down
            sPenSettled = false;
            sPenWasDown = true;
            sTouchTicks = 0;
            sTouchTravel = 0;
        }
        else
        {
            touchPosition touchPos;
            touchReadXY(&touchPos);

            if (touchPos.rawx != 0 && touchPos.rawy != 0)
            {
                penDown = true;
                touchX = touchPos.px;
                touchY = touchPos.py;

                if (!sPenSettled)
                {
                    sStickOriginX = touchPos.px;
                    sStickOriginY = touchPos.py;
                    sLastTouchX = touchPos.px;
                    sLastTouchY = touchPos.py;
                    sPenSettled = true;
                }
                else if (sTouchMode == CONTROLLER_TOUCH_MODE_MOUSE)
                {
                    updateMouse(touchPos.px, touchPos.py, (keysXY & KEYXY_Y) != 0);
                }

                if (sTouchMode == CONTROLLER_TOUCH_MODE_STICK)
                {
                    // The touch screen acts as a virtual right stick: the point
                    // where the stylus touches down becomes the center of the
                    // stick and moving away from it deflects the stick.
                    buttons |= CONTROLLER_BUTTON_DS_TOUCH;
                    if (sAnalogEnabled)
                    {
                        report->z = getStickAxis((int)touchPos.px - (int)sStickOriginX);
                        report->rz = getStickAxis((int)touchPos.py - (int)sStickOriginY);
                    }
                }
            }
        }

        if (sTouchTicks < 0xFFFF)
        {
            sTouchTicks++;
        }
    }
    else
    {
        if (sPenWasDown && sTouchMode == CONTROLLER_TOUCH_MODE_MOUSE)
        {
            checkTap();
        }
        sPenWasDown = false;
        sPenSettled = false;
    }

    // the shoulder buttons double as the mouse buttons while the pointer is live
    u8 mouseButtons = 0;
    if (sTouchMode == CONTROLLER_TOUCH_MODE_MOUSE)
    {
        if (keys & KEY_L)
        {
            mouseButtons |= MOUSE_BUTTON_LEFT;
        }
        if (keys & KEY_R)
        {
            mouseButtons |= MOUSE_BUTTON_RIGHT;
        }
        if (sTapHoldTicks != 0)
        {
            mouseButtons |= MOUSE_BUTTON_LEFT;
            sTapHoldTicks--;
        }
    }
    sMouseButtons = mouseButtons;

    report->buttons = buttons;

    *ipcState = (touchX << CONTROLLER_STATE_TOUCH_X_SHIFT)
        | (touchY << CONTROLLER_STATE_TOUCH_Y_SHIFT)
        | (penDown ? CONTROLLER_STATE_PEN_DOWN : 0)
        | ((keysXY & KEYXY_X) ? CONTROLLER_STATE_BUTTON_X : 0)
        | ((keysXY & KEYXY_Y) ? CONTROLLER_STATE_BUTTON_Y : 0)
        | ((u32)sTouchMode << CONTROLLER_STATE_TOUCH_MODE_SHIFT)
        | (sAnalogEnabled ? CONTROLLER_STATE_ANALOG_ENABLED : 0);
}

//--------------------------------------------------------------------+
// Mouse reports
//--------------------------------------------------------------------+

void controller_invalidateMouseState(void)
{
    // an impossible button mask, so that the next report is always sent
    sMouseSentButtons = 0xFF;
}

/// @brief Takes up to a full axis worth of movement out of an accumulator.
static s8 takeMovement(int* accum, int unit)
{
    int steps = *accum / unit;
    if (steps > AXIS_MAX)
    {
        steps = AXIS_MAX;
    }
    else if (steps < -AXIS_MAX)
    {
        steps = -AXIS_MAX;
    }
    *accum -= steps * unit;
    return (s8)steps;
}

bool controller_takeMouseReport(hid_mouse_report_t* report)
{
    const s8 x = takeMovement(&sMouseAccumX, 100);
    const s8 y = takeMovement(&sMouseAccumY, 100);
    // dragging down scrolls down, so the wheel goes the other way
    const s8 wheel = (s8)-takeMovement(&sMouseScrollAccum, CONTROLLER_MOUSE_SCROLL_PIXELS);
    const u8 buttons = sMouseButtons;

    if (x == 0 && y == 0 && wheel == 0 && buttons == sMouseSentButtons)
    {
        return false;
    }

    report->buttons = buttons;
    report->x = x;
    report->y = y;
    report->wheel = wheel;
    report->pan = 0;
    sMouseSentButtons = buttons;
    return true;
}
