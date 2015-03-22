#include LANG_SPECIFIC

menu_t main_menu = {
    {NULL, prev, next, switch_cross,   NULL, NULL, switch_menu},
    {NULL, prev, next, enter_gauge,    NULL, NULL, switch_menu},
    //{NULL, prev, next, switch_polarity,NULL, NULL, switch_menu},
    {NULL, prev, next, enter_gauge,    NULL, NULL, switch_menu},
    {NULL, prev, next, enter_gauge,    NULL, NULL, switch_menu},
    {NULL, prev, next, set_move_cross, NULL, NULL, switch_menu},
    {NULL, prev, next, calibrate_enter,NULL, NULL, switch_menu},
    {NULL, prev, next, reset_confirm, NULL, NULL, switch_menu}
};

struct gauge_t gauges[] = {
    {NULL, NULL, NULL},
    {&brightness, update_brightness, finish_brightness},
    //{NULL, NULL, NULL},
    {&dbrightness, update_dbrightness, finish_dbrightness},
    {&dcontrast, update_dcontrast, finish_dcontrast},
    {NULL, NULL, NULL},
    {NULL, NULL, NULL},
    {NULL, NULL, NULL}
};

static void camera_contrast(int button) {
    calibration_button = (button == button_right) ? GPIO_Pin_4 : GPIO_Pin_7;
    calibration_button_request = 2;
}

static void switch_polarity(int button) {
    polarity = !polarity;
    set_polarity_request = true;
}

static void switch_zoom(int button) {
    if (current_zoom == 2) {
        current_zoom = 0;
    } else {
        current_zoom++;
    }
    set_zoom_request = true;
    reload_settings = true;
}

static void buttons(void)
{
    static uint8_t old_button = button_none;
    if (!ad_done) return;
    ad_done = false;
    buttons_level = adc_buffer[0];

    uint16_t temp = buttons_level / 4;
    temp += (STEPWIDTH/2);
    temp /= STEPWIDTH;
    debounced_code = codes[temp];

    if (debounced_code == 0) {
        hcount = HCOUNT_OFF;
        if (old_button == button_menu) {
            button = button_menu;
            old_button = button_none;
        }
    } else {
        if (hcount == HCOUNT_ON) {
            old_button = debounced_code;
            if (debounced_code != button_menu) {
                button = debounced_code;
            }
        }
        if (hcount >= HCOUNT_REPEAT) {
            if ((debounced_code != button_menu)) {
                if (menu > 0) {
                    hcount = HCOUNT_ON;
                    if (autorepeat) {
                        old_button = button = debounced_code;
                    }
                } else {
                    if ((debounced_code == button_down) && (hcount == (HCOUNT_LONG - 1))) {
                        switch_polarity(debounced_code);
                    }
                }
            } else {
                if (hcount == (HCOUNT_LONG - 1)) {
                    old_button = button = button_long;
                }
            }
        }
        if (hcount < HCOUNT_LONG) {
            hcount++;
        }
    }
}
