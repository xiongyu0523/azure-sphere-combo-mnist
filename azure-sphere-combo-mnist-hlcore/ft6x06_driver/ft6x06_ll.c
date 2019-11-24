#if defined(AzureSphere_CA7)

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <applibs/log.h>
#include "applibs_versions.h"
#include <applibs/i2c.h>

#include <hw/sample_hardware.h>

static int i2cFd;

#elif defined(AzureSphere_CM4)

#include <stdlib.h>
#include <string.h>	

#include "I2CMaster.h"	
#include "Log_Debug.h"	

#define FT6X06_I2C		MT3620_UNIT_ISU2	

static I2CMaster* I2cHandler;

#endif

#define FT6206_I2C_ADDR			0x38

#include "ft6x06_ll.h"

int ft6x06_ll_i2c_init(void)
{
#if defined(AzureSphere_CA7)

	i2cFd = I2CMaster_Open(FT6X06_I2C);
	if (i2cFd < 0) {
		Log_Debug("ERROR: I2CMaster_Open: errno=%d (%s)\r\n", errno, strerror(errno));
		return -1;
	}

	int ret = I2CMaster_SetBusSpeed(i2cFd, I2C_BUS_SPEED_STANDARD);
	if (ret < 0) {
		Log_Debug("ERROR: I2CMaster_SetBusSpeed: errno=%d (%s)\r\n", errno, strerror(errno));
		close(i2cFd);
		return -1;
	}

	ret = I2CMaster_SetTimeout(i2cFd, 100);
	if (ret < 0) {
		Log_Debug("ERROR: I2CMaster_SetTimeout: errno=%d (%s)\r\n", errno, strerror(errno));
		close(i2cFd);
		return -1;
	}

	return 0;

#elif defined(AzureSphere_CM4)

	// removed

#endif
}

int ft6x06_ll_i2c_tx(uint8_t* tx_data, uint32_t tx_len)
{
#if defined(AzureSphere_CA7)

	int ret = I2CMaster_Write(i2cFd, FT6206_I2C_ADDR, tx_data, tx_len);
	if (ret < 0) {
		Log_Debug("ERROR: I2CMaster_Write: errno=%d (%s)\r\n", errno, strerror(errno));
		return -1;
	} else if (ret != tx_len) {
		Log_Debug("ERROR: I2CMaster_Write transfer %d bytes, expect %d bytes\r\n", ret, tx_len);
		return -1;
	}

	return 0;

#elif defined(AzureSphere_CM4)

	// removed

#endif
}

int ft6x06_ll_i2c_tx_then_rx(uint8_t* tx_data, uint32_t tx_len, uint8_t* rx_data, uint32_t rx_len)
{
#if defined(AzureSphere_CA7)

	int ret = I2CMaster_WriteThenRead(i2cFd, FT6206_I2C_ADDR, tx_data, tx_len, rx_data, rx_len);
	if (ret < 0) {
		Log_Debug("ERROR: I2CMaster_WriteThenRead: errno=%d (%s)\r\n", errno, strerror(errno));
		return -1;
	} else if (ret != (tx_len + rx_len)) {
		Log_Debug("ERROR: I2CMaster_WriteThenRead transfer %d bytes, expect %d bytes\r\n", ret, tx_len + rx_len);
		return -1;
	}

	return 0;

#elif defined(AzureSphere_CM4)

	// removed

#endif
}
