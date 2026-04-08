#include "ch32fun.h"
#include "rv003usb.h"
#include "midi_uart.h"
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

#define IRAM_ATTR
#define MAX_IN_TIMEOUT 1000
#define R_GLITCH_HIGH
// #define RUNNING_ON_ESP32S2
#define BB_PRINTF_DEBUG(...) ((void)0)
#include "bitbang_rvswdio.h"

#define PROGRAMMER_PROTOCOL_NUMBER 5

#define PIN_SWCLK PC5       // Remappable.
#define PIN_TARGETPOWER PC0 // Remappable
#define POWER_ON 1
#define POWER_OFF 0
#define PIN_TARGETPOWER_MODE GPIO_CFGLR_OUT_10Mhz_PP

// #define STATUS_LED PC0
#ifdef STATUS_LED
#define STATUS_LED_OFF 0
#define STATUS_LED_ON 1
#endif

#include "bitbang_rvswdio_ch32v003.h"

#if CH5xx_SUPPORT
#include "ch5xx.h"
#endif

#define PIN_MODE_SWITCH PC3

// --- MODE SWITCH ---
typedef enum {
  MODE_PROGRAMMER = 0,
  MODE_MIDI = 1,
} usb_mode_t;

usb_mode_t usb_mode = MODE_PROGRAMMER;

// --- PROGRAMMER DATA ---
struct SWIOState state;
__attribute__((aligned(4))) uint8_t scratch[264];
__attribute__((aligned(4))) uint8_t retbuff[264];
volatile uint32_t scratch_run = 0;
volatile uint32_t scratch_return = 0;
uint8_t programmer_mode = 0;

static void SetupProgrammer(void) {
  state.t1coeff = 0;
  programmer_mode = 0;
}

static void SwitchMode(uint8_t **liptr, uint8_t **lretbuffptr) {
  programmer_mode = *((*liptr)++);
  *((*lretbuffptr)++) = PROGRAMMER_PROTOCOL_NUMBER;
  *((*lretbuffptr)++) = programmer_mode;
  BB_PRINTF_DEBUG("PM %d\n", programmer_mode);
  if (programmer_mode == 0) {
    SetupProgrammer();
  }
}

