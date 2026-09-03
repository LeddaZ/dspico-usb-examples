#include "common.h"
#include "UsbStringDescriptor.h"
#include "tusb.h"
#include "usb_descriptors.h"

/* A combination of interfaces must have a unique product id, since PC will save device driver after the first plug.
 * Same VID/PID with different interface e.g MSC (first), then CDC (later) will possibly cause system error on PC.
 *
 * Auto ProductID layout's Bitmap:
 *   [MSB]     AUDIO | MIDI | HID | MSC | CDC          [LSB]
 */
#define _PID_MAP(itf, n)  ( (CFG_TUD_##itf) << (n) )
#define USB_PID           (0x4000 | _PID_MAP(CDC, 0) | _PID_MAP(MSC, 1) | _PID_MAP(HID, 2) | \
    _PID_MAP(MIDI, 3) | _PID_MAP(AUDIO, 4) | _PID_MAP(VENDOR, 5) )

const u8* tud_descriptor_device_cb(void)
{
    static const tusb_desc_device_t deviceDescriptor =
    {
        .bLength            = sizeof(tusb_desc_device_t),
        .bDescriptorType    = TUSB_DESC_DEVICE,
        .bcdUSB             = 0x0110,

        // the class is defined by the (single) hid interface
        .bDeviceClass       = 0x00,
        .bDeviceSubClass    = 0x00,
        .bDeviceProtocol    = 0x00,
        .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

        .idVendor           = 0xCafe,
        .idProduct          = USB_PID,
        .bcdDevice          = 0x0100,

        .iManufacturer      = STRID_MANUFACTURER,
        .iProduct           = STRID_PRODUCT,
        .iSerialNumber      = STRID_SERIAL,

        .bNumConfigurations = 0x01
    };

    return (const u8*)&deviceDescriptor;
}

/* The report layout matches tinyusb's hid_gamepad_report_t:
 *   int8_t x, y      -> left stick   (the d-pad)
 *   int8_t z, rz     -> right stick  (the touch screen)
 *   int8_t rx, ry    -> analog triggers (unused, always 0)
 *   uint8_t hat      -> d-pad hat switch
 *   uint32_t buttons -> 32 buttons, see ControllerInput.h for the mapping
 */
static const u8 sGamepadReportDescriptor[] =
{
    TUD_HID_REPORT_DESC_GAMEPAD()
};

/* The mouse report matches tinyusb's hid_mouse_report_t:
 *   uint8_t buttons  -> left/right/middle
 *   int8_t x, y      -> relative movement of the stylus
 *   int8_t wheel     -> scroll notches
 *   int8_t pan       -> horizontal scroll (unused)
 */
static const u8 sMouseReportDescriptor[] =
{
    TUD_HID_REPORT_DESC_MOUSE()
};

const u8* tud_hid_descriptor_report_cb(u8 instance)
{
    return instance == CONTROLLER_HID_MOUSE ? sMouseReportDescriptor : sGamepadReportDescriptor;
}

#define CONFIG_TOTAL_LEN    (TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN)

const u8* tud_descriptor_configuration_cb(u8 index)
{
    (void)index;

    static const u8 configurationDescriptor[] =
    {
        // Config number, interface count, string index, total length, attribute, power in mA
        TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

        // Interface number, string index, protocol, report descriptor len, EP in address, size & polling interval
        TUD_HID_DESCRIPTOR(ITF_NUM_HID_GAMEPAD, STRID_GAMEPAD_INTERFACE, HID_ITF_PROTOCOL_NONE,
            sizeof(sGamepadReportDescriptor), EPNUM_HID_GAMEPAD_IN, CONTROLLER_HID_EP_SIZE,
            CONTROLLER_HID_INTERVAL),
        TUD_HID_DESCRIPTOR(ITF_NUM_HID_MOUSE, STRID_MOUSE_INTERFACE, HID_ITF_PROTOCOL_NONE,
            sizeof(sMouseReportDescriptor), EPNUM_HID_MOUSE_IN, CONTROLLER_HID_EP_SIZE,
            CONTROLLER_HID_INTERVAL)
    };

    // a wTotalLength that does not match the descriptor is a very annoying way
    // to fail enumeration, so let the compiler check it
    static_assert(sizeof(configurationDescriptor) == CONFIG_TOTAL_LEN,
        "the configuration descriptor length does not match CONFIG_TOTAL_LEN");

    return configurationDescriptor;
}

const u16* tud_descriptor_string_cb(u8 index, u16 langid)
{
    (void)langid;

    switch (index)
    {
        case STRID_LANGID:
        {
            static const UsbStringDescriptor<2> descriptor(0x0409);
            return (const u16*)&descriptor;
        }
        case STRID_MANUFACTURER:
        {
            return USB_STRING_DESCRIPTOR(u"LNH");
        }
        case STRID_PRODUCT:
        {
            return USB_STRING_DESCRIPTOR(u"DSpico Controller");
        }
        case STRID_SERIAL:
        {
            return USB_STRING_DESCRIPTOR(u"123456789");
        }
        case STRID_GAMEPAD_INTERFACE:
        {
            return USB_STRING_DESCRIPTOR(u"DSpico Gamepad");
        }
        case STRID_MOUSE_INTERFACE:
        {
            return USB_STRING_DESCRIPTOR(u"DSpico Touch Mouse");
        }
        default:
        {
            return nullptr;
        }
    }
}
