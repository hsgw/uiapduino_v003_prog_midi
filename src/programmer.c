#include "ch32fun.h"
#include "programmer.h"
#include <string.h>

#define CH5xx_SUPPORT 0
#define IRAM_ATTR
#define BB_PRINTF_DEBUG(...) ((void)0)

#define RVBB_REMAP 1 // To put SWD on PC1/PC2

#define PIN_SWCLK PC5       // Remappable.
#define PIN_TARGETPOWER PC0 // Remappable
#define POWER_ON 1
#define POWER_OFF 0
#define PIN_TARGETPOWER_MODE GPIO_CFGLR_OUT_10Mhz_PP

#ifdef STATUS_LED
#define STATUS_LED_OFF 0
#define STATUS_LED_ON 1
#endif

#define PROGRAMMER_PROTOCOL_NUMBER 5

#include "bitbang_rvswdio.h"
#include "bitbang_rvswdio_ch32v003.h"

#if CH5xx_SUPPORT
#include "ch5xx.h"
#endif

// --- PROGRAMMER DATA ---
struct SWIOState state;
__attribute__((aligned(4))) uint8_t scratch[264];
__attribute__((aligned(4))) uint8_t retbuff[264];
volatile uint32_t scratch_run = 0;
volatile uint32_t scratch_return = 0;
uint8_t programmer_mode = 0;

void programmer_setup(void) {
  state.t1coeff = 0;
  programmer_mode = 0;
}

void programmer_configure_io_for_rvswio(void) {
  ConfigureIOForRVSWIO();
}

static void SwitchMode(uint8_t **liptr, uint8_t **lretbuffptr) {
  programmer_mode = *((*liptr)++);
  *((*lretbuffptr)++) = PROGRAMMER_PROTOCOL_NUMBER;
  *((*lretbuffptr)++) = programmer_mode;
  BB_PRINTF_DEBUG("PM %d\n", programmer_mode);
  if (programmer_mode == 0) {
    programmer_setup();
  }
}

void programmer_handle_command_buffer(uint8_t *buffer) {
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