static void HandleCommandBuffer(uint8_t *buffer) {
  uint8_t *iptr = &buffer[1];
  uint8_t *retbuffptr = retbuff + 1;
#define reqlen (sizeof(scratch) - 1)
#ifdef STATUS_LED
  funDigitalWrite(STATUS_LED, STATUS_LED_ON);
#endif
  while (iptr - buffer < reqlen) {
    uint8_t cmd = *(iptr++);
    int remain = reqlen - (iptr - buffer);
    if ((sizeof(retbuff) - (retbuffptr - retbuff)) < 6)
      break;

    if (programmer_mode == 0) {
      if (cmd == 0xfe) {
        cmd = *(iptr++);
        switch (cmd) {
        case 0xfd:
          SwitchMode(&iptr, &retbuffptr);
          break;
        case 0x0e:
        case 0x01: {
          funPinMode(PIN_TARGETPOWER, PIN_TARGETPOWER_MODE);
          funDigitalWrite(PIN_TARGETPOWER, POWER_ON);
          int r = InitializeSWDSWIO(&state);
          if (cmd == 0x0e)
            *(retbuffptr++) = r;
          break;
        }
        case 0x02:
          funDigitalWrite(PIN_TARGETPOWER, POWER_OFF);
          break;
        case 0x03:
          funPinMode(PIN_TARGETPOWER, PIN_TARGETPOWER_MODE);
          funDigitalWrite(PIN_TARGETPOWER, POWER_ON);
          break;
        case 0x04:
          Delay_Us(iptr[0] | (iptr[1] << 8));
          iptr += 2;
          break;
        case 0x05:
          ResetInternalProgrammingState(&state);
          break;
        case 0x06:
          *(retbuffptr++) = WaitForFlash(&state);
          break;
        case 0x07:
          *(retbuffptr++) = WaitForDoneOp(&state);
          break;
        case 0x08: {
          if (remain >= 9) {
            int r = WriteWord(
                &state,
                iptr[0] | (iptr[1] << 8) | (iptr[2] << 16) | (iptr[3] << 24),
                iptr[4] | (iptr[5] << 8) | (iptr[6] << 16) | (iptr[7] << 24));
            iptr += 8;
            *(retbuffptr++) = r;
          }
          break;
        }
        case 0x09: {
          if (remain >= 4) {
            int r = ReadWord(&state,
                             iptr[0] | (iptr[1] << 8) | (iptr[2] << 16) |
                                 (iptr[3] << 24),
                             (uint32_t *)&retbuffptr[1]);
            iptr += 4;
            retbuffptr[0] = r;
            if (r < 0)
              memset(&retbuffptr[1], 0, 4);
            retbuffptr += 5;
          }
          break;
        }
        case 0x0a:
          ResetInternalProgrammingState(&state);
          break;
        case 0x0b:
          if (remain >= 68) {
            int r = WriteBlock(&state,
                               iptr[0] | (iptr[1] << 8) | (iptr[2] << 16) |
                                   (iptr[3] << 24),
                               (uint8_t *)&iptr[4], 64, 1);
            iptr += 68;
            *(retbuffptr++) = r;
          }
          break;
        case 0x0c:
          if (remain >= 8) {
            int use_apll = *(iptr++);
            int sdm0 = *(iptr++);
            iptr += 6;
            if (use_apll) {
              RCC->CFGR0 =
                  (RCC->CFGR0 & (~(7 << 24))) | (sdm0 << 24) | (1 << 26);
              funPinMode(PC4, GPIO_CFGLR_OUT_50Mhz_AF_PP);
            } else {
              RCC->CFGR0 &= ~(7 << 24);
            }
          }
          break;
        case 0x0d: {
          int tries = 100;
          if (remain >= 8) {
            int r;
            uint32_t leavevalA =
                iptr[0] | (iptr[1] << 8) | (iptr[2] << 16) | (iptr[3] << 24);
            iptr += 4;
            uint32_t leavevalB =
                iptr[0] | (iptr[1] << 8) | (iptr[2] << 16) | (iptr[3] << 24);
            iptr += 4;
            uint8_t *origretbuf = (retbuffptr++);
            int canrx = (sizeof(retbuff) - (retbuffptr - retbuff)) - 8;
            while (canrx > 8) {
              r = PollTerminal(&state, retbuffptr, canrx, leavevalA, leavevalB);
              origretbuf[0] = r;
              if (r >= 0) {
                retbuffptr += r;
                if (tries-- <= 0)
                  break;
              } else {
                break;
              }
              canrx = (sizeof(retbuff) - (retbuffptr - retbuff)) - 8;
              if (leavevalA != 0 || leavevalB != 0)
                break;
            }
          }
          break;
        }
        case 0x0f:
          if (remain >= 8) {
            state.target_chip_type = *(iptr++);
            state.sectorsize = iptr[0] | (iptr[1] << 8);
            iptr += 2;
            iptr += 5;
            *(retbuffptr++) = 0;
          }
          break;
        case 0x10: {
          int r = DetermineChipTypeAndSectorInfo(&state, &retbuffptr[1]);
          retbuffptr[0] = r;
          if (r < 0)
            memset(&retbuffptr[1], 0, 6);
          retbuffptr += 7;
        } break;
        case 0x12:
          if (remain >= 70) {
            uint32_t length = iptr[4];
            uint8_t erase = iptr[5];
            int r = WriteBlock(&state,
                               iptr[0] | (iptr[1] << 8) | (iptr[2] << 16) |
                                   (iptr[3] << 24),
                               (uint8_t *)&iptr[6], length, erase);
            iptr += length + 2;
            *(retbuffptr++) = r;
          }
          break;
        }
      } else if (cmd == 0xff) {
        break;
      } else {
        if (cmd & 1) {
          if (remain >= 4) {
            cmd = (cmd >> 1) + 1;
            if (cmd >= DMPROGBUF0 && cmd <= DMPROGBUF7)
              state.statetag = STTAG("XXXX");
            MCFWriteReg32(&state, cmd,
                          iptr[0] | (iptr[1] << 8) | (iptr[2] << 16) |
                              (iptr[3] << 24));
            iptr += 4;
          }
        } else {
          if (remain >= 1 && (sizeof(retbuff) - (retbuffptr - retbuff)) >= 4) {
            cmd = (cmd >> 1) + 1;
            int r = MCFReadReg32(&state, cmd, (uint32_t *)&retbuffptr[1]);
            retbuffptr[0] = r;
            if (r < 0)
              memset(&retbuffptr[1], 0, 4);
            retbuffptr += 5;
          }
        }
      }
    } else {
      *(retbuffptr++) = 0;
      *(retbuffptr++) = programmer_mode;
      break;
    }
  }
  retbuff[0] = (retbuffptr - retbuff) - 1;
#ifdef STATUS_LED
  funDigitalWrite(STATUS_LED, STATUS_LED_OFF);
#endif
}

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
    ConfigureIOForRVSWIO();
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
      if (scratch_run) {
        scratch_return = 0;
        HandleCommandBuffer(scratch);
        scratch_run = 0;
        scratch_return = 1;
      }
    }
  }
}

void usb_handle_user_in_request(struct usb_endpoint *e, uint8_t *scratchpad,
                                int endp, uint32_t sendtok,
                                struct rv003usb_internal *ist) {
  if (usb_mode == MODE_MIDI) {
    if (endp && midi_in.len) {
      usb_send_data(midi_in.buffer, midi_in.len, 0, sendtok);
      midi_in.len = 0;
    } else {
      usb_send_empty(sendtok);
    }
    return;
  }

  if (endp) {
    usb_send_empty(sendtok);
  } else {
    LogUEvent(5, endp, sendtok, scratch_run);
    if (scratch_run) {
      usb_send_data(0, 0, 2, 0x5A);
    } else {
      if (!e->max_len) {
        usb_send_empty(sendtok);
      } else {
        int len = e->max_len;
        int slen = len;
        if (slen > 8)
          slen = 8;
        usb_send_data((uint8_t *)e->opaque, slen, 0, sendtok);
        e->opaque += 8;
        len -= 8;
        if (len < 0)
          len = 0;
        e->max_len = len;
        LogUEvent(6, e->max_len, slen, len);
      }
    }
  }
}

void usb_handle_user_data(struct usb_endpoint *e, int current_endpoint,
                          uint8_t *data, int len,
                          struct rv003usb_internal *ist) {
  if (usb_mode == MODE_MIDI) {
    if (len)
      midi_receive(data);
    if (len == 8)
      midi_receive(&data[4]);
    return;
  }

  if (scratch_run) {
    usb_send_data(0, 0, 2, 0x5A);
    return;
  }

  usb_send_data(0, 0, 2, 0xD2);
  int offset = e->count << 3;
  int torx = e->max_len - offset;
  if (torx > len)
    torx = len;
  if (torx > 0) {
    memcpy(scratch + offset, data, torx);
    e->count++;
    if ((e->count << 3) >= e->max_len) {
      scratch_run = e->max_len;
    }
  }
}

void usb_handle_hid_get_report_start(struct usb_endpoint *e, int reqLen,
                                     uint32_t lValueLSBIndexMSB) {
  if (usb_mode == MODE_MIDI)
    return;
  if (reqLen > sizeof(scratch))
    reqLen = sizeof(scratch);
  e->opaque = retbuff;
  e->max_len = reqLen;
  e->custom = 1;
  scratch_return = 0;
}

void usb_handle_hid_set_report_start(struct usb_endpoint *e, int reqLen,
                                     uint32_t lValueLSBIndexMSB) {
  if (usb_mode == MODE_MIDI)
    return;
  e->opaque = scratch;
  e->custom = 0;
  if (scratch_run)
    reqLen = 0;
  if (reqLen > sizeof(scratch))
    reqLen = sizeof(scratch);
  e->max_len = reqLen;
  LogUEvent(2, reqLen, lValueLSBIndexMSB, 0);
}

void usb_handle_other_control_message(struct usb_endpoint *e, struct usb_urb *s,
                                      struct rv003usb_internal *ist) {
  LogUEvent(SysTick->CNT, s->wRequestTypeLSBRequestMSB, s->lValueLSBIndexMSB,
            s->wLength);
}
