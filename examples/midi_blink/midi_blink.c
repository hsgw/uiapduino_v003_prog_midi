/**
 * MIDI Blink Example for CH32V006
 * 
 * This example demonstrates basic MIDI UART communication with LED blink feedback.
 * When a MIDI message is received, the LED blinks to provide visual confirmation.
 * 
 * Hardware Setup:
 * - LED connected to PC0 (with appropriate current-limiting resistor)
 * - MIDI IN circuit connected to PD6 (UART1 RX)
 * - MIDI OUT circuit connected to PD5 (UART1 TX)
 * 
 * Baud Rate: 31250 (standard MIDI speed)
 */

#include "ch32fun.h"
#include <stdio.h>

// Pin definitions
#define LED_PIN PC0
#define MIDI_RX_PIN PD6
#define MIDI_TX_PIN PD5

// MIDI UART RX buffer
#define MIDI_RX_BUFFER_SIZE 128
static volatile uint8_t midi_rx_buffer[MIDI_RX_BUFFER_SIZE];
static volatile uint16_t midi_rx_read_ptr = 0;

// MIDI TX buffer for echo-back
#define MIDI_TX_BUFFER_SIZE 128
static volatile uint8_t midi_tx_buffer[MIDI_TX_BUFFER_SIZE];
static volatile uint16_t midi_tx_head = 0;
static volatile uint16_t midi_tx_tail = 0;
static volatile uint8_t midi_tx_active = 0;
static volatile uint16_t midi_tx_last_len = 0;

// LED blink control
static volatile uint8_t blink_trigger = 0;
static volatile uint16_t blink_counter = 0;

void midi_uart_init(void) {
  // Enable UART1 and DMA1
  RCC->APB2PCENR |= RCC_APB2Periph_USART1;
  RCC->AHBPCENR |= RCC_AHBPeriph_DMA1;

  // Configure UART pins using ch32fun
  // TX: Alternate function push-pull
  funPinMode(MIDI_TX_PIN, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP_AF);
  // RX: Input with pull-up
  funPinMode(MIDI_RX_PIN, GPIO_Speed_In | GPIO_CNF_IN_PUPD);
  funDigitalWrite(MIDI_RX_PIN, FUN_HIGH); // Enable pull-up

  // UART1 Configuration: 31250 bps @ 48MHz
  // BRR = 48,000,000 / 31250 = 1536 (0x0600)
  USART1->BRR = 0x0600;
  USART1->CTLR1 = USART_CTLR1_TE | USART_CTLR1_RE | USART_CTLR1_UE | 
                  USART_CTLR1_IDLEIE;

  // DMA1 Channel 5 (USART1 RX) Configuration
  DMA1_Channel5->PADDR = (uint32_t)&USART1->DATAR;
  DMA1_Channel5->MADDR = (uint32_t)midi_rx_buffer;
  DMA1_Channel5->CNTR = MIDI_RX_BUFFER_SIZE;
  DMA1_Channel5->CFGR = DMA_CFGR1_MINC | DMA_CFGR1_CIRC | DMA_CFGR1_PL_1;
  DMA1_Channel5->CFGR |= DMA_CFGR1_EN;

  // DMA1 Channel 4 (USART1 TX) Configuration
  DMA1_Channel4->PADDR = (uint32_t)&USART1->DATAR;
  DMA1_Channel4->CFGR = DMA_CFGR1_MINC | DMA_CFGR1_DIR | DMA_CFGR1_TCIE;
  NVIC_EnableIRQ(DMA1_Channel4_IRQn);

  // Enable UART1 DMA RX/TX and IDLE interrupt
  USART1->CTLR3 |= USART_CTLR3_DMAR | USART_CTLR3_DMAT;
  NVIC_EnableIRQ(USART1_IRQn);
}

// DMA1 Channel 4 TX Complete Interrupt
void DMA1_Channel4_IRQHandler(void) __attribute__((interrupt));
void DMA1_Channel4_IRQHandler(void) {
  DMA1->INTFCR = DMA_CTCIF4; // Clear Transfer Complete flag

  // Update tail pointer
  midi_tx_tail = (midi_tx_tail + midi_tx_last_len) % MIDI_TX_BUFFER_SIZE;
  midi_tx_active = 0;
  
  // Start next transmission if data is waiting
  if (midi_tx_head != midi_tx_tail) {
    uint16_t len = (midi_tx_head > midi_tx_tail) 
                       ? (midi_tx_head - midi_tx_tail)
                       : (MIDI_TX_BUFFER_SIZE - midi_tx_tail);
    
    midi_tx_last_len = len;
    midi_tx_active = 1;
    
    DMA1_Channel4->CFGR &= ~DMA_CFGR1_EN;
    DMA1_Channel4->CNTR = len;
    DMA1_Channel4->MADDR = (uint32_t)&midi_tx_buffer[midi_tx_tail];
    DMA1_Channel4->CFGR |= DMA_CFGR1_EN;
  }
}

