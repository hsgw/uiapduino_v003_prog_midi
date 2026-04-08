#ifndef _PROGRAMMER_H
#define _PROGRAMMER_H

#include <stdint.h>

extern __attribute__((aligned(4))) uint8_t scratch[264];
extern __attribute__((aligned(4))) uint8_t retbuff[264];
extern volatile uint32_t scratch_run;
extern volatile uint32_t scratch_return;
extern uint8_t programmer_mode;

// Initialize programmer mode
void programmer_setup(void);

// Handle programmer command buffer
void programmer_handle_command_buffer(uint8_t *buffer);

// Configure IO for RVSWIO
void programmer_configure_io_for_rvswio(void);

#endif // _PROGRAMMER_H
