#include <stdio.h>
#include "tusb.h"
#include "pico/unique_id.h"
#include "usb_descriptors.h"

// Interface 0: Boot Keyboard (6KRO, no report ID for boot compatibility)
uint8_t const desc_hid_report_boot_kbd[] =
{
  TUD_HID_REPORT_DESC_KEYBOARD()
};

// Interface 1: Extended HID (mouse + consumer control with report IDs)
uint8_t const desc_hid_report_extended[] =
{
  TUD_HID_REPORT_DESC_MOUSE   ( HID_REPORT_ID(REPORT_ID_MOUSE) ),
  TUD_HID_REPORT_DESC_CONSUMER( HID_REPORT_ID(REPORT_ID_CONSUMER_CONTROL) ),
};

// USB Device Descriptor - matches real GMMK Pro
tusb_desc_device_t const desc_device =
{
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = 0x0200,

  // Class defined at interface level (standard for HID)
  .bDeviceClass       = 0x00,
  .bDeviceSubClass    = 0x00,
  .bDeviceProtocol    = 0x00,

  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

  .idVendor           = 0x320F,  // Glorious
  .idProduct          = 0x5044,  // GMMK Pro
  .bcdDevice          = 0x0043,  // Matches real GMMK Pro firmware version

  .iManufacturer      = 0x01,
  .iProduct           = 0x02,
  .iSerialNumber      = 0x03,

  .bNumConfigurations = 0x01
};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const * tud_descriptor_device_cb(void)
{
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_DESC_LEN)

uint8_t const desc_fs_configuration[] =
{
  // Config number, interface count, string index, total length, attribute, power in mA
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

  // Interface 0: Boot Keyboard (SubClass 1, Protocol 1 = Keyboard)
  TUD_HID_DESCRIPTOR(ITF_NUM_HID_BOOT_KBD, 0, HID_ITF_PROTOCOL_KEYBOARD, sizeof(desc_hid_report_boot_kbd), 0x81, 8, 10),

  // Interface 1: Extended HID (SubClass 0, Protocol 0)
  TUD_HID_DESCRIPTOR(ITF_NUM_HID_EXTENDED, 0, HID_ITF_PROTOCOL_NONE, sizeof(desc_hid_report_extended), 0x82, CFG_TUD_HID_EP_BUFSIZE, 10),
};

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const * tud_descriptor_configuration_cb(uint8_t index)
{
  (void) index; // for multiple configurations
  return desc_fs_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// array of pointer to string descriptors
char const* string_desc_arr [] =
{
  (const char[]) { 0x09, 0x04 }, // 0: is supported language = English (0x0409)
  "Glorious",                    // 1: Manufacturer
  "GMMK Pro",                    // 2: Product
  NULL,                          // 3: Serial - generated dynamically from Pico unique ID
};

static uint16_t _desc_str[32];
static char serial_str[32];  // Buffer for generated serial

// Generate serial number from Pico's unique board ID
static const char* get_serial_string(void) {
  static bool serial_generated = false;
  if (!serial_generated) {
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    // Format like real GMMK Pro: GL-GMMK-PRO-XXXXX
    snprintf(serial_str, sizeof(serial_str), "GL-GMMK-PRO-%02X%02X%02X",
             board_id.id[5], board_id.id[6], board_id.id[7]);
    serial_generated = true;
  }
  return serial_str;
}

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  (void) langid;

  uint8_t chr_count;

  if (index == 0)
  {
    memcpy(&_desc_str[1], string_desc_arr[0], 2);
    chr_count = 1;
  }
  else
  {
    const char* str;

    if (index == 3) {
      // Serial number - generate from Pico unique ID
      str = get_serial_string();
    } else if (index < sizeof(string_desc_arr)/sizeof(string_desc_arr[0])) {
      str = string_desc_arr[index];
    } else {
      return NULL;
    }

    if (str == NULL) return NULL;

    // Cap at max char
    chr_count = (uint8_t) strlen(str);
    if (chr_count > 31) chr_count = 31;

    // Convert ASCII string into UTF-16
    for (uint8_t i = 0; i < chr_count; i++)
    {
      _desc_str[1+i] = str[i];
    }
  }

  // first byte is length (including header), second byte is string type
  _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2*chr_count + 2));

  return _desc_str;
}

//--------------------------------------------------------------------+
// HID Report Descriptor
//--------------------------------------------------------------------+

uint8_t const * tud_hid_descriptor_report_cb(uint8_t instance)
{
  if (instance == ITF_NUM_HID_BOOT_KBD) {
    return desc_hid_report_boot_kbd;
  } else {
    return desc_hid_report_extended;
  }
}