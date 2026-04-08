#include "ch32fun.h"
#include "midi_uart.h"
#include <string.h>

// MIDI UART RX buffer
uint8_t midi_uart_rx_buffer[MIDI_UART_BUFF_SIZE];
volatile uint16_t midi_uart_rx_read_ptr = 0;

// MIDI TX Ring Buffer
uint8_t midi_uart_tx_buffer[MIDI_TX_BUFF_SIZE];
volatile uint16_t midi_uart_tx_head = 0; // Where we add data
volatile uint16_t midi_uart_tx_tail = 0; // Where DMA reads from
volatile uint8_t midi_uart_tx_active = 0;
volatile uint16_t midi_uart_tx_last_len = 0;

// MIDI IN message
midi_message_t midi_in;

void start_midi_tx_dma(void) {
  if (midi_uart_tx_active || midi_uart_tx_head == midi_uart_tx_tail)
    return;

  uint16_t len = (midi_uart_tx_head > midi_uart_tx_tail)
                     ? (midi_uart_tx_head - midi_uart_tx_tail)
                     : (MIDI_TX_BUFF_SIZE - midi_uart_tx_tail);

  midi_uart_tx_last_len = len;
  midi_uart_tx_active = 1;

  DMA1_Channel4->CFGR &= ~DMA_CFGR1_EN;
  DMA1_Channel4->CNTR = len;
  DMA1_Channel4->MADDR = (uint32_t)&midi_uart_tx_buffer[midi_uart_tx_tail];
  DMA1_Channel4->CFGR |= DMA_CFGR1_EN;
}

void DMA1_Channel4_IRQHandler(void) __attribute__((interrupt));
void DMA1_Channel4_IRQHandler(void) {
  DMA1->INTFCR = DMA_CTCIF4; // Clear Transfer Complete flag

  midi_uart_tx_tail =
      (midi_uart_tx_tail + midi_uart_tx_last_len) % MIDI_TX_BUFF_SIZE;
  midi_uart_tx_active = 0;
  start_midi_tx_dma();
}

void midi_send(uint8_t *msg, uint8_t len) {
  memcpy(midi_in.buffer, msg, len);
  midi_in.len = len;
}

void USART1_IRQHandler(void) __attribute__((interrupt));
void USART1_IRQHandler(void) {
  if (USART1->STATR & USART_STATR_IDLE) {
    // Clear IDLE flag by reading STATR then DATAR
    volatile uint32_t temp;
    temp = USART1->STATR;
    temp = USART1->DATAR;
    (void)temp;
    // The main loop will handle the data by checking DMA CNTR
  }
}

void uart_midi_init(void) {
  // Enable UART1, GPIOD, AFIO and DMA1, SRAM
  RCC->APB2PCENR |=
      RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOD | RCC_APB2Periph_AFIO;
  RCC->AHBPCENR |= RCC_AHBPeriph_DMA1 | RCC_AHBPeriph_SRAM;

  // PD6 as UART1 RX (Input floating / pull-up)
  GPIOD->CFGLR &= ~(0xf << (4 * 6));
  GPIOD->CFGLR |= (GPIO_Speed_In | GPIO_CNF_IN_PUPD) << (4 * 6);
  GPIOD->BSHR = GPIO_BSHR_BS6; // Pull-up for PD6

  // PD5 as UART1 TX (10MHz Output alt func, push-pull)
  GPIOD->CFGLR &= ~(0xf << (4 * 5));
  GPIOD->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP_AF) << (4 * 5);

  // UART1 Configuration: 31250 bps @ 48MHz
  // BRR = 48,000,000 / 31250 = 1536 (0x0600)
  USART1->BRR = 0x0600;
  USART1->CTLR1 =
      USART_CTLR1_TE | USART_CTLR1_RE | USART_CTLR1_UE | USART_CTLR1_IDLEIE;

  // DMA1 Channel 5 (USART1 RX) Configuration
  DMA1_Channel5->PADDR = (uint32_t)&USART1->DATAR;
  DMA1_Channel5->MADDR = (uint32_t)midi_uart_rx_buffer;
  DMA1_Channel5->CNTR = MIDI_UART_BUFF_SIZE;
  DMA1_Channel5->CFGR =
      DMA_CFGR1_MINC | DMA_CFGR1_CIRC |
      DMA_CFGR1_PL_1; // Memory increment, Circular, High priority
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

