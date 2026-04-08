#include "ch32fun.h"
#include "rv003usb.h"
#include "midi_uart.h"
#include "programmer.h"
#include <string.h>

#define CH5xx_SUPPORT 0

// For RVBB_REMAP
//        PC1+PC2 = SWIO (Plus 5.1k pull-up to PD2)
// Otherwise (Ideal for 8-pin SOIC, because PC4+PA1 are bound)
//        PC4+PA1 = SWIO (Plus 5.1k pull-up to PD2)
//
// By Default:
//        PD2 = Power control
//
// If programming V2xx, V3xx, X0xx, DEFAULT:
//        PC5 = SWCLK
//
// If you are using the MCO feature
//        PC4 = MCO (optional)
//
#define RVBB_REMAP 1 // To put SWD on PC1/PC2

// These are needed for main file (target power control)
#define PIN_TARGETPOWER PC0
#define POWER_ON 1
#define PIN_TARGETPOWER_MODE GPIO_CFGLR_OUT_10Mhz_PP

// #define STATUS_LED PC0
#ifdef STATUS_LED
#define STATUS_LED_OFF 0
#define STATUS_LED_ON 1
#endif

#define PIN_MODE_SWITCH PC3

// --- MODE SWITCH ---
typedef enum {
  MODE_PROGRAMMER = 0,
  MODE_MIDI = 1,
} usb_mode_t;

usb_mode_t usb_mode = MODE_PROGRAMMER;

int main() {
  SystemInit();

  // Mode check on PIN_MODE_SWITCH
  funGpioInitAll();
  funPinMode(PIN_MODE_SWITCH, GPIO_Speed_In | GPIO_CNF_IN_PUPD);
  funDigitalWrite(PIN_MODE_SWITCH, 1); // Pull-up
  Delay_Ms(10);
  if (funDigitalRead(PIN_MODE_SWITCH)) {
    usb_mode = MODE_MIDI;
    set_usb_mode_midi();
  } else {
    usb_mode = MODE_PROGRAMMER;
    set_usb_mode_programmer();
  }

  Delay_Ms(1);
  usb_setup();

  if (usb_mode == MODE_MIDI) {
    uart_midi_init();
  } else {
    // Programmer Init
    funGpioInitAll();
    programmer_configure_io_for_rvswio();
    programmer_setup();
#ifdef STATUS_LED
    funPinMode(STATUS_LED, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
    funDigitalWrite(STATUS_LED, STATUS_LED_OFF);
#endif
  }

  // This is required in all USB modes. Turn on the target power.
  funPinMode(PIN_TARGETPOWER, PIN_TARGETPOWER_MODE);
  funDigitalWrite(PIN_TARGETPOWER, POWER_ON);

  while (1) {
    if (usb_mode == MODE_MIDI) {
      process_midi_uart_to_usb();
    } else {
      programmer_process_pending();
    }
  }
}

void usb_handle_user_in_request(struct usb_endpoint *e, uint8_t *scratchpad,
                                int endp, uint32_t sendtok,
                                struct rv003usb_internal *ist) {
  if (usb_mode == MODE_MIDI) {
    midi_usb_handle_in_request(e, scratchpad, endp, sendtok, ist);
  } else {
    programmer_usb_handle_in_request(e, scratchpad, endp, sendtok, ist);
  }
}

void usb_handle_user_data(struct usb_endpoint *e, int current_endpoint,
                          uint8_t *data, int len,
                          struct rv003usb_internal *ist) {
  if (usb_mode == MODE_MIDI) {
    midi_usb_handle_data(e, current_endpoint, data, len, ist);
  } else {
    programmer_usb_handle_data(e, current_endpoint, data, len, ist);
  }
}

void usb_handle_hid_get_report_start(struct usb_endpoint *e, int reqLen,
                                     uint32_t lValueLSBIndexMSB) {
  if (usb_mode == MODE_MIDI) {
    midi_usb_handle_hid_get_report_start(e, reqLen, lValueLSBIndexMSB);
  } else {
    programmer_usb_handle_hid_get_report_start(e, reqLen, lValueLSBIndexMSB);
  }
}

void usb_handle_hid_set_report_start(struct usb_endpoint *e, int reqLen,
                                     uint32_t lValueLSBIndexMSB) {
  if (usb_mode == MODE_MIDI) {
    midi_usb_handle_hid_set_report_start(e, reqLen, lValueLSBIndexMSB);
  } else {
    programmer_usb_handle_hid_set_report_start(e, reqLen, lValueLSBIndexMSB);
  }
}

void usb_handle_other_control_message(struct usb_endpoint *e, struct usb_urb *s,
                                      struct rv003usb_internal *ist) {
  LogUEvent(SysTick->CNT, s->wRequestTypeLSBRequestMSB, s->lValueLSBIndexMSB,
            s->wLength);
}
