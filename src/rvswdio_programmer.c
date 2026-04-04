#include "ch32fun.h"
#include "rv003usb.h"
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
#define PIN_TARGETPOWER PD2 // Remappable
#define POWER_ON 1
#define POWER_OFF 0
#define PIN_TARGETPOWER_MODE GPIO_CFGLR_OUT_10Mhz_PP

#define STATUS_LED PC0
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
uint8_t is_midi_mode = 0;

// --- MIDI DATA ---
typedef struct {
	volatile uint8_t len;
	uint8_t buffer[8];
} midi_message_t;

midi_message_t midi_in;

void midi_send(uint8_t * msg, uint8_t len)
{
	memcpy( midi_in.buffer, msg, len );
	midi_in.len = len;
}
#define midi_send_ready() (midi_in.len == 0)

const uint16_t noteLookup[128] = { 65535,61856,58385,55108,52015,49096,46340,43739,41284,38967,36780,34716,32767,30928,29192,27554,26007,24548,23170,21870,20642,19484,18390,17358,16384,15464,14596,13777,13004,12274,11585,10935,10321,9742,9195,8679,8192,7732,7298,6888,6502,6137,5792,5467,5161,4871,4598,4339,4096,3866,3649,3444,3251,3068,2896,2734,2580,2435,2299,2170,2048,1933,1825,1722,1625,1534,1448,1367,1290,1218,1149,1085,1024,967,912,861,813,767,724,683,645,609,575,542,512,483,456,431,406,384,362,342,323,304,287,271,256,242,228,215,203,192,181,171,161,152,144,136,128,121,114,108,102,96,91,85,81,76,72,68,64,60,57,54,51,48,45,43 };

void tim2_set_period(uint16_t period)
{
	if (period) {
		TIM2->ATRLR = period;
		TIM2->CTLR1 |= TIM_CEN;
	} else {
		TIM2->CTLR1 &= ~TIM_CEN;
	}
}

void midi_receive(uint8_t * msg)
{
	static uint8_t note;
	if (msg[1] == 0x90 && msg[3] !=0) {
		note = msg[2];
		tim2_set_period( noteLookup[ note ] );
	}
	else if (msg[2] == note && (msg[1]==0x80 || (msg[1]==0x90 && msg[3]==0)) ) {
		tim2_set_period(0);
	}
}

void scan_keyboard(void)
{
	static uint8_t keystate = 0;
	uint8_t midi_msg[4] = {0x09, 0x90, 0x40, 0x7F};
	uint16_t input = GPIOC->INDR;
	for (uint8_t i = 2; i <= 5; i++) {
		uint8_t mask = (1<<i);
		if ((input & mask) != (keystate & mask)) {
			midi_msg[2] = 0x40+i;
			if (input & mask) {
				midi_msg[3] = 0x00;
			} else {
				midi_msg[3] = 0x7F;
			}
			while (!midi_send_ready()) {};
			midi_send(midi_msg, 4);
		}
	}
	keystate = input;
}

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
      if (cmd == 0xfe)
      {
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
        case 0x08:
        {
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
        case 0x09:
        {
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
        case 0x0d:
        {
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
        case 0x10:
        {
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
		is_midi_mode = 1;
		set_usb_mode_midi();
	} else {
		is_midi_mode = 0;
		set_usb_mode_programmer();
	}

	Delay_Ms(1);
	usb_setup();

	if (is_midi_mode) {
		// MIDI Init
		// (PC2-PC5 are inputs, done above for PC3 but let's redo like demo)
		GPIOC->CFGLR &= ~(0xf<<(4*2) | 0xf<<(4*3) | 0xf<<(4*4) | 0xf<<(4*5));
		GPIOC->CFGLR |= (GPIO_Speed_In | GPIO_CNF_IN_PUPD) << (4*2) |
						(GPIO_Speed_In | GPIO_CNF_IN_PUPD) << (4*3) |
						(GPIO_Speed_In | GPIO_CNF_IN_PUPD) << (4*4) |
						(GPIO_Speed_In | GPIO_CNF_IN_PUPD) << (4*5);
		GPIOC->BSHR = GPIO_BSHR_BS2 | GPIO_BSHR_BS3 | GPIO_BSHR_BS4 | GPIO_BSHR_BS5;

		// TIM2 Init
		RCC->APB1PCENR |= RCC_APB1Periph_TIM2;
		GPIOC->CFGLR &= ~(0xf<<(4*0));
		GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP_AF)<<(4*0);
		GPIOC->CFGLR &= ~(0xf<<(4*1));
		GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP_AF)<<(4*1);
		RCC->APB2PCENR |= RCC_APB2Periph_AFIO;
		AFIO->PCFR1 |= AFIO_PCFR1_TIM2_REMAP_PARTIALREMAP2;
		TIM2->PSC = 0x0008;
		TIM2->CTLR1 |= TIM_ARPE;
		TIM2->CHCTLR1 |= TIM_OC1M_1 | TIM_OC1M_0 | TIM_OC1PE; 
		TIM2->CHCTLR2 |= TIM_OC3M_1 | TIM_OC3M_0 | TIM_OC3PE; 
		TIM2->CCER |= TIM_CC1E | TIM_CC3E | TIM_CC1P;
		TIM2->CH1CVR = 0;
		TIM2->CH3CVR = 0;
	} else {
		// Programmer Init
		funGpioInitAll();
		ConfigureIOForRVSWIO();
#ifdef STATUS_LED
		funPinMode(STATUS_LED, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
		funDigitalWrite(STATUS_LED, STATUS_LED_OFF);
#endif
	}

	while (1) {
		if (is_midi_mode) {
			scan_keyboard();
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
	if (is_midi_mode) {
		if (endp && midi_in.len) {
			usb_send_data( midi_in.buffer, midi_in.len, 0, sendtok );
			midi_in.len = 0;
		} else {
			usb_send_empty( sendtok );
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
	if (is_midi_mode) {
		if (len) midi_receive(data);
		if (len==8) midi_receive(&data[4]);
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
	if (is_midi_mode) return;
	if (reqLen > sizeof(scratch))
		reqLen = sizeof(scratch);
	e->opaque = retbuff;
	e->max_len = reqLen;
	e->custom = 1;
	scratch_return = 0;
}

void usb_handle_hid_set_report_start(struct usb_endpoint *e, int reqLen,
                                     uint32_t lValueLSBIndexMSB) {
	if (is_midi_mode) return;
	e->opaque = scratch;
	e->custom = 0;
	if (scratch_run)
		reqLen = 0;
	if (reqLen > sizeof(scratch))
		reqLen = sizeof(scratch);
	e->max_len = reqLen;
	LogUEvent(2, reqLen, lValueLSBIndexMSB, 0);
}

void usb_handle_other_control_message( struct usb_endpoint * e, struct usb_urb * s, struct rv003usb_internal * ist )
{
	LogUEvent( SysTick->CNT, s->wRequestTypeLSBRequestMSB, s->lValueLSBIndexMSB, s->wLength );
}
