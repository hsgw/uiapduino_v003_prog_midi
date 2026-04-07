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
#define MIDI_UART_BUFF_SIZE 128
uint8_t midi_uart_rx_buffer[MIDI_UART_BUFF_SIZE];
volatile uint16_t midi_uart_rx_read_ptr = 0;

// MIDI TX Ring Buffer
#define MIDI_TX_BUFF_SIZE 128
uint8_t midi_uart_tx_buffer[MIDI_TX_BUFF_SIZE];
volatile uint16_t midi_uart_tx_head = 0; // Where we add data
volatile uint16_t midi_uart_tx_tail = 0; // Where DMA reads from
volatile uint8_t midi_uart_tx_active = 0;
volatile uint16_t midi_uart_tx_last_len = 0;

void start_midi_tx_dma(void)
{
	if (midi_uart_tx_active || midi_uart_tx_head == midi_uart_tx_tail) return;

	uint16_t len = (midi_uart_tx_head > midi_uart_tx_tail) ? 
                   (midi_uart_tx_head - midi_uart_tx_tail) : 
                   (MIDI_TX_BUFF_SIZE - midi_uart_tx_tail);
	
	midi_uart_tx_last_len = len;
	midi_uart_tx_active = 1;

	DMA1_Channel4->CFGR &= ~DMA_CFGR1_EN;
	DMA1_Channel4->CNTR = len;
	DMA1_Channel4->MADDR = (uint32_t)&midi_uart_tx_buffer[midi_uart_tx_tail];
	DMA1_Channel4->CFGR |= DMA_CFGR1_EN;
}

void DMA1_Channel4_IRQHandler(void) __attribute__((interrupt));
void DMA1_Channel4_IRQHandler(void)
{
	DMA1->INTFCR = DMA_CTCIF4; // Clear Transfer Complete flag
	midi_uart_tx_tail = (midi_uart_tx_tail + midi_uart_tx_last_len) % MIDI_TX_BUFF_SIZE;
	midi_uart_tx_active = 0;
	start_midi_tx_dma();
}

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

void USART1_IRQHandler(void) __attribute__((interrupt));
void USART1_IRQHandler(void)
{
	if(USART1->STATR & USART_STATR_IDLE) {
		// Clear IDLE flag by reading STATR then DATAR
		volatile uint32_t temp;
		temp = USART1->STATR;
		temp = USART1->DATAR;
		(void)temp;
		// The main loop will handle the data by checking DMA CNTR
	}
}

void uart_midi_init(void)
{
	// Enable UART1, GPIOD, AFIO and DMA1, SRAM
	RCC->APB2PCENR |= RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOD | RCC_APB2Periph_AFIO;
	RCC->AHBPCENR |= RCC_AHBPeriph_DMA1 | RCC_AHBPeriph_SRAM;

	// UART1 to PD5(TX) and PD6(RX) (Default mapping)
	AFIO->PCFR1 &= ~AFIO_PCFR1_USART1_REMAP;

	// PD5 as UART1 TX (10MHz Output alt func, push-pull)
	GPIOD->CFGLR &= ~(0xf << (4 * 5));
	GPIOD->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP_AF) << (4 * 5);

	// PD6 as UART1 RX (Input floating / pull-up)
	GPIOD->CFGLR &= ~(0xf << (4 * 6));
	GPIOD->CFGLR |= (GPIO_Speed_In | GPIO_CNF_IN_PUPD) << (4 * 6);
	GPIOD->BSHR = GPIO_BSHR_BS6; // Pull-up

	// UART1 Configuration: 31250 bps @ 48MHz
	// BRR = 48,000,000 / 31250 = 1536 (0x0600)
	USART1->BRR = 0x0600;
	USART1->CTLR1 = USART_CTLR1_TE | USART_CTLR1_RE | USART_CTLR1_UE | USART_CTLR1_IDLEIE;

	// DMA1 Channel 5 (USART1 RX) Configuration
	DMA1_Channel5->PADDR = (uint32_t)&USART1->DATAR;
	DMA1_Channel5->MADDR = (uint32_t)midi_uart_rx_buffer;
	DMA1_Channel5->CNTR = MIDI_UART_BUFF_SIZE;
	DMA1_Channel5->CFGR = DMA_CFGR1_MINC | DMA_CFGR1_CIRC | DMA_CFGR1_PL_1; // Memory increment, Circular, High priority
	DMA1_Channel5->CFGR |= DMA_CFGR1_EN;

	// DMA1 Channel 4 (USART1 TX) Configuration
	DMA1_Channel4->PADDR = (uint32_t)&USART1->DATAR;
	DMA1_Channel4->CFGR = DMA_CFGR1_MINC | DMA_CFGR1_DIR | DMA_CFGR1_TCIE;
	NVIC_EnableIRQ(DMA1_Channel4_IRQn);

	// Enable UART1 DMA RX/TX
	USART1->CTLR3 |= USART_CTLR3_DMAR | USART_CTLR3_DMAT;

	// Enable UART1 IDLE Interrupt in NVIC
	NVIC_EnableIRQ(USART1_IRQn);
}

