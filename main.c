/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/udp.h"
#include "lwip/pbuf.h"

#include "bsp/board_api.h"
#include "tusb.h"

#include "usb_descriptors.h"
#include "wifi_config.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTOTYPES
//--------------------------------------------------------------------+

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum  {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

// UDP command buffer (ring buffer for incoming commands)
#define UDP_BUF_SIZE 256
static char udp_buffer[UDP_BUF_SIZE];
static volatile uint16_t udp_write_idx = 0;
static volatile uint16_t udp_read_idx = 0;

// UDP PCB (protocol control block)
static struct udp_pcb *udp_pcb = NULL;

void led_blinking_task(void);
void hid_task(void);
void process_command(char *buf);

// UDP receive callback - called by lwIP when a packet arrives
static void udp_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                               const ip_addr_t *addr, u16_t port) {
    (void)arg;
    (void)pcb;
    (void)addr;
    (void)port;

    if (p != NULL) {
        // Copy data to our buffer if there's room
        uint16_t len = p->len;
        if (len > 0 && len < UDP_BUF_SIZE - 1) {
            char *data = (char *)p->payload;
            // Simple copy - in production you'd want proper ring buffer handling
            for (uint16_t i = 0; i < len; i++) {
                udp_buffer[udp_write_idx] = data[i];
                udp_write_idx = (udp_write_idx + 1) % UDP_BUF_SIZE;
            }
            // Ensure null termination for string parsing
            if (data[len-1] != '\n' && data[len-1] != '\0') {
                udp_buffer[udp_write_idx] = '\n';
                udp_write_idx = (udp_write_idx + 1) % UDP_BUF_SIZE;
            }
        }
        pbuf_free(p);
    }
}

/*------------- MAIN -------------*/
int main(void)
{
  board_init();

  // init device stack on configured roothub port
  tud_init(BOARD_TUD_RHPORT);

  if (board_init_after_tusb) {
    board_init_after_tusb();
  }

  // Initialize WiFi chip
  if (cyw43_arch_init()) {
    // WiFi init failed - continue anyway, USB HID still works
    blink_interval_ms = 100; // Fast blink to indicate error
  } else {
    // Enable station mode
    cyw43_arch_enable_sta_mode();

    // Connect to WiFi network
    int connect_result = cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, WIFI_CONNECT_TIMEOUT_MS);

    if (connect_result == 0) {
      // Connected! Set up UDP listener
      udp_pcb = udp_new();
      if (udp_pcb != NULL) {
        err_t err = udp_bind(udp_pcb, IP_ADDR_ANY, UDP_PORT);
        if (err == ERR_OK) {
          udp_recv(udp_pcb, udp_recv_callback, NULL);
          // Success - normal blink pattern will show USB state
        }
      }
    }
    // If WiFi connect failed, USB HID still works, just no remote control
  }

  uint32_t utility_counter = 0;

  while (1)
  {
    // High priority tasks
    tud_task();
    hid_task();

    // Poll the WiFi/lwIP stack
    cyw43_arch_poll();

    // Low priority utility tasks
    // Run this block roughly every 50000 loops to reduce overhead
    if (++utility_counter > 50000)
    {
        utility_counter = 0;
        led_blinking_task();
    }
  }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+

