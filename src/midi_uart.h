#ifndef _MIDI_UART_H
#define _MIDI_UART_H

#include <stdint.h>

// Forward declarations for RV003USB types
struct usb_endpoint;
struct rv003usb_internal;

// --- MIDI DATA ---
#define MIDI_UART_BUFF_SIZE 128
#define MIDI_TX_BUFF_SIZE 128

typedef struct {
  volatile uint8_t len;
  uint8_t buffer[8];
} midi_message_t;

extern midi_message_t midi_in;
extern uint8_t midi_uart_rx_buffer[MIDI_UART_BUFF_SIZE];
extern volatile uint16_t midi_uart_rx_read_ptr;
extern uint8_t midi_uart_tx_buffer[MIDI_TX_BUFF_SIZE];
extern volatile uint16_t midi_uart_tx_head;
extern volatile uint16_t midi_uart_tx_tail;
extern volatile uint8_t midi_uart_tx_active;
extern volatile uint16_t midi_uart_tx_last_len;

// Initialize UART for MIDI communication
void uart_midi_init(void);

// Process received MIDI data from UART and send via USB
void process_midi_uart_to_usb(void);

// Send USB MIDI message
void midi_send(uint8_t *msg, uint8_t len);
#define midi_send_ready() (midi_in.len == 0)

// Receive USB MIDI data and send to UART
void midi_receive(uint8_t *msg);

// Start DMA transmission for MIDI UART TX
void start_midi_tx_dma(void);

// USB callback handlers for MIDI mode
void midi_usb_handle_in_request(struct usb_endpoint *e, uint8_t *scratchpad,
                                int endp, uint32_t sendtok,
                                struct rv003usb_internal *ist);
void midi_usb_handle_data(struct usb_endpoint *e, int current_endpoint,
                          uint8_t *data, int len,
                          struct rv003usb_internal *ist);
void midi_usb_handle_hid_get_report_start(struct usb_endpoint *e, int reqLen,
                                          uint32_t lValueLSBIndexMSB);
void midi_usb_handle_hid_set_report_start(struct usb_endpoint *e, int reqLen,
                                          uint32_t lValueLSBIndexMSB);

#endif // _MIDI_UART_H
