#include "display.h"
#include "config.h"
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

TFT_eSPI tft = TFT_eSPI();  // Non-static: accessible via extern from settings

// XPT2046 on separate SPI bus (touch pins differ from display pins)
static SPIClass touch_spi(VSPI);
static XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_WIDTH * LVGL_BUF_LINES];
static lv_color_t buf2[SCREEN_WIDTH * LVGL_BUF_LINES];
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;
static lv_disp_t* disp = nullptr;

static hw_timer_t* lvgl_tick_timer = nullptr;

static void IRAM_ATTR lvgl_tick_isr() {
    lv_tick_inc(LV_TICK_PERIOD_MS);
}

static void disp_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)color_p, w * h, true);
    tft.endWrite();
    lv_disp_flush_ready(drv);
}

static void touch_read(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    if (ts.touched()) {
        TS_Point p = ts.getPoint();
        // Map raw touch coords to screen coords (landscape rotation 1)
        data->state = LV_INDEV_STATE_PR;
        data->point.x = map(p.x, TOUCH_CAL_X_MIN, TOUCH_CAL_X_MAX, 0, SCREEN_WIDTH - 1);
        data->point.y = map(p.y, TOUCH_CAL_Y_MIN, TOUCH_CAL_Y_MAX, 0, SCREEN_HEIGHT - 1);

        // Clamp to screen bounds
        if (data->point.x < 0) data->point.x = 0;
        if (data->point.x >= SCREEN_WIDTH) data->point.x = SCREEN_WIDTH - 1;
        if (data->point.y < 0) data->point.y = 0;
        if (data->point.y >= SCREEN_HEIGHT) data->point.y = SCREEN_HEIGHT - 1;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
}

void display_init() {
    // Init display
    tft.init();
    tft.setRotation(1);  // Landscape 320x240
    tft.fillScreen(TFT_BLACK);

    // Init touch on separate SPI bus
    touch_spi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(touch_spi);
    ts.setRotation(1);  // Match display rotation

    // Init LVGL
    lv_init();

    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, SCREEN_WIDTH * LVGL_BUF_LINES);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res  = SCREEN_WIDTH;
    disp_drv.ver_res  = SCREEN_HEIGHT;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp = lv_disp_drv_register(&disp_drv);

    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touch_read;
    lv_indev_drv_register(&indev_drv);

    // Hardware timer for LVGL tick (timer 0, prescaler 80 = 1MHz)
    lvgl_tick_timer = timerBegin(0, 80, true);
    timerAttachInterrupt(lvgl_tick_timer, &lvgl_tick_isr, true);
    timerAlarmWrite(lvgl_tick_timer, LV_TICK_PERIOD_MS * 1000, true);
    timerAlarmEnable(lvgl_tick_timer);
}

lv_disp_t* display_get() {
    return disp;
}
