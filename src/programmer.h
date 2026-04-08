#ifndef _PROGRAMMER_H
#define _PROGRAMMER_H

#include <stdint.h>

// Forward declarations for RV003USB types
struct usb_endpoint;
struct rv003usb_internal;

// Initialize programmer mode
void programmer_setup(void);

// Handle programmer command buffer
void programmer_handle_command_buffer(uint8_t *buffer);

// Configure IO for RVSWIO
void programmer_configure_io_for_rvswio(void);

// Process pending programmer command (returns 1 if processed, 0 if no pending)
int programmer_process_pending(void);

// USB callback handlers for Programmer mode
void programmer_usb_handle_in_request(struct usb_endpoint *e, uint8_t *scratchpad,
                                      int endp, uint32_t sendtok,
                                      struct rv003usb_internal *ist);
void programmer_usb_handle_data(struct usb_endpoint *e, int current_endpoint,
                                uint8_t *data, int len,
                                struct rv003usb_internal *ist);
void programmer_usb_handle_hid_get_report_start(struct usb_endpoint *e, int reqLen,
                                                uint32_t lValueLSBIndexMSB);
void programmer_usb_handle_hid_set_report_start(struct usb_endpoint *e, int reqLen,
                                                uint32_t lValueLSBIndexMSB);

#endif // _PROGRAMMER_H