void process_midi_uart_to_usb(void) {
  static uint8_t midi_msg[4];
  static uint8_t midi_idx = 0;
  static uint8_t expected_len = 0;
  static uint8_t running_status = 0;

  uint16_t dma_curr_ptr = MIDI_UART_BUFF_SIZE - DMA1_Channel5->CNTR;
  while (midi_uart_rx_read_ptr != dma_curr_ptr) {
    uint8_t byte = midi_uart_rx_buffer[midi_uart_rx_read_ptr];
    midi_uart_rx_read_ptr = (midi_uart_rx_read_ptr + 1) % MIDI_UART_BUFF_SIZE;

    if (byte & 0x80) {
      // Status byte
      if (byte < 0xF8) { // Not Real-time message
        midi_msg[1] = byte;
        midi_idx = 2;

        if (byte < 0xF0) {
          running_status = byte;
        } else {
          running_status = 0; // System Common cancels running status
        }

        uint8_t type = byte & 0xF0;
        if (type == 0xC0 || type == 0xD0)
          expected_len = 2;
        else if (type == 0xF0) {
          // System Common
          if (byte == 0xF1 || byte == 0xF3)
            expected_len = 2;
          else if (byte == 0xF2)
            expected_len = 3;
          else if (byte == 0xF6)
            expected_len = 1;
          else {
            // SysEx (F0) or undefined (F4, F5) or EOX (F7)
            expected_len = 0; // Skip
            midi_idx = 0;
          }
        } else
          expected_len = 3;
      } else {
        // Real-time message (1 byte)
        uint8_t rt_msg[4] = {0x0F, byte, 0, 0};
        while (!midi_send_ready())
          ;
        midi_send(rt_msg, 4);
        continue;
      }
    } else if (running_status) {
      // Data byte with running status
      if (midi_idx < 4) {
        midi_msg[midi_idx++] = byte;
      }
    }

    if (midi_idx > 1 && midi_idx == (expected_len + 1)) {
      // Message complete
      uint8_t cin = (midi_msg[1] >> 4);
      if (cin == 0xF) {
        // System Common
        if (midi_msg[1] == 0xF1 || midi_msg[1] == 0xF3)
          cin = 0x2; // 2-byte System Common
        else if (midi_msg[1] == 0xF2)
          cin = 0x3; // 3-byte System Common
        else
          cin = 0x5; // 1-byte System Common (F6)
      }
      midi_msg[0] = cin;
      while (!midi_send_ready())
        ;
      midi_send(midi_msg, 4);

      // Reset for running status
      midi_idx = 2;
    }
  }
}

void midi_receive(uint8_t *msg) {
  // USB MIDI (4 bytes) -> Serial MIDI (1-3 bytes)
  uint8_t cin = msg[0] & 0x0F;
  uint8_t len = 0;

  switch (cin) {
  case 0x5:
  case 0xF:
    len = 1;
    break;
  case 0x2:
  case 0xC:
  case 0xD:
    len = 2;
    break;
  case 0x3:
  case 0x8:
  case 0x9:
  case 0xA:
  case 0xB:
  case 0xE:
    len = 3;
    break;
  }

  for (uint8_t i = 0; i < len; i++) {
    uint16_t next_head = (midi_uart_tx_head + 1) % MIDI_TX_BUFF_SIZE;
    // If buffer is full, we might have to wait or drop.
    // For MIDI speed vs USB speed, it might fill if we spam.
    while (next_head == midi_uart_tx_tail)
      ;

    midi_uart_tx_buffer[midi_uart_tx_head] = msg[i + 1];
    midi_uart_tx_head = next_head;
  }
  start_midi_tx_dma();
}