// Process a single command string and send appropriate HID report
void process_command(char *buf)
{
  char *saveptr;
  char *token = strtok_r(buf, " ", &saveptr);

  if (token == NULL) return;

  if (strcmp(token, "keyboard") == 0)
  {
    uint8_t modifier = 0;
    uint8_t keycode[6] = {0};
    int i = 0;

    token = strtok_r(NULL, " ", &saveptr);
    if (token) {
      long val = strtol(token, NULL, 10);
      if (val >= 0 && val <= 255) modifier = (uint8_t)val;
    }

    while( (token = strtok_r(NULL, " ", &saveptr)) != NULL && i < 6)
    {
      long val = strtol(token, NULL, 10);
      if (val >= 0 && val <= 255) keycode[i++] = (uint8_t)val;
    }

    tud_hid_keyboard_report(REPORT_ID_KEYBOARD, modifier, keycode);
  }
  else if (strcmp(token, "mouse") == 0)
  {
    uint8_t buttons = 0;
    int8_t x = 0, y = 0, vertical = 0, horizontal = 0;
    long temp_val;

    token = strtok_r(NULL, " ", &saveptr);
    if (token) {
      temp_val = strtol(token, NULL, 10);
      if (temp_val >= 0 && temp_val <= 255) buttons = (uint8_t)temp_val;
    }

    token = strtok_r(NULL, " ", &saveptr);
    if (token) {
      temp_val = strtol(token, NULL, 10);
      if (temp_val >= -127 && temp_val <= 127) x = (int8_t)temp_val;
    }

    token = strtok_r(NULL, " ", &saveptr);
    if (token) {
      temp_val = strtol(token, NULL, 10);
      if (temp_val >= -127 && temp_val <= 127) y = (int8_t)temp_val;
    }

    token = strtok_r(NULL, " ", &saveptr);
    if (token) {
      temp_val = strtol(token, NULL, 10);
      if (temp_val >= -127 && temp_val <= 127) vertical = (int8_t)temp_val;
    }

    token = strtok_r(NULL, " ", &saveptr);
    if (token) {
      temp_val = strtol(token, NULL, 10);
      if (temp_val >= -127 && temp_val <= 127) horizontal = (int8_t)temp_val;
    }

    tud_hid_mouse_report(REPORT_ID_MOUSE, buttons, x, y, vertical, horizontal);
  }
  else if (strcmp(token, "consumer") == 0)
  {
    uint16_t code = 0;
    token = strtok_r(NULL, " ", &saveptr);
    if (token) {
       long val = strtol(token, NULL, 10);
       if (val >= 0 && val <= 65535) code = (uint16_t)val;
    }
    tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &code, sizeof(code));
  }
  else if (strcmp(token, "gamepad") == 0)
  {
    hid_gamepad_report_t report = {0};
    long temp_val;

    token = strtok_r(NULL, " ", &saveptr); if(token) { temp_val = strtol(token, NULL, 10); if (temp_val >=-127 && temp_val <= 127) report.x = (int8_t)temp_val; }
    token = strtok_r(NULL, " ", &saveptr); if(token) { temp_val = strtol(token, NULL, 10); if (temp_val >=-127 && temp_val <= 127) report.y = (int8_t)temp_val; }
    token = strtok_r(NULL, " ", &saveptr); if(token) { temp_val = strtol(token, NULL, 10); if (temp_val >=-127 && temp_val <= 127) report.z = (int8_t)temp_val; }
    token = strtok_r(NULL, " ", &saveptr); if(token) { temp_val = strtol(token, NULL, 10); if (temp_val >=-127 && temp_val <= 127) report.rz = (int8_t)temp_val; }
    token = strtok_r(NULL, " ", &saveptr); if(token) { temp_val = strtol(token, NULL, 10); if (temp_val >=-127 && temp_val <= 127) report.rx = (int8_t)temp_val; }
    token = strtok_r(NULL, " ", &saveptr); if(token) { temp_val = strtol(token, NULL, 10); if (temp_val >=-127 && temp_val <= 127) report.ry = (int8_t)temp_val; }

    token = strtok_r(NULL, " ", &saveptr); if(token) { temp_val = strtol(token, NULL, 10); if (temp_val >= 0 && temp_val <= 255) report.hat = (uint8_t)temp_val; }

    token = strtok_r(NULL, " ", &saveptr); if(token) { report.buttons = (uint32_t)strtoul(token, NULL, 10); }

    tud_hid_report(REPORT_ID_GAMEPAD, &report, sizeof(report));
  }
}

// This task handles reading UDP commands and sending HID reports
void hid_task(void)
{
  // Remote wakeup
  if ( tud_suspended() )
  {
    tud_remote_wakeup();
  }

  // Check for commands in UDP buffer
  static char cmd_buf[64 + 1];
  static uint8_t cmd_idx = 0;

  // Read from UDP ring buffer until we find a newline (command delimiter)
  while (udp_read_idx != udp_write_idx)
  {
    char c = udp_buffer[udp_read_idx];
    udp_read_idx = (udp_read_idx + 1) % UDP_BUF_SIZE;

    if (c == '\n' || c == '\r' || c == '\0')
    {
      if (cmd_idx > 0)
      {
        cmd_buf[cmd_idx] = '\0';
        process_command(cmd_buf);
        cmd_idx = 0;
      }
    }
    else if (cmd_idx < sizeof(cmd_buf) - 1)
    {
      cmd_buf[cmd_idx++] = c;
    }
  }
}

// Invoked when sent REPORT successfully to host
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len)
{
  (void) instance;
  (void) report;
  (void) len;
  // Body is intentionally empty.
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
  (void) instance;

  if (report_type == HID_REPORT_TYPE_OUTPUT)
  {
    // Set keyboard LED e.g Capslock, Numlock etc...
    if (report_id == REPORT_ID_KEYBOARD)
    {
      if ( bufsize < 1 ) return;

      uint8_t const kbd_leds = buffer[0];

      if (kbd_leds & KEYBOARD_LED_CAPSLOCK)
      {
        blink_interval_ms = 0;
        board_led_write(true);
      } else {
        board_led_write(false);
        blink_interval_ms = BLINK_MOUNTED;
      }
    }
  }
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // blink is disabled
  if (!blink_interval_ms) return;

  // Blink every interval ms
  if ( board_millis() - start_ms < blink_interval_ms) return;
  start_ms += blink_interval_ms;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}