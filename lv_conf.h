#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

/* Color depth: 16-bit RGB565 matches ILI9341 */
#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP    0   /* pushColors(..., true) handles byte swap */

/* Memory */
#define LV_MEM_CUSTOM       0
#define LV_MEM_SIZE         (40U * 1024U)

/* Display refresh */
#define LV_DISP_DEF_REFR_PERIOD  33  /* ~30fps */
#define LV_INDEV_DEF_READ_PERIOD  30

/* Logging (disable in production) */
#define LV_USE_LOG           0

/* Fonts */
#define LV_FONT_MONTSERRAT_12  1
#define LV_FONT_MONTSERRAT_14  1
#define LV_FONT_MONTSERRAT_16  1
#define LV_FONT_MONTSERRAT_20  1
#define LV_FONT_MONTSERRAT_28  1
#define LV_FONT_DEFAULT        &lv_font_montserrat_14

/* Themes */
#define LV_USE_THEME_DEFAULT   1
#define LV_THEME_DEFAULT_DARK  1

/* Features */
#define LV_USE_ANIM            1
#define LV_USE_GROUP           0
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_NXP_PXP     0
#define LV_USE_GPU_NXP_VG_LITE 0

/* Widgets */
#define LV_USE_ARC        1
#define LV_USE_BAR        1
#define LV_USE_BTN        1
#define LV_USE_BTNMATRIX  1
#define LV_USE_CANVAS     0
#define LV_USE_CHECKBOX   0
#define LV_USE_DROPDOWN   0
#define LV_USE_IMG        1
#define LV_USE_LABEL      1
#define LV_USE_LINE       1
#define LV_USE_ROLLER     0
#define LV_USE_SLIDER     1
#define LV_USE_SWITCH     1
#define LV_USE_TABLE      1
#define LV_USE_TEXTAREA   1

/* Extra widgets */
#define LV_USE_ANIMIMG    0
#define LV_USE_CALENDAR   0
#define LV_USE_CHART      0
#define LV_USE_COLORWHEEL 0
#define LV_USE_IMGBTN     0
#define LV_USE_KEYBOARD   1
#define LV_USE_LED        1
#define LV_USE_LIST       1
#define LV_USE_MENU       0
#define LV_USE_METER      0
#define LV_USE_MSGBOX     1
#define LV_USE_SPAN       0
#define LV_USE_SPINBOX    0
#define LV_USE_SPINNER    1
#define LV_USE_TABVIEW    1
#define LV_USE_TILEVIEW   0
#define LV_USE_WIN        0

#endif /* LV_CONF_H */
