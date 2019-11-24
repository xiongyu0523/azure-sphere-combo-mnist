#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <time.h>

#include <applibs/log.h>
#include <applibs/gpio.h>

#include "delay.h"
#include "ili9341.h"
#include "ft6x06.h"

int main(void)
{
	ili9341_init();
	ili9341_clean_screen(BLACK);
	ft6x06_init();

	uint16_t x, y;
	while (1) {
		delay_ms(20);

		if (ft6x06_detect_touch() > 0) {
			ft6x06_get_xy(&x, &y);

			ili9341_fillCircle(x, y, 4, WHITE);
		}
	}
}
