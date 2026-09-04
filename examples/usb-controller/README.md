# USB Controller

> [!IMPORTANT]
> This works, but it's vibecoded. I'm not very knowledgeable on C++ but I still wanted to see
> if this is possible, since I have a use for it. If someone competent wants to pick
> this up or redo it from scratch, I'd be more than happy.

Turns a DS, DSi or 3DS with a DSpico cartridge into a plain USB HID gamepad,
with the touch screen usable as a right stick or as a mouse. No driver is
needed: the console enumerates as a standard gamepad plus a standard mouse, so
Windows (`joy.cpl`), Linux (`jstest`, SDL, Steam), macOS and RetroArch all pick
it up out of the box.

## Using it

1. Copy `usb-controller.nds` to your DSpico micro SD card and launch it.
2. Plug the DSpico into a PC.
3. The top screen shows the connection state and the live input; the bottom
   screen visualizes the stylus.

The console never goes to sleep when you close the lid, so you can fold it
shut and keep playing with a controller in the other hand if you want to.

## Modes

Two things can be switched while the app runs. The combos are swallowed, so the
host never sees the Select or shoulder press that triggered them.

| Combo        | Effect                                                     |
| ------------ | ---------------------------------------------------------- |
| **Select+L** | Cycles the touch screen: **stick** -> **mouse** -> **off**  |
| **Select+R** | Turns the **analog axes** on or off                        |

**Touch screen modes**

* `stick` – the touch screen is a virtual right stick (see below) and the
  stylus reports as button 9.
* `mouse` – the stylus drives the mouse interface: dragging moves the pointer,
  a short tap is a left click, `L` and `R` are held left/right buttons (so you
  can drag and drop), and holding `Y` while dragging scrolls the wheel instead
  of moving the pointer.
* `off` – the touch screen is ignored entirely, for when the console is resting
  somewhere and you do not want stray touches.

**Analog off** reports every axis as centered and leaves the d-pad driving only
the hat switch. Some games and frontends get confused by a pad that offers a
hat *and* a stick that mirror each other, or auto-map the stick over the
buttons; turning the axes off makes the pad purely digital. In the stick touch
mode the stylus then still reports button 9, but no longer deflects anything.

The starting mode of both toggles is set by `CONTROLLER_DEFAULT_TOUCH_MODE` and
`CONTROLLER_DEFAULT_ANALOG` in
[`common/ControllerConfig.h`](common/ControllerConfig.h).

## Mapping

| DS input      | Reported as                                    |
| ------------- | ---------------------------------------------- |
| D-pad         | Hat switch **and** left stick (X/Y)            |
| B             | Button 1 (south)                               |
| A             | Button 2 (east)                                |
| Y             | Button 3 (west)                                |
| X             | Button 4 (north)                               |
| L / R         | Buttons 5 / 6                                  |
| Select        | Button 7                                       |
| Start         | Button 8                                       |
| Stylus down   | Button 9 (stick mode only)                     |
| Stylus drag   | Right stick (Z/Rz), or the mouse pointer       |

The face buttons are mapped by **position**, not by label: the DS diamond has
the same layout as an SNES or xbox pad, so the bottom button (DS `B`) is
reported as button 1, which is what most games expect. Set
`CONTROLLER_MAP_FACE_BY_POSITION` to `0` in [`common/ControllerConfig.h`](common/ControllerConfig.h)
to report `A` as button 1 instead.

### The touch screen as a stick

In `stick` mode the touch screen behaves like a virtual right stick. The point
where the stylus touches down becomes the center of the stick, and dragging
away from it deflects the stick; `CONTROLLER_TOUCH_STICK_RADIUS` pixels of
travel is full deflection. Lifting the stylus recenters the stick. Because the
origin follows your stylus instead of being fixed at the middle of the screen,
you can put your hand down anywhere on the screen.

### The touch screen as a mouse

In `mouse` mode the stylus is a relative pointer, like a laptop touchpad: only
the movement is sent, so the pointer picks up where it left off every time you
put the stylus down. `CONTROLLER_MOUSE_SPEED` scales the movement (200 = twice
the stylus travel) and the leftover fraction of a pixel is carried over, so
slow drags are not rounded away. Movement is accumulated while the endpoint is
busy rather than dropped.

Note that `L`, `R` and `Y` keep reporting as gamepad buttons while they act as
the mouse buttons and the scroll modifier; the mouse and the gamepad are two
separate interfaces and both stay live.

## How it works

* All of the interesting work happens on the **arm7**, since it is the cpu
  that owns the key registers, the touch screen and the DSpico card bus.
* `ControllerInput.cpp` samples the buttons (`REG_KEYINPUT` / `REG_KEYXY`) and
  the touch screen and turns them into a tinyusb `hid_gamepad_report_t` and,
  in mouse mode, a `hid_mouse_report_t`.
* The device exposes **two hid interfaces**: instance 0 is the gamepad, instance
  1 is the mouse. The mouse interface is always present but stays silent unless
  the touch screen is in mouse mode.
* Input is polled from a timer interrupt at `CONTROLLER_POLL_HZ` (250 Hz by
  default) rather than once per frame, which cuts the input lag down from
  ~16 ms to ~4 ms. A report is only sent when something actually changed,
  so an idle gamepad does not put any traffic on the card bus.
* The arm7 publishes a packed state word to the arm9 over the ipc fifo once
  per frame ([`common/ControllerIpc.h`](common/ControllerIpc.h)); the arm9 only
  draws the status screen.

| File | What it does |
| ---- | ------------ |
| `arm7/source/main.cpp` | Startup, usb/input threads, tinyusb device callbacks |
| `arm7/source/ControllerInput.cpp` | Button and touch sampling, mode combos, report building |
| `arm7/source/usb_descriptors.cpp` | Device, configuration and hid report descriptors |
| `arm9/source/main.cpp` | Status console and touch screen visualization |
| `common/ControllerConfig.h` | Tuning knobs (poll rate, stick radius, mouse speed, defaults) |
| `common/ControllerIpc.h` | Layout of the arm7 to arm9 status message |

## Building

Like the other examples, this needs a **pre-calico** devkitPro/libnds
environment. With one set up, run `make` in this folder.

The easiest way to get a matching environment is the same container the CI
uses:

```sh
docker run --rm -v "$PWD:/src" -w /src/examples/usb-controller \
    devkitpro/devkitarm:20241104 make
```

> [!IMPORTANT]
> There is currently a bug in the DSpico firmware that sometimes causes the
> DSpico to crash if you use USB, reset the console and use USB again. If that
> happens, unplug USB and reset the console so that the DSpico power cycles.
