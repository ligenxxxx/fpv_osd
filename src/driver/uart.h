#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

int uart_set_opt(int fd, int nSpeed, int nBits, char nEvent, int nStop);
int uart_open();
void uart_close(int fd);
int uart_write(int fd, uint8_t *data, int len);
int uart_write_byte(int fd, uint8_t data);
int uart_read(int fd, uint8_t *data, int len);
int uart_read_byte(int fd, uint8_t *data);

#ifdef __cplusplus
}
#endif