void process_midi_uart_to_usb(void)
{
	static uint8_t midi_msg[4];
	static uint8_t midi_idx = 0;
	static uint8_t expected_len = 0;
	static uint8_t running_status = 0;

	uint16_t dma_curr_ptr = MIDI_UART_BUFF_SIZE - DMA1_Channel5->CNTR;

	while(midi_uart_rx_read_ptr != dma_curr_ptr) {
		uint8_t byte = midi_uart_rx_buffer[midi_uart_rx_read_ptr];
		midi_uart_rx_read_ptr = (midi_uart_rx_read_ptr + 1) % MIDI_UART_BUFF_SIZE;

		if(byte & 0x80) {
			// Status byte
			if(byte < 0xF8) { // Not Real-time message
				midi_msg[1] = byte;
				midi_idx = 2;
				
				if (byte < 0xF0) {
					running_status = byte;
				} else {
					running_status = 0; // System Common cancels running status
				}
				
				uint8_t type = byte & 0xF0;
				if(type == 0xC0 || type == 0xD0) expected_len = 2;
				else if(type == 0xF0) {
					// System Common
					if(byte == 0xF1 || byte == 0xF3) expected_len = 2;
					else if(byte == 0xF2) expected_len = 3;
					else if(byte == 0xF6) expected_len = 1;
					else {
						// SysEx (F0) or undefined (F4, F5) or EOX (F7)
						expected_len = 0; // Skip
						midi_idx = 0;
					}
				} else expected_len = 3;
			} else {
				// Real-time message (1 byte)
				uint8_t rt_msg[4] = {0x0F, byte, 0, 0};
				while(!midi_send_ready());
				midi_send(rt_msg, 4);
				continue;
			}
		} else if(running_status) {
			// Data byte with running status
			if (midi_idx < 4) {
				midi_msg[midi_idx++] = byte;
			}
		}

		if(midi_idx > 1 && midi_idx == (expected_len + 1)) {
			// Message complete
			uint8_t cin = (midi_msg[1] >> 4);
			if (cin == 0xF) {
				// System Common
				if (midi_msg[1] == 0xF1 || midi_msg[1] == 0xF3) cin = 0x2; // 2-byte System Common
				else if (midi_msg[1] == 0xF2) cin = 0x3;                   // 3-byte System Common
				else cin = 0x5;                                            // 1-byte System Common (F6)
			}
			midi_msg[0] = cin;
			while(!midi_send_ready());
			midi_send(midi_msg, 4);
			
			// Reset for running status
			midi_idx = 2;
		}
	}
}

void midi_receive(uint8_t * msg)
{
	// USB MIDI (4 bytes) -> Serial MIDI (1-3 bytes)
	uint8_t cin = msg[0] & 0x0F;
	uint8_t len = 0;

	switch(cin) {
		case 0x5: case 0xF: len = 1; break;
		case 0x2: case 0xC: case 0xD: len = 2; break;
		case 0x3: case 0x8: case 0x9: case 0xA: case 0xB: case 0xE: len = 3; break;
	}

	for(uint8_t i = 0; i < len; i++) {
		uint16_t next_head = (midi_uart_tx_head + 1) % MIDI_TX_BUFF_SIZE;
		// If buffer is full, we might have to wait or drop.
		// For MIDI speed vs USB speed, it might fill if we spam.
		while (next_head == midi_uart_tx_tail);

		midi_uart_tx_buffer[midi_uart_tx_head] = msg[i+1];
		midi_uart_tx_head = next_head;
	}
	start_midi_tx_dma();
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

	while (1) {
		if (is_midi_mode) {
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
