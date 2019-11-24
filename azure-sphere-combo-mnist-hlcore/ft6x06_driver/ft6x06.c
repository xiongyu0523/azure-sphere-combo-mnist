#include "ili9341.h"
#include "ft6x06_ll.h"
#include "ft6x06.h"

#if defined(AzureSphere_CA7)
#include <applibs/log.h>
#elif defined(AzureSphere_CM4)
#include "Log_Debug.h"
#endif

void ft6x06_init(void)
{
	ft6x06_ll_i2c_init();

	uint8_t buf[2];

	buf[0] = FT6206_DEV_MODE_REG;
	buf[1] = FT6206_DEV_MODE_WORKING;

	(void)ft6x06_ll_i2c_tx(&buf[0], 2);

	buf[0] = FT6206_CHIP_ID_REG;

	ft6x06_ll_i2c_tx_then_rx(&buf[0], 1, &buf[1], 1);
	if ((buf[1] != FT6206_ID_VALUE) && (buf[1] != FT6x36_ID_VALUE)) {
		Log_Debug("ERROR: Incorrect FT6X06 detected\r\n");
	}
}

uint8_t ft6x06_detect_touch(void)
{
	uint8_t buf[2];

	buf[0] = FT6206_TD_STAT_REG;
	ft6x06_ll_i2c_tx_then_rx(&buf[0], 1, &buf[1], 1);

	buf[1] &= FT6206_TD_STAT_MASK;

	// after reset, the device report 15
	if (buf[1] > FT6206_MAX_DETECTABLE_TOUCH) {
		buf[1] = 0;
	}

	return buf[1];
}

void ft6x06_get_xy(uint16_t *p_x, uint16_t *p_y)
{
	uint8_t reg = FT6206_P1_XH_REG;
	uint8_t buf[4];

	ft6x06_ll_i2c_tx_then_rx(&reg, 1, &buf[0], 4);

	*p_x = ((buf[0] & FT6206_MSB_MASK) << 8) | (buf[1] & FT6206_LSB_MASK);
	*p_y = ((buf[2] & FT6206_MSB_MASK) << 8) | (buf[3] & FT6206_LSB_MASK);

#if (ORITENTATION == LANDSCAPE)
	uint16_t tmpt = ILI9341_LCD_PIXEL_WIDTH - *p_y - 1;
	*p_y = *p_x;
	*p_x = tmpt;
#endif

	Log_Debug("%d, %d\r\n", *p_x, *p_y);
}
