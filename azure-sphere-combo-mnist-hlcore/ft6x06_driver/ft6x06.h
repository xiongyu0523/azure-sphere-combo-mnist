#ifndef FT6X06_H
#define FT6X06_H

#include <stdint.h>
  
#define FT6206_MAX_DETECTABLE_TOUCH     2

#define FT6206_DEV_MODE_REG             0x00

#define FT6206_DEV_MODE_WORKING         0x00
#define FT6206_DEV_MODE_FACTORY         0x04

#define FT6206_DEV_MODE_MASK            0x7
#define FT6206_DEV_MODE_SHIFT           4

#define FT6206_TD_STAT_REG              0x02

#define FT6206_TD_STAT_MASK             0x0F
#define FT6206_TD_STAT_SHIFT            0x00

#define FT6206_TOUCH_EVT_FLAG_PRESS_DOWN 0x00
#define FT6206_TOUCH_EVT_FLAG_LIFT_UP    0x01
#define FT6206_TOUCH_EVT_FLAG_CONTACT    0x02
#define FT6206_TOUCH_EVT_FLAG_NO_EVENT   0x03

#define FT6206_TOUCH_EVT_FLAG_SHIFT     6
#define FT6206_TOUCH_EVT_FLAG_MASK      (3 << FT6206_TOUCH_EVT_FLAG_SHIFT)

#define FT6206_MSB_MASK                 0x0F
#define FT6206_MSB_SHIFT                0

#define FT6206_LSB_MASK                 0xFF
#define FT6206_LSB_SHIFT                0

#define FT6206_P1_XH_REG                0x03
#define FT6206_P1_XL_REG                0x04
#define FT6206_P1_YH_REG                0x05
#define FT6206_P1_YL_REG                0x06

#define FT6206_P1_WEIGHT_REG            0x07

#define FT6206_TOUCH_WEIGHT_MASK        0xFF
#define FT6206_TOUCH_WEIGHT_SHIFT       0

#define FT6206_P1_MISC_REG              0x08

#define FT6206_TOUCH_AREA_MASK          (0x04 << 4)
#define FT6206_TOUCH_AREA_SHIFT         0x04

#define FT6206_CHIP_ID_REG              0xA8

#define FT6206_ID_VALUE                 0x11
#define FT6x36_ID_VALUE                 0xCD

void ft6x06_init(void);
uint8_t ft6x06_detect_touch(void);
void ft6x06_get_xy(uint16_t* p_x, uint16_t* p_y);

#endif