void start_midi_tx(const uint8_t *data, uint8_t len) {
  // Add data to TX buffer
  for (uint8_t i = 0; i < len; i++) {
    uint16_t next_head = (midi_tx_head + 1) % MIDI_TX_BUFFER_SIZE;
    // Wait if buffer is full (shouldn't happen at MIDI speeds)
    while (next_head == midi_tx_tail);
    midi_tx_buffer[midi_tx_head] = data[i];
    midi_tx_head = next_head;
  }
  
  // Start DMA transmission if not already active
  if (!midi_tx_active && midi_tx_head != midi_tx_tail) {
    uint16_t len = (midi_tx_head > midi_tx_tail) 
                       ? (midi_tx_head - midi_tx_tail)
                       : (MIDI_TX_BUFFER_SIZE - midi_tx_tail);
    
    midi_tx_last_len = len;
    midi_tx_active = 1;
    
    DMA1_Channel4->CNTR = len;
    DMA1_Channel4->MADDR = (uint32_t)&midi_tx_buffer[midi_tx_tail];
    DMA1_Channel4->CFGR |= DMA_CFGR1_EN;
  }
}

void USART1_IRQHandler(void) __attribute__((interrupt));
void USART1_IRQHandler(void) {
  if (USART1->STATR & USART_STATR_IDLE) {
    // Clear IDLE flag by reading STATR then DATAR
    volatile uint32_t temp;
    temp = USART1->STATR;
    temp = USART1->DATAR;
    (void)temp;
    // Data will be processed in main loop
  }
}

void led_init(void) {
  funPinMode(LED_PIN, GPIO_Speed_10MHz | GPIO_CNF_OUT_PP);
  funDigitalWrite(LED_PIN, FUN_LOW); // LED off initially
}

void blink_led(void) {
  blink_trigger = 1;
  blink_counter = 0;
}

void process_midi_rx(void) {
  static uint8_t midi_msg[4];
  static uint8_t midi_idx = 0;
  static uint8_t expected_len = 0;
  static uint8_t running_status = 0;

  uint16_t dma_curr_ptr = MIDI_RX_BUFFER_SIZE - DMA1_Channel5->CNTR;
  
  while (midi_rx_read_ptr != dma_curr_ptr) {
    uint8_t byte = midi_rx_buffer[midi_rx_read_ptr];
    midi_rx_read_ptr = (midi_rx_read_ptr + 1) % MIDI_RX_BUFFER_SIZE;

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
            expected_len = 0;
            midi_idx = 0;
          }
        } else
          expected_len = 3;
      } else {
        // Real-time message (1 byte) - blink LED and echo-back
        blink_led();
        start_midi_tx(&byte, 1);
        continue;
      }
    } else if (running_status) {
      // Data byte with running status
      if (midi_idx < 4) {
        midi_msg[midi_idx++] = byte;
      }
    }

    if (midi_idx > 1 && midi_idx == (expected_len + 1)) {
      // Message complete - blink LED
      blink_led();
      
      // Echo-back: send the MIDI message back
      start_midi_tx(midi_msg, expected_len);
      
      // Reset for running status
      midi_idx = 2;
    }
  }
}

int main() {
  SystemInit();
  funGpioInitAll(); // Enable all GPIO clocks

  // Initialize LED
  led_init();

  // Initialize MIDI UART
  midi_uart_init();

  // Main loop
  while (1) {
    // Process incoming MIDI data
    process_midi_rx();

    // Handle LED blink
    if (blink_trigger) {
      funDigitalWrite(LED_PIN, FUN_HIGH); // LED on
      blink_counter++;
      
      if (blink_counter > 5000) { // Blink duration (~100ms at 48MHz)
        funDigitalWrite(LED_PIN, FUN_LOW); // LED off
        blink_trigger = 0;
        blink_counter = 0;
      }
    }
  }
}
