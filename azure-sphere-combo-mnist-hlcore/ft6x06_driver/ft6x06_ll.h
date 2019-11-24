#ifndef FT6X06_LL_H
#define FT6X06_LL_H

#include <stdint.h>

int ft6x06_ll_i2c_init(void);
int ft6x06_ll_i2c_tx(uint8_t* tx_data, uint32_t tx_len);
int ft6x06_ll_i2c_tx_then_rx(uint8_t* tx_data, uint32_t tx_len, uint8_t* rx_data, uint32_t rx_len);

#endif