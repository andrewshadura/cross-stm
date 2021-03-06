#include "stm32f0xx_adc.h"
#include "stm32f0xx_dac.h"
#include "stm32f0xx_dma.h"
#include "stm32f0xx_exti.h"
#include "stm32f0xx_flash.h"
#include "stm32f0xx_gpio.h"
#include "stm32f0xx_misc.h"
#include "stm32f0xx_syscfg.h"
#include "stm32f0xx_pwr.h"
#include "stm32f0xx_rcc.h"
#include "stm32f0xx_spi.h"
#include "stm32f0xx_tim.h"
#include "stm32f0xx_usart.h"
#include "flash_async.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "statusbar.h"
#include "cross-big.h"
#include "cross-small.h"
#include "cross-tdot.h"
#include "cross-tdot-mini.h"
#include "cross-milt.h"

#include "compass.h"

#include "compass-setup-x-en.h"
#include "compass-setup-y-en.h"
#include "compass-setup-z-en.h"
#include "compass-setup-x-ru.h"
#include "compass-setup-y-ru.h"
#include "compass-setup-z-ru.h"

/* statusbar_width is the width of the bitmap */
/* STATUSBAR_WIDTH is the width of the actual statusbar */
#define STATUSBAR_WIDTH (256)

#define LEFT_OFFSET -50

struct coords_t {
    uint16_t x;
    uint16_t y;
};

#define MAX_ZOOMS 4

struct setting_t {
    struct coords_t coords[MAX_ZOOMS];
    uint8_t brightness;
    uint8_t cross_type;
};

volatile uint16_t adc_buffer[3] = {4095, 4095, 4095};

#define MAX_USERS 8

struct settings_t {
    /*
        for each user:
            for each zoom: coords
            brightness
            cross type
     */
    struct setting_t users[MAX_USERS];
    uint32_t seqno;
    uint32_t invalid;
    uint8_t brightness;
    uint8_t contrast;
    uint8_t palette;
    uint8_t language;
    int16_t cross_x_off;
    int16_t cross_y_off;
};

struct settings_t __attribute__((section (".flash.settings0"))) settings0;/* = {
    .invalid = 0xffffffff
};*/
struct settings_t __attribute__((section (".flash.settings1"))) settings1;/* = {
    .invalid = 0xffffffff
};*/

struct settings_t settings;

extern void Delay(volatile int i) {
    for (; i != 0; i--);
}

#define MAX_ROW 400

uint16_t row = 0;

uint16_t adccal = 0;

#define CHARGEN_PROGRESS 8
#define CHARGEN_NUMBERS 11
#define CHARGEN_PLUS    (10 + CHARGEN_NUMBERS)
#define CHARGEN_MINUS   (11 + CHARGEN_NUMBERS)
#define CHARGEN_X       (12 + CHARGEN_NUMBERS)
#define CHARGEN_Y       (13 + CHARGEN_NUMBERS)

unsigned char statusbar_ram_bits[STATUSBAR_WIDTH / 8];

unsigned char compass_bits[9 + 2];
static signed char compass_shifts[9 + 2];

unsigned char gauge_ram_bits[12];
char gauge_select = 0;

#define STATUSBAR_START (35+9)
#define MAINWIN_START (100+9)
#define GAUGE_TOP_START (70)
#define GAUGE_BOTTOM_START (249)
#define CROSS_CENTRE cross_y

uint16_t gauge_start = 0;

#define CROSS_Y_DEFAULT (162+9 + cross_y_off)
#define CROSS_X_DEFAULT (LEFT_OFFSET + 175 + 25 + cross_x_off)

uint16_t cross_x_default = 0;
uint16_t cross_y_default = 0;

#define CROSS_X_RANGE 79
#define CROSS_Y_RANGE 79

int16_t cross_y_off = 0;
int16_t cross_x_off = 0;

uint16_t cross_y = 0; /* +/- 85 */
uint16_t cross_x = 0;

uint16_t start_inv = MAINWIN_START + 3, end_inv = MAINWIN_START + 14;
uint16_t real_start_inv = MAINWIN_START + 3, real_end_inv = MAINWIN_START + 14;

int battery_low = 0;

#define BATTERY_LOW_LEVEL 78

volatile uint16_t battery_level = 0;

#if 0
static void set_out(char i) {
    if (i) {
        GPIOA->ODR &= ~GPIO_Pin_7;
    } else {
        GPIOA->ODR |= GPIO_Pin_7;
    }
}
#endif

enum buttons {
    button_none = 0,
    button_up,
    button_down,
    button_menu,
    button_left,
    button_right,
    button_long,

    button_count,
    button_max = button_count - 1
};

enum cross_type {
    cross_type_min = 0,
    cross_type_big = cross_type_min,
    cross_type_small,
    cross_type_tdot,
    cross_type_tdot_mini,
    cross_type_milt,

    cross_type_count,
    cross_type_max = cross_type_count - 1
};

const unsigned char cross_height[] = {
    [cross_type_big] = cross_big_height,
    [cross_type_small] = cross_small_height,
    [cross_type_tdot] = cross_tdot_height,
    [cross_type_tdot_mini] = cross_tdot_mini_height,
    [cross_type_milt] = cross_milt_height
};

int button = button_none;

int cross_type = cross_type_big;

enum language_t {
    LANG_EN = 0,
    LANG_RU = 1
};

bool batteryblink = true;
volatile bool send = false;

bool show_cross = true;
bool show_gauge = false;
bool show_menu = false;
bool show_coords = false;
char show_saving = 0;

uint8_t compass_setup_state = 0;

#define SAVING_DELAY 3

bool autorepeat = false;

bool reload_settings = true;
volatile bool save_settings_request = false;
volatile bool force_save_settings = false;
volatile bool set_dbrightness_request = false;
volatile bool set_dcontrast_request = false;
volatile bool set_polarity_request = false;
volatile bool set_zoom_request = false;
volatile bool set_palette_request = false;
volatile uint8_t calibration_request = 0;
volatile uint8_t calibration_button_request = 0;
volatile uint16_t calibration_button = 0;
#define CALIBRATE 17

#define CALIBRATION_BUTTONS_PORT GPIOB
#define CBUTGPIO CALIBRATION_BUTTONS_PORT

#define CALIBRATION_BUTTON_LEFT  GPIO_Pin_4
#define CALIBRATION_BUTTON_RIGHT GPIO_Pin_8
#define CALIBRATION_BUTTON_UP    GPIO_Pin_5
#define CALIBRATION_BUTTON_DOWN  GPIO_Pin_3
#define CALIBRATION_BUTTONS_ALL  (CALIBRATION_BUTTON_LEFT | CALIBRATION_BUTTON_RIGHT | CALIBRATION_BUTTON_UP | CALIBRATION_BUTTON_DOWN)

uint8_t initial_camera_delay = 50;
bool configuring_camera = false;
bool configuring_oled = false;

int gauge_value = 0;

int menu = 0;

bool inverted = false;

bool once = false;

uint8_t frameno = 0;

#define HCOUNT_OFF   -2
#define HCOUNT_ON     0
#define HCOUNT_REPEAT 25
#define HCOUNT_LONG   35

typedef void (*fn_t)(void);
typedef void (*fn1_t)(int button);

typedef const fn1_t menuitem_t[button_count];
typedef const menuitem_t menu_t[];

int current_item = -1;
int current_confirm = 0;

//#define MENU_LENGTH ((menu_height / 16))
#define MENU_LENGTH ((sizeof(main_menu) / sizeof(menuitem_t)))

int current_menu_height;
int current_menu_length;

int current_input = 1;

int current_zoom = 0;

bool polarity = false;

#define MAX_GAUGE_VALUE 24
#define MAX_BRIGHTNESS (MAX_GAUGE_VALUE - 1)

uint8_t brightness = MAX_BRIGHTNESS;
uint8_t dbrightness;
uint8_t dcontrast;

uint8_t calibrate_mode = 0;

static void load_settings(void);
static void init_settings(void);
static void save_settings(void);
static void update_brightness(int value);
static void update_dbrightness(int value);
static void update_dcontrast(int value);

/*
  |15|14|13|12|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|

A =       7  6  5        4                          = 0x3900
B =                                        3  2  1  = 0x0007

A + 0x0300:          [1][1]
C =       7  6  5  4  X  X

Ch >> 7:
                               7  6  5  4  X  X
B:
                                           3  2  1

                               7  6  5  4  3  2  1
                               6  5  4  3  2  1  0

*/

#define A_MASK (GPIO_Pin_13 | GPIO_Pin_12 | GPIO_Pin_11 | GPIO_Pin_8)
#define B_MASK (GPIO_Pin_2 | GPIO_Pin_1 | GPIO_Pin_0)
#define C_MASK (GPIO_Pin_13 | GPIO_Pin_12 | GPIO_Pin_11 | GPIO_Pin_10)

static inline char merge_inputs(uint16_t a, uint16_t b) {
    a = ((a & A_MASK) + 0x0300) & C_MASK;
    b = b & B_MASK;

    b = b | (a >> 7);
    return b & 0x7f;
}

const char input_map[128] = {
#define LT(n) n, n, n, n, n, n, n, n, n, n, n, n, n, n, n, n
    LT(6), LT(6), LT(6), LT(6), LT(5), LT(5), LT(4),
    3, 3, 3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 1, 1, 0, 7
};


uint8_t Tx1Buffer[16] = {0xf0, 0x03, 0x26, 0x01, 0x00, 0x27, 0xff};
volatile uint8_t Tx1Count = 0;
uint8_t Tx2Buffer[16] = {0x02, 0x21, 0x03, 0x19, 0x00, 0x03};
volatile uint8_t Tx2Count = 0;

bool boot = true;

uint8_t compass_retries = 0;
bool live_compass = true;
bool compass_off_request = false;
int16_t azimuth = 0;

void update_status(void) {
    int i;
    uint16_t a = GPIOA->IDR;
    uint16_t b = GPIOB->IDR;
    uint8_t inputs = merge_inputs(a, b);
    uint8_t input = input_map[inputs];
    if (input != current_input) {
        current_input = input;
        reload_settings = true;
    }

    statusbar_ram_bits[0] = CHARGEN_NUMBERS + current_input + 1;
    for (i = 1; i <= 24; i++) {
        statusbar_ram_bits[i] = 0;
    }

    switch (current_zoom) {
        case 1:
            statusbar_ram_bits[14] = 4;
            statusbar_ram_bits[15] = 5;
            break;
        case 2:
            statusbar_ram_bits[14] = 6;
            statusbar_ram_bits[15] = 7;
            break;
        case 3:
            statusbar_ram_bits[14] = CHARGEN_NUMBERS + 8;
            statusbar_ram_bits[15] = 7;
            break;
        default:
            statusbar_ram_bits[14] = 0;
            statusbar_ram_bits[15] = 0;
            break;
    }

    uint16_t azimuth_binary = ((uint16_t)(azimuth + 180)) * 128 / 360;
    static uint16_t azimuth_binary_displayed = 0;

    if (azimuth_binary != azimuth_binary_displayed) {
        int16_t d = azimuth_binary - azimuth_binary_displayed;
        if (d < -96) {
            d = 128 + d;
        } else if (d > 96) {
            d = -(128 - d);
        }
        d /= 2;
        if (d != 0) {
            azimuth_binary_displayed += d;
        } else {
            azimuth_binary_displayed = azimuth_binary;
        }
    }

    if (compass_retries && live_compass) {
        #define CHARGEN_COMPASS 33
        #define COMPASS_NEXT 8
        compass_bits[0] = 0;
        compass_shifts[0] = 0;
        compass_bits[1] = CHARGEN_COMPASS + ((0 + (azimuth_binary_displayed / 8)) % 16);
        compass_shifts[1] = azimuth_binary_displayed % 8;
        compass_bits[2] = CHARGEN_COMPASS + ((1 + (azimuth_binary_displayed / 8)) % 16);
        compass_shifts[2] = azimuth_binary_displayed % 8;
        compass_bits[3] = CHARGEN_COMPASS + ((2 + (azimuth_binary_displayed / 8)) % 16);
        compass_shifts[3] = azimuth_binary_displayed % 8;
        compass_bits[4] = CHARGEN_COMPASS + ((3 + (azimuth_binary_displayed / 8)) % 16);
        compass_shifts[4] = azimuth_binary_displayed % 8;
        compass_bits[5] = CHARGEN_COMPASS + ((4 + (azimuth_binary_displayed / 8)) % 16);
        compass_shifts[5] = 0xff;
    } else {
        compass_shifts[1] = 0xff;
        compass_shifts[2] = 0xff;
        compass_shifts[3] = 0xff;
        compass_shifts[4] = 0xff;
    }

    for (i = 25; i < (STATUSBAR_WIDTH / 8); i++) {
        statusbar_ram_bits[i] = i;
    }

    if (reload_settings) {
        cross_type = settings.users[current_input].cross_type;
        brightness = settings.users[current_input].brightness;
        update_brightness(brightness);
        cross_x = settings.users[current_input].coords[current_zoom].x;
        cross_y = settings.users[current_input].coords[current_zoom].y;
        cross_x_off = settings.cross_x_off;
        cross_y_off = settings.cross_y_off;
        cross_x_default = CROSS_X_DEFAULT;
        cross_y_default = CROSS_Y_DEFAULT;

        reload_settings = false;
    }

    static uint16_t count = 0;
    if (++count == 2) {
        count = 0;
        read_compass_request = true;
        if (compass_retries) {
            compass_retries--;
        }
    }

    #if 1
    if (live_compass && (battery_low < 80) && (menu < 2)) {
    uint16_t qw;
    if (azimuth < 0) {
        qw = 360 + azimuth;
    } else {
        qw = azimuth;
    }
    statusbar_ram_bits[31] = CHARGEN_NUMBERS + qw % 10;
    qw /= 10;
    statusbar_ram_bits[30] = CHARGEN_NUMBERS + qw % 10;
    qw /= 10;
    statusbar_ram_bits[29] = CHARGEN_NUMBERS + qw % 10;
    } else {
    statusbar_ram_bits[31] = 0;
    statusbar_ram_bits[30] = 0;
    statusbar_ram_bits[29] = 0;
    }
    #endif
}

const char f100[] = {1, CHARGEN_NUMBERS + 1, CHARGEN_NUMBERS + 0, CHARGEN_NUMBERS + 0, 2, 1, CHARGEN_NUMBERS + 5, CHARGEN_NUMBERS + 2};
const char f75[] = {1, CHARGEN_NUMBERS + 7, CHARGEN_NUMBERS + 5, 2, 1, CHARGEN_NUMBERS + 4, CHARGEN_NUMBERS + 0};

struct gauge_t {
    uint8_t * var;
    fn1_t update_fn;
    fn1_t finish_fn;
};

static void next(int button);
static void prev(int button);
static void next_confirm(int button);
static void select_confirm(int button);
static void camera_contrast(int button);
static void next_cross(int button);
static void prev_cross(int button);
static void switch_cross(int button);
static void switch_inversion(int button);
static void switch_menu(int button);
static void switch_move_menu(int button);
static void switch_zoom(int button);
static void enter_gauge(int button);
static void change_gauge(int button);
static void finish_gauge(int button);
static void finish_brightness(int value);
static void finish_dbrightness(int value);
static void finish_dcontrast(int value);
static void set_move_cross(int button);
static void cross_xy(int button);
static void finish_move(int button);
static void switch_polarity(int button);
static void switch_palette(int button);
static void calibrate_xy(int button);
static void calibrate_set(int button);
static void calibrate_menu(int button);
static void calibrate_enter(int button);
static void reset_confirm(int button);
static void reset_settings(int button);
static void switch_lang(int button);
static void default_set(int button);
static void compass_setup(int button);
static void compass_setup_confirm(int button);

#define STEPS 5
#define STEPWIDTH (1024/STEPS)

uint8_t codes[] = {button_menu, button_left, button_right, button_down, button_up, 0};
uint8_t debounced_code = 0;
volatile int8_t hcount = 0;
volatile bool ad_done = false;
volatile uint16_t buttons_level;

#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
#define CONFIGISE_(x) STRINGIFY(x##_specific.c)
#define CONFIGISE(x) CONFIGISE_(x)
#define LANGISE_(config,lang) STRINGIFY(menu-config-lang-w.h)
#define LANGISE(...) LANGISE_(__VA_ARGS__)

#define CONFIG_SPECIFIC CONFIGISE(CONFIG)
#define LANG_SPECIFIC LANGISE(CONFIG,LANG)

#include CONFIG_SPECIFIC

int menu_width = menu_en_width;
int menu_height = menu_en_height;

const unsigned char * menu_widths = menu_en_widths;

const char (* menu_bits)[menu_en_width / 8] = menu_en_bits;

int helper_width = helper_en_width;
int helper_height = helper_en_height;

const char (* helper_bits)[helper_en_width / 8] = helper_en_bits;
const char (* compass_setup_x_bits)[menu_en_width / 8] = compass_setup_x_en_bits;
const char (* compass_setup_y_bits)[menu_en_width / 8] = compass_setup_y_en_bits;
const char (* compass_setup_z_bits)[menu_en_width / 8] = compass_setup_z_en_bits;

int saving_min = SAVING_EN_MIN;
int saving_max = SAVING_EN_MAX;

void set_language(void) {
    switch (settings.language) {
        case LANG_EN: {
            menu_bits = menu_en_bits;
            helper_bits= helper_en_bits;
            menu_widths = menu_en_widths;
            menu_width = menu_en_width;
            menu_height = menu_en_height;
            helper_width = helper_en_width;
            helper_height = helper_en_height;
            saving_min = SAVING_EN_MIN;
            saving_max = SAVING_EN_MAX;
            compass_setup_x_bits = compass_setup_x_en_bits;
            compass_setup_y_bits = compass_setup_y_en_bits;
            compass_setup_z_bits = compass_setup_z_en_bits;
        } break;
        case LANG_RU: {
            menu_bits = menu_ru_bits;
            helper_bits= helper_ru_bits;
            menu_widths = menu_ru_widths;
            menu_width = menu_ru_width;
            menu_height = menu_ru_height;
            helper_width = helper_ru_width;
            helper_height = helper_ru_height;
            saving_min = SAVING_RU_MIN;
            saving_max = SAVING_RU_MAX;
            compass_setup_x_bits = compass_setup_x_ru_bits;
            compass_setup_y_bits = compass_setup_y_ru_bits;
            compass_setup_z_bits = compass_setup_z_ru_bits;
        } break;
    }
}

static void switch_lang(int button) {
    settings.language = !settings.language;
    set_language();
    save_settings_request = true;
    force_save_settings = true;
}

menu_t off_menu = {
    {NULL, switch_zoom, switch_inversion, NULL, camera_contrast, camera_contrast, switch_menu}
};

menu_t default_xy_menu = {
    {NULL, cross_xy, cross_xy, default_set, cross_xy, cross_xy, NULL}
};

menu_t switch_cross_menu = {
    {NULL, NULL, NULL, switch_cross, prev_cross, next_cross, switch_menu}
};

menu_t gauge_menu = {
    {NULL, NULL, NULL, finish_gauge, change_gauge, change_gauge, switch_menu}
};

menu_t move_cross_menu = {
    {NULL, cross_xy, cross_xy, finish_move, cross_xy, cross_xy, switch_move_menu}
};

menu_t calibrate_cross_menu = {
    {NULL, calibrate_xy, calibrate_xy, calibrate_set, calibrate_xy, calibrate_xy, NULL}
};

menu_t compass_setup_menu = {
    {NULL, NULL, NULL, compass_setup_confirm, NULL, NULL, NULL}
};

menu_t confirm_menu = {
    {NULL, next_confirm, next_confirm, select_confirm, NULL, NULL, NULL},
};

menu_t * current_menu = &default_xy_menu;

static void update_gauge(void) {
    int i;

    if (show_saving) {
        int j = saving_min;

        gauge_ram_bits[0] = 0;
        for (i = 1; i < sizeof(gauge_ram_bits); i++) {
            if (j > saving_max) {
                gauge_ram_bits[i] = 0;
            } else {
                gauge_ram_bits[i] = j++;
            }
        }
        return;
    }

    if (current_item == 0) {
        for (i = 0; i < sizeof(gauge_ram_bits); i++) {
            gauge_ram_bits[i] = 0;
            if (cross_type == cross_type_big) {
                if (i < sizeof(f100)) {
                    gauge_ram_bits[i] = f100[i];
                }
            } else if (cross_type == cross_type_small) {
                if (i < sizeof(f75)) {
                    gauge_ram_bits[i] = f75[i];
                }
            }
        }
        return;
    }

    int last_pos = (gauge_value + 1) / 2;
    int last_part = (gauge_value + 1) % 2;

    for (i = 0; i < sizeof(gauge_ram_bits); i++) {
        if (i < last_pos) {
            gauge_ram_bits[i] = CHARGEN_PROGRESS + 2;
        } else if (i == last_pos) {
            gauge_ram_bits[i] = CHARGEN_PROGRESS + last_part;
        } else {
            gauge_ram_bits[i] = CHARGEN_PROGRESS;
        }
    }
}

static void next(int button) {
    current_item = (current_item + 1) % current_menu_length;
    start_inv = MAINWIN_START + 3 + current_item * 16;
    end_inv = MAINWIN_START + 14 + current_item * 16;
}

static void prev(int button) {
    if (current_item == 0) {
        current_item = current_menu_length - 1;
    } else {
        current_item--;
    }
    start_inv = MAINWIN_START + 3 + current_item * 16;
    end_inv = MAINWIN_START + 14 + current_item * 16;
}

static void next_confirm(int button) {
    current_confirm = !current_confirm;
    start_inv = MAINWIN_START + 3 + current_confirm * 16;
    end_inv = MAINWIN_START + 14 + current_confirm * 16;
}

static void select_confirm(int button) {
    if (current_confirm == 0) {
        /* yes */
        reset_settings(button);
    }
    menu--;
    show_menu = false;
    current_menu_height = menu_height;
    current_menu = &main_menu;
    real_start_inv = start_inv = MAINWIN_START + 3 + current_item * 16;
    real_end_inv = end_inv = MAINWIN_START + 14 + current_item * 16;
}

void send1_packet(void);
void send2_packet(void);

static void switch_move_menu(int button) {
    finish_move(button);
    compass_off_request = false;
    live_compass = true;
    show_cross = false;
    show_coords = false;
    menu--;
    switch_menu(button);
}

static void switch_menu(int button) {
    if (menu) {
        if (menu == 2) {
            fn1_t callback = (*current_menu)[0][button_menu];
            if (callback) {
                callback(button_menu);
            }
        }
        menu = 0;
        cross_type = settings.users[current_input].cross_type;
        cross_x = settings.users[current_input].coords[current_zoom].x;
        cross_y = settings.users[current_input].coords[current_zoom].y;
        show_cross = true;
        show_gauge = false;
        show_coords = false;
    } else {
        menu = 1;
        //send_packet();
        show_cross = false;
        show_gauge = false;
        show_coords = false;
        start_inv = MAINWIN_START + 3 + current_item * 16;
        end_inv = MAINWIN_START + 14 + current_item * 16;
        current_menu_height = menu_height;
        current_menu_length = MENU_LENGTH;
    }
}

static void switch_inversion(int button) {
    inverted = !inverted;
    update_brightness(brightness);
}

static void next_cross(int button) {
    if (cross_type == cross_type_max) {
        cross_type = cross_type_min;
    } else {
        cross_type++;
    }
}

static void prev_cross(int button) {
    if (cross_type == cross_type_min) {
        cross_type = cross_type_max;
    } else {
        cross_type--;
    }
}

static void switch_cross(int button) {
    current_menu = &switch_cross_menu;
    switch (menu) {
        case 1:
            menu++;
            show_saving = 0;
            show_cross = true;
            show_gauge = true;
            break;
        case 2:
            menu--;
            settings.users[current_input].cross_type = cross_type;
            save_settings_request = true;
            show_saving = SAVING_DELAY;
            show_cross = false;
            show_gauge = false;
            break;
    }
}

static void enter_gauge(int button) {
    menu++;
    show_saving = 0;
    show_gauge = true;
    cross_x = CROSS_X_DEFAULT;
    cross_y = CROSS_Y_DEFAULT;
    show_cross = 1;
    gauge_value = *(gauges[current_item].var);
    current_menu = &gauge_menu;
    gauges[current_item].update_fn(gauge_value);
}

static void update_brightness(int value) {
    if (inverted) {
        DAC_SetChannel1Data(DAC_Align_12b_R, 0x3ff - 128 - value * 16 - 128);
    } else {
        DAC_SetChannel1Data(DAC_Align_12b_R, 0x3ff + value * 16 - 128);
    }
}

static void update_dbrightness(int value) {
    dbrightness = value;
    set_dbrightness_request = true;
}

static void update_dcontrast(int value) {
    dcontrast = value;
    set_dcontrast_request = true;
}

static void change_gauge(int button) {
    if ((button == button_left) || (button == button_down)) {
        /*
        switch (gauge_value) {
            case 23: gauge_value = 12; break;
            case 12: gauge_value = 0; break;
            case 0: gauge_value = 0; break;
        }
        */
        gauge_value = (gauge_value == 0) ? 0 : gauge_value - 1;
    }
    if ((button == button_right) || (button == button_up)) {
        /*
        switch (gauge_value) {
            case 0: gauge_value = 12; break;
            case 12: gauge_value = 23; break;
            case 23: gauge_value = 23; break;
        }
        */
        gauge_value = (gauge_value >= 23) ? 23 : gauge_value + 1;
    }
    gauges[current_item].update_fn(gauge_value);
}

static void finish_dbrightness(int value) {
    dbrightness = value;
    settings.brightness = value;
}

static void finish_dcontrast(int value) {
    dcontrast = value;
    settings.contrast = value;
}

static void finish_brightness(int value) {
    settings.users[current_input].brightness = brightness;
}

static void finish_gauge(int button) {
    menu--;
    show_gauge = false;
    show_cross = 0;
    cross_x = settings.users[current_input].coords[current_zoom].x;
    cross_y = settings.users[current_input].coords[current_zoom].y;
    *(gauges[current_item].var) = gauge_value;
    gauges[current_item].finish_fn(gauge_value);
    save_settings_request = true;
    show_saving = SAVING_DELAY;
}

static void set_move_cross(int button) {
    menu++;
    current_menu = &move_cross_menu;
    show_coords = true;
    show_cross = true;
    compass_off_request = true;
}

static void cross_xy(int button) {
    switch (button) {
        case button_left:
            if (cross_x > (cross_x_default - CROSS_X_RANGE)) {
                cross_x--;
            }
            break;
        case button_right:
            if (cross_x < (cross_x_default + CROSS_X_RANGE)) {
                cross_x++;
            }
            break;
        case button_up:
            if (cross_y > (cross_y_default - CROSS_Y_RANGE)) {
                cross_y--;
            }
            break;
        case button_down:
            if (cross_y < (cross_y_default + CROSS_Y_RANGE)) {
                cross_y++;
            }
            break;
    }
}

static void default_set(int button) {
    menu = 0;
    show_cross = true;
    show_coords = false;
    current_item = 0;
    settings.cross_x_off = cross_x_off = (int16_t)cross_x - CROSS_X_DEFAULT;
    settings.cross_y_off = cross_y_off = (int16_t)cross_y - CROSS_Y_DEFAULT;
    cross_x_default = CROSS_X_DEFAULT;
    cross_y_default = CROSS_Y_DEFAULT;
    init_settings();
    save_settings_request = true;
    force_save_settings = true;
    reload_settings = true;
}

static uint16_t clamp(uint16_t centre, int16_t delta, int16_t max) {
    if (delta > max) {
        delta = max;
    }
    if ((-delta) > max) {
        delta = -max;
    }
    return centre + delta;
}

static void finish_move(int button) {
    settings.users[current_input].coords[current_zoom].x = cross_x;
    settings.users[current_input].coords[current_zoom].y = cross_y;
    if (current_zoom == 0) {
        int16_t delta_x = settings.users[current_input].coords[0].x - cross_x_default;
        int16_t delta_y = settings.users[current_input].coords[0].y - cross_y_default;
        settings.users[current_input].coords[1].x = clamp(cross_x_default, delta_x * 2, CROSS_X_RANGE);
        settings.users[current_input].coords[1].y = clamp(cross_y_default, delta_y * 2, CROSS_Y_RANGE);
        settings.users[current_input].coords[2].x = clamp(cross_x_default, delta_x * 4, CROSS_X_RANGE);
        settings.users[current_input].coords[2].y = clamp(cross_y_default, delta_y * 4, CROSS_Y_RANGE);
        settings.users[current_input].coords[3].x = clamp(cross_x_default, delta_x * 8, CROSS_X_RANGE);
        settings.users[current_input].coords[3].y = clamp(cross_y_default, delta_y * 8, CROSS_Y_RANGE);
    }
    save_settings_request = true;
    force_save_settings = true;
    show_saving = SAVING_DELAY;
    show_gauge = false;
}

static void calibrate_xy(int button) {
    switch (button) {
        case button_left:
            calibration_button = CALIBRATION_BUTTON_LEFT;
            break;
        case button_right:
            calibration_button = CALIBRATION_BUTTON_RIGHT;
            if (calibrate_mode == 2) {
                calibrate_mode = 0;
            }
            if (calibrate_mode == 3) {
                menu = 1;
                calibrate_mode = 0;
            }
            break;
        case button_up:
            calibration_button = CALIBRATION_BUTTON_UP;
            break;
        case button_down:
            calibrate_mode = (calibrate_mode + 1) & 3;
            calibration_button = CALIBRATION_BUTTON_DOWN;
            break;
    }
    calibration_button_request = 2;
}

static void calibrate_set(int button) {
}

static void calibrate_menu(int button) {
    menu = 0;
    show_cross = true;
    show_gauge = false;
}

static void calibrate_enter(int button) {
    menu = 2;
    show_cross = false;
    show_gauge = false;
    calibration_request = CALIBRATE;
    current_menu = &calibrate_cross_menu;
    calibrate_mode = 0;
}

static void compass_setup(int button) {
    menu = 2;
    show_cross = false;
    compass_off_request = true;
    show_menu = true;
    current_menu = &compass_setup_menu;
    current_menu_height = menu_height = compass_setup_x_en_height;
    compass_setup_state = 1;
    real_start_inv = start_inv = 0;
    real_end_inv = end_inv = 0;
}

static void compass_setup_confirm(int button) {
    if ((compass_setup_state >= 2) && (compass_setup_state < 5)) {
        compass_setup_state++;
    }
}

volatile uint16_t DACVal = 0x057f;

void ADC1_COMP_IRQHandler(void)
{
    if(ADC_GetITStatus(ADC1,ADC_IT_EOSEQ) == SET){
        ADC_ClearITPendingBit(ADC1,ADC_IT_EOSEQ);
        ad_done = true;
    }
}


void control(void) {
    if (boot) {
        if (ad_done) {
            buttons();
            boot = false;
            if (button == button_menu) {
                menu = 2;
                show_coords = true;
                show_cross = true;
                cross_x_off = 0;
                cross_y_off = 0;
                cross_x_default = CROSS_X_DEFAULT;
                cross_y_default = CROSS_Y_DEFAULT;
                button = button_none;
            } else {
                current_item = 0;
            }
        }
    } else {
                if (button != button_none) {
                    /*
                    if ((*current_menu)[current_item][button]) {
                        (*current_menu)[current_item][button]();
                    }
                    */
                    fn1_t callback;
                    switch (menu) {
                        case 0:
                            callback = off_menu[0][button];
                            break;
                        case 1:
                            callback = main_menu[current_item][button];
                            break;
                        case 2:
                            callback = (*current_menu)[0][button];
                            break;
                        default:
                            callback = NULL;
                    }
                    if (callback) {
                        callback(button);
                    }
                    if (menu == 1) {
                        update_brightness(MAX_BRIGHTNESS);
                    }
                    button = button_none;
                }

                autorepeat = menu == 2;
                buttons();
    }
}

uint8_t pulses = 0;

void HSYNC(void);

uint16_t pulse_width;

bool found = false;

void draw_nothing(void);
void draw_status(void);
void draw_menu(void);
void draw_confirm(void);
void draw_cross(void);
void draw_gauge(void);

fn_t current_fn = draw_nothing;

enum state_t {
    state_status = 0,
    state_main,
    state_bottom,
    state_skip
};

enum state_t state = state_status;


void EXTI2_3_IRQHandler(void)
{
    if(EXTI_GetITStatus(EXTI_Line2) != RESET)
    {
        pulse_width = TIM6->CNT;
        TIM6->CNT = 0;
        if ((pulse_width < 2500) && (row > 255)) {
            pulses++;
            if (pulses == 3) {
                found = true;
                current_fn = draw_nothing;
                state = state_status;
                ADC_StartOfConversion(ADC1);
            }
        } else {
            if (found) {
                row = 3;
                found = false;
                once = true;
                frameno = 0xf & (frameno + 1);
            }
            pulses = 0;
            HSYNC();
        }

        /* Clear the EXTI line 2 pending bit */
        EXTI_ClearITPendingBit(EXTI_Line2);
    }
}

void draw_nothing(void) {
    if (row == 5) {
        control();
    }
    if (row == 6) {
        update_status();
        if (show_gauge) {
            update_gauge();
        }
    }
    if (row == 7) {
        #if 1
        if (show_coords) {
            int16_t p_w = cross_x - cross_x_default;

            #define NUMBERS_START 14
            statusbar_ram_bits[NUMBERS_START + 0] = 0;
            statusbar_ram_bits[NUMBERS_START + 1] = 0;
            if (p_w < 0) {
                statusbar_ram_bits[NUMBERS_START + 1] = CHARGEN_MINUS;
                p_w = -p_w;
            } else if (p_w > 0) {
                statusbar_ram_bits[NUMBERS_START + 1] = CHARGEN_PLUS;
            }
            statusbar_ram_bits[NUMBERS_START + 2] = CHARGEN_NUMBERS + p_w % 10;
            p_w /= 10;
            if (p_w) {
                statusbar_ram_bits[NUMBERS_START - 1] = CHARGEN_X;
                statusbar_ram_bits[NUMBERS_START + 0] = statusbar_ram_bits[NUMBERS_START + 1];
                statusbar_ram_bits[NUMBERS_START + 1] = CHARGEN_NUMBERS + p_w % 10;
            } else {
                statusbar_ram_bits[NUMBERS_START + 0] = CHARGEN_X;
            }

            p_w = cross_y_default - cross_y;
            statusbar_ram_bits[NUMBERS_START + 4] = 0;
            statusbar_ram_bits[NUMBERS_START + 5] = 0;
            statusbar_ram_bits[NUMBERS_START + 6] = 0;
            if (p_w < 0) {
                statusbar_ram_bits[NUMBERS_START + 6] = CHARGEN_MINUS;
                p_w = -p_w;
            } else if (p_w > 0) {
                statusbar_ram_bits[NUMBERS_START + 6] = CHARGEN_PLUS;
            }
            statusbar_ram_bits[NUMBERS_START + 7] = CHARGEN_NUMBERS + p_w % 10;
            p_w /= 10;
            if (p_w) {
                statusbar_ram_bits[NUMBERS_START + 4] = CHARGEN_Y;
                statusbar_ram_bits[NUMBERS_START + 5] = statusbar_ram_bits[NUMBERS_START + 6];
                statusbar_ram_bits[NUMBERS_START + 6] = CHARGEN_NUMBERS + p_w % 10;
            } else {
                statusbar_ram_bits[NUMBERS_START + 5] = CHARGEN_Y;
            }
        }
        #endif
        if ((CROSS_CENTRE + cross_height[cross_type] / 2) >= GAUGE_BOTTOM_START) {
            gauge_start = GAUGE_TOP_START;
        } else {
            gauge_start = GAUGE_BOTTOM_START;
        }
    }
    switch (state) {
        case state_status:
            if (row >= STATUSBAR_START) {
                state = state_main;
                current_fn = draw_status;
                return;
            }
            break;
        case state_main:
            if (row >= MAINWIN_START) {
                if ((menu == 1) || (show_menu)) {
                    current_fn = draw_menu;
                    state = state_bottom;
                    return;
                } /* else if (show_gauge) {
                    current_fn = draw_gauge;
                    state = state_bottom;
                    return;
                } */
            }
            if (row >= (CROSS_CENTRE - cross_height[cross_type] / 2)) {
                if (show_cross) {
                    current_fn = draw_cross;
                    state = state_bottom;
                    return;
                }
            } else if (row == GAUGE_TOP_START) {
                if (show_gauge) {
                    if (gauge_start == GAUGE_TOP_START) {
                        current_fn = draw_gauge;
                    }
                }
            }
            if ((menu == 2) && (!show_cross) && (!show_gauge) && (!show_menu)) {
                state = state_bottom;
            }
            break;
        case state_bottom:
            if (row == GAUGE_BOTTOM_START) {
                if (show_gauge) {
                    if (gauge_start == GAUGE_BOTTOM_START) {
                        current_fn = draw_gauge;
                    }
                }
            }
            if (save_settings_request && ((menu == 0) || force_save_settings)) {
                save_settings();
            } else {
                uint16_t vrefint = *((__IO uint16_t*) 0x1ffff7ba);
                battery_level = (330L * vrefint / 4096 * adc_buffer[1] / adc_buffer[2]);
            }
            if (read_compass_request && live_compass) {
                if (Compass_Read(0x00, (void *) &azimuth)) {
                    read_compass_request = false;
                    compass_retries = 50;
                    if (compass_off_request) {
                        live_compass = false;
                        compass_off_request = false;
                    }
                }
            }
            if (compass_setup_state) {
                if (!live_compass) {
                switch (compass_setup_state) {
                    case 1:
                        if (Compass_Write(0x02, 0x01)) {
                            compass_setup_state++;
                        }
                        break;

                    case 5:
                        if (Compass_Write(0x02, 0x03)) {
                            compass_setup_state++;
                        }
                        break;

                    case 6:
                    case 7:
                        if (once) {
                            compass_setup_state++;
                        }
                        break;

                    case 8: {
                        uint8_t status[2];
                        if (once && Compass_Read(0x02, &status)) {
                            if ((status[0] & 0x2) == 0x0) {
                                compass_setup_state++;
                            }
                        }
                    }   break;

                    case 9:
                        if (Compass_Write(0x02, 0x00)) {
                            compass_setup_state = 0;
                            menu--;
                            show_menu = false;
                            set_language();
                            show_cross = true;
                            real_start_inv = start_inv = MAINWIN_START + 3 + current_item * 16;
                            real_end_inv = end_inv = MAINWIN_START + 14 + current_item * 16;
                            current_menu_height = menu_height;
                            live_compass = true;
                        }
                        break;
                }
                }
            }

            if (once) {
                once = false;
                if (start_inv != real_start_inv) {
                    int16_t d = (start_inv - real_start_inv) / 5;
                    if (d != 0) {
                        real_start_inv += (d * 2);
                        real_end_inv += (d * 2);
                    } else {
                        real_start_inv = start_inv;
                        real_end_inv = end_inv;
                    }
                }
                if (!configuring_oled) {
                    if (set_dbrightness_request || set_dcontrast_request) {
                        set_dbrightness_request = false;
                        set_dcontrast_request = false;
                        send2_packet();
                    }
                }
                if (initial_camera_delay) {
                    initial_camera_delay--;
                } else if (!configuring_camera) {
                    if (set_polarity_request) {
                        set_polarity_request = false;
                        Tx1Buffer[1] = 0x03;
                        Tx1Buffer[3] = 0x01;
                        Tx1Buffer[4] = polarity ? 0x00 : 0x0f;
                        send1_packet();
                    } else if (set_zoom_request) {
                        set_zoom_request = false;
                        Tx1Buffer[1] = 0x03;
                        Tx1Buffer[3] = 0x02;
                        Tx1Buffer[4] = current_zoom ? (1 << current_zoom) : 0;
                        send1_packet();
                    } else if (set_palette_request) {
                        set_palette_request = false;
                        Tx1Buffer[1] = 0x03;
                        Tx1Buffer[3] = 0x11;
                        Tx1Buffer[4] = settings.palette;
                        send1_packet();
                    } else if (calibration_button_request) {
                        if (frameno == 0) {
                            if ((calibration_button_request & 1) == 0) {
                                CBUTGPIO->ODR &= (~calibration_button);
                            } else {
                                CBUTGPIO->ODR |= calibration_button;
                            }
                            calibration_button_request--;
                        }
                    } else if (calibration_request) {
                        if (frameno == 0) {
                            CBUTGPIO->ODR &= (~(CALIBRATION_BUTTON_RIGHT | CALIBRATION_BUTTON_LEFT));
                            if ((calibration_request & 1) == 0) {
                                CBUTGPIO->ODR &= (~CALIBRATION_BUTTON_DOWN);
                            } else {
                                CBUTGPIO->ODR |= CALIBRATION_BUTTON_DOWN;
                            }
                            //send1_packet();
                            calibration_request--;
                            if (calibration_request == 0) {
                                CBUTGPIO->ODR |= (CALIBRATION_BUTTONS_ALL);
                            }
                        }

                    }
                }
            }
            break;
        case state_skip:
            break;
    }
}

void draw_status(void) {
                Delay(LEFT_OFFSET + 100);

                int i;
                int status_row = row - STATUSBAR_START - 1;

                const char * ptr = &statusbar_bits[status_row][0];

                int current_width;
                if (current_item >= 0) {
                    current_width = menu_widths[current_item];
                } else {
                    current_width = 0;
                }

                if (menu < 2) {
                    SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[0]]));
                    SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[1]]));
                    for (i = 2; i < 24; i++) {
                        SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[i]]));
                        while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                    }
                    int j = 1;
                    #if 1
                    for (; i < 29; i++) {
                        uint8_t b;
                        int s = compass_shifts[j];
                        /* if (s < 0) {
                            b = (((((uint16_t)ptr[compass_bits[j - 1]]) << 8) |
                                 ptr[compass_bits[j]]) >> (-s)) & 0xff;
                        } else*/
                        if (status_row < 16) {
                        if (s == 0xff) {
                            b = 0;
                        } else {
                            b = (((((uint16_t)ptr[compass_bits[j + 1]]) << 8) |
                                 ptr[compass_bits[j]]) >> s) & 0xff;
                        }
                        } else {
                            b = (i == 26) ? 0x01 : 0;
                        }
                        SPI_SendData8(SPI1, ~(b));
                        while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                        j++;
                    }
                    #endif
                    for (; i < (STATUSBAR_WIDTH / 8) - 2; i++) {
                        SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[i]]));
                        while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                    }
                } else {
                    SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[0]]));
                    int left = 15 - (current_width + 1) / 2;
                    SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[1]]));
                    int right = left + current_width;
                    for (i = 2; i < left; i++) {
                        SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[i]]));
                        while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                    }
                    /* if (status_row < 16) */ {
                        const char * ptr2 = &menu_bits[current_item * 16 + status_row][0];
                        for (; i < right; i++) {
                            SPI_SendData8(SPI1, ~(*(ptr2++)));
                            while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                            if (((*(ptr2)) == (*(ptr2 - 1))) && ((*(ptr2)) == 0)) break;
                        }
                    }
                    for (; i < (STATUSBAR_WIDTH / 8) - 2; i++) {
                        SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[i]]));
                        while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                    }
                }
                static uint16_t count = 0;
                if (++count == 350) {
                    count = 0;
                    batteryblink = !batteryblink;
                    send = true;

                    //button = button_down;
                    if ((battery_low < 80) && (battery_level <= BATTERY_LOW_LEVEL)) {
                        battery_low++;
                    } else if ((battery_low > 0) && (battery_level > BATTERY_LOW_LEVEL)) {
                        battery_low--;
                    }

                    if (show_gauge && show_saving) {
                        if (!(--show_saving)) {
                            gauge_select = 0;
                            show_gauge = false;
                        }
                    }
                }
                if (batteryblink && (battery_low == 80)) {
                    SPI_SendData8(SPI1, ~(ptr[i]));
                    SPI_SendData8(SPI1, ~(ptr[i + 1]));
                } else {
                    SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[i]]));
                    SPI_SendData8(SPI1, ~(ptr[statusbar_ram_bits[i + 1]]));
                }
                SPI_SendData8(SPI1, 0xff);

    if (row == (STATUSBAR_START + 16 + 4)) {
        current_fn = draw_nothing;
    }
}

void draw_menu(void) {
                Delay(LEFT_OFFSET + 160);

                int i;
                const char * ptr;
                if (menu < 2) {
                    ptr = &menu_bits[row - MAINWIN_START][0];
                } else {
                    switch (compass_setup_state) {
                        case 0:
                            ptr = &helper_bits[row - MAINWIN_START][0];
                            break;

                        case 1:
                        case 2:
                            ptr = &compass_setup_x_bits[row - MAINWIN_START][0];
                            break;

                        case 3:
                            ptr = &compass_setup_y_bits[row - MAINWIN_START][0];
                            break;

                        case 4:
                        default:
                            ptr = &compass_setup_z_bits[row - MAINWIN_START][0];
                            break;
                    }
                }
                bool notselected = ((row < real_start_inv) | (row > real_end_inv));
                if (notselected) {
                    SPI_SendData8(SPI1, ~(*(ptr++)));
                    SPI_SendData8(SPI1, ~(*(ptr++)));
                } else {
                    SPI_SendData8(SPI1, (*(ptr++)));
                    SPI_SendData8(SPI1, (*(ptr++)));
                }
                for (i = 2; i < (menu_width / 8); i++) {
                    uint8_t data = (*(ptr++));
                    if (notselected) {
                        data ^= 0xff;
                    }
                    SPI_SendData8(SPI1, data);
                    while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                }
                SPI_SendData8(SPI1, 0xff);

    if (row >= (MAINWIN_START + current_menu_height - 1)) {
        current_fn = draw_nothing;
    }
}

void draw_cross(void) {
                uint16_t CROSS_START = CROSS_CENTRE - cross_height[cross_type] / 2;
                Delay(cross_x);

                int i;
                const char * ptr;
                const char * ptrs[] = {
                    [cross_type_big]  = &cross_big_lines[cross_big_bits[row - CROSS_START]][0],
                    [cross_type_small]  = &cross_small_lines[cross_small_bits[row - CROSS_START]][0],
                    [cross_type_tdot] = &cross_tdot_lines[cross_tdot_bits[row - CROSS_START]][0],
                    [cross_type_tdot_mini] = &cross_tdot_mini_lines[cross_tdot_mini_bits[row - CROSS_START]][0],
                    [cross_type_milt] = &cross_milt_lines[cross_milt_bits[row - CROSS_START]][0]
                };

                ptr = ptrs[cross_type];

                SPI_SendData8(SPI1, ~(*(ptr++)));
                SPI_SendData8(SPI1, ~(*(ptr++)));
                for (i = 2; i < (menu_width / 8); i++) {
                    uint8_t data = (*(ptr++));
                    data ^= 0xff;
                    SPI_SendData8(SPI1, data);
                    while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                }
                SPI_SendData8(SPI1, 0xff);

    if (row >= (CROSS_CENTRE + cross_height[cross_type] / 2 - 1)) {
        current_fn = draw_nothing;
    }
}

void draw_gauge(void) {
                Delay(LEFT_OFFSET + 180);

                int i;
                const char * ptr = &statusbar_bits[gauge_select * 16 + row - gauge_start - 1][0];

                //SPI_SendData8(SPI1, ~(ptr[gauge_ram_bits[0]]));
                //SPI_SendData8(SPI1, ~(ptr[gauge_ram_bits[1]]));
                for (i = 0; i < sizeof(gauge_ram_bits); i++) {
                    SPI_SendData8(SPI1, ~(ptr[gauge_ram_bits[i]]));
                    if (i > 1) while (SPI_GetTransmissionFIFOStatus(SPI1) != SPI_TransmissionFIFOStatus_HalfFull);
                }
                SPI_SendData8(SPI1, 0xff);

    if (row >= (gauge_start + 16)) {
        current_fn = draw_nothing;
    }
}

void HSYNC(void) {
    if (row < MAX_ROW) {
        row++;
    }

    current_fn();
}

static void init_settings(void) {
    int i;
    for (i = 0; i < MAX_USERS; i++) {
        int j;
        for (j = 0; j < MAX_ZOOMS; j++) {
            settings.users[i].coords[j].x = CROSS_X_DEFAULT;
            settings.users[i].coords[j].y = CROSS_Y_DEFAULT;
        }
        settings.users[i].brightness = MAX_BRIGHTNESS;
        settings.users[i].cross_type = cross_type_min;
    }

    settings.seqno = 0;
    settings.invalid = 0x12345678;
    settings.brightness = MAX_GAUGE_VALUE / 2;
    settings.contrast = MAX_GAUGE_VALUE / 2;
    settings.palette = 8;
    settings.language = LANG_EN;
}

static void reset_confirm(int button) {
    menu++;
    current_menu = &confirm_menu;
    show_menu = true;
    current_menu_height = 2 * 16;
    current_confirm = 1;
    real_start_inv = start_inv = MAINWIN_START + 3 + current_confirm * 16;
    real_end_inv = end_inv = MAINWIN_START + 14 + current_confirm * 16;
}

static void reset_settings(int button) {
    init_settings();
    update_dbrightness(settings.brightness);
    update_dcontrast(settings.contrast);
    set_language();
    reload_settings = true;
    set_palette_request = true;
    save_settings_request = true;
    force_save_settings = true;
}

static void load_settings(void) {
    if (settings0.invalid == 0x12345678) {
        memcpy(&settings, &settings0, sizeof(struct settings_t));
    } else {
        init_settings();
    }
}

enum {
    FLASH_Idle = 0,
    FLASH_Unlocked,
    FLASH_Erasing,
    FLASH_Erased
};

static void save_settings(void) {
#if 1
    SPI_Cmd(SPI1, DISABLE);
    FLASH_Unlock();
    FLASH_ErasePage((uint32_t)&settings0);
    int i, words = (sizeof(settings) + 3) / 4;

    for (i = 0; i < words; i++) {
        FLASH_ProgramWord(((uint32_t)&(((uint32_t *)&settings0)[i])),
                          (((uint32_t *)&settings)[i]));
    }
    SPI_Cmd(SPI1, ENABLE);

    save_settings_request = false;
    force_save_settings = false;
    if (show_saving) {
        show_gauge = true;
        gauge_select = 1;
    }

    state = state_skip;
#else
    static bool saving = false;
    static char state = FLASH_Idle;

    {
        static int i, words;
        if (!saving) {
            switch (state) {
                case FLASH_Idle: {
                    FLASH_Unlock();
                    state = FLASH_Unlocked;
                    break;
                }
                case FLASH_Unlocked: {
                    if (FLASH_GetStatus() != FLASH_BUSY) {
                        SPI_Cmd(SPI1, DISABLE);
                        FLASH_ErasePage_AsyncStart((uint32_t)&settings0);
                        state = FLASH_Erasing;
                    }
                    break;
                }
                case FLASH_Erasing: {
                    if (FLASH_GetStatus() != FLASH_BUSY) {
                        FLASH_ErasePage_AsyncStop();
                        state = FLASH_Erased;
                    }
                    break;
                }
                case FLASH_Erased: {
                    if (FLASH_GetStatus() != FLASH_BUSY) {
                        state = FLASH_Unlocked;
                        i = 0;
                        words = (sizeof(settings) + 3) / 4;
                        saving = true;
                        SPI_Cmd(SPI1, ENABLE);
                    }
                    break;
                }
                //
            };
        } else {
            if (i < words) {
                FLASH_ProgramWord(((uint32_t)&(((uint32_t *)&settings0)[i])),
                                  (((uint32_t *)&settings)[i]));
                i++;
            } else {
                saving = false;
                save_settings_request = false;
                force_save_settings = false;
                if (show_saving) {
                    show_gauge = true;
                    gauge_select = 1;
                }
            }
        }
    }
#endif
}

#if 1
//uint8_t TxBuffer[16] = {0xf0, 0x02, 0x26, 0x00, 0x26, 0xff};
volatile uint8_t NbrOfDataToTransfer1 = 7;

volatile uint8_t NbrOfDataToTransfer2 = 6;

void USART2_IRQHandler(void) {
    if (USART_GetITStatus(USART2, USART_IT_TXE) != RESET)
    {
        /* Write one byte to the transmit data register */
        USART_SendData(USART2, Tx1Buffer[Tx1Count++]);

        if (Tx1Count == NbrOfDataToTransfer1)
        {
            /* Disable the USART1 Transmit interrupt */
            USART_ITConfig(USART2, USART_IT_TXE, DISABLE);
            configuring_camera = false;
        }
    }
}

void USART1_IRQHandler(void) {
    if (USART_GetITStatus(USART1, USART_IT_TXE) != RESET)
    {
        /* Write one byte to the transmit data register */
        USART_SendData(USART1, Tx2Buffer[Tx2Count++]);

        if (Tx2Count == NbrOfDataToTransfer2)
        {
            /* Disable the USART2 Transmit interrupt */
            USART_ITConfig(USART1, USART_IT_TXE, DISABLE);
            configuring_oled = false;
        }
    }
}
#endif

void send1_packet(void) {
    int length = Tx1Buffer[1];
    int i;
    uint8_t checksum = 0;
    for (i = 0; i < length; i++) {
        checksum = 0xff & (checksum + Tx1Buffer[2 + i]);
    }
    Tx1Buffer[2 + length] = checksum;
    Tx1Buffer[2 + length + 1] = 0xff;
    NbrOfDataToTransfer1 = length + 4;
    configuring_camera = true;
    Tx1Count = 0;
    USART_ITConfig(USART2, USART_IT_TXE, ENABLE);
}

void send2_packet(void) {
    Tx2Buffer[0] = 0x02;
    Tx2Buffer[1] = 0x21;
    Tx2Buffer[2] = 0x05;
    //Tx2Buffer[3] = 0x4a;
    //Tx2Buffer[4] = 0x02;
    Tx2Buffer[3] = 0x09;
    /* 128 +/-36 */
    Tx2Buffer[4] = 128 - 36 + (3 * dcontrast);
    Tx2Buffer[5] = 0x08;
    /* 128 +/-36 */
    Tx2Buffer[6] = 128 - 36 + (3 * dbrightness);
    Tx2Buffer[7] = 0x03;
    configuring_oled = true;
    Tx2Count = 0;
    NbrOfDataToTransfer2 = Tx2Buffer[2] + 3;
    USART_ITConfig(USART1, USART_IT_TXE, ENABLE);
}

int main(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    EXTI_InitTypeDef EXTI_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;
    DMA_InitTypeDef DMA_InitStructure;
    SPI_InitTypeDef SPI_InitStructure;
    ADC_InitTypeDef ADC_InitStructure;
    DAC_InitTypeDef DAC_InitStructure;
    TIM_TimeBaseInitTypeDef TIM_InitStructure;
    USART_InitTypeDef USART_InitStructure;

    /* Enable the GPIO_LED Clock */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);
    /* Configure the GPIO_LED pin */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_3;
    //GPIO_Init(GPIOB, &GPIO_InitStructure);

    CBUTGPIO->ODR |= CALIBRATION_BUTTON_RIGHT;
    GPIO_InitStructure.GPIO_Pin = CALIBRATION_BUTTON_RIGHT;
    GPIO_Init(CBUTGPIO, &GPIO_InitStructure);

    CBUTGPIO->ODR |= CALIBRATION_BUTTON_LEFT;
    GPIO_InitStructure.GPIO_Pin = CALIBRATION_BUTTON_LEFT;
    GPIO_Init(CBUTGPIO, &GPIO_InitStructure);

    CBUTGPIO->ODR |= CALIBRATION_BUTTON_UP;
    GPIO_InitStructure.GPIO_Pin = CALIBRATION_BUTTON_UP;
    GPIO_Init(CBUTGPIO, &GPIO_InitStructure);

    CBUTGPIO->ODR |= CALIBRATION_BUTTON_DOWN;
    GPIO_InitStructure.GPIO_Pin = CALIBRATION_BUTTON_DOWN;
    GPIO_Init(CBUTGPIO, &GPIO_InitStructure);

    /* Enable GPIOA clock */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_OD;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* Configure PA.04 (DAC1_OUT1) in analog mode -------------------------*/
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AN;
    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* Enable DAC clock */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_DAC, ENABLE);

    /* DAC1 channel1 Configuration */
    DAC_InitStructure.DAC_Trigger = DAC_Trigger_None;
    DAC_InitStructure.DAC_OutputBuffer = DAC_OutputBuffer_Enable;
    DAC_Init(DAC_Channel_1, &DAC_InitStructure);

    /* Enable DAC1 Channel1 */
    DAC_Cmd(DAC_Channel_1, ENABLE);

    /* Enable GPIOB clock */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOB, ENABLE);

    /* Configure PB8 pin as input floating */
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = A_MASK;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = B_MASK;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

    /* Connect EXTI2 Line to PA2 pin */
    SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOA, EXTI_PinSource2);

    /* Configure EXTI2 line */
    EXTI_InitStructure.EXTI_Line = EXTI_Line2;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    /* Enable and set EXTI2-3 Interrupt to the lowest priority */
    NVIC_InitStructure.NVIC_IRQChannel = EXTI2_3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPriority = 0x00;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);

    SPI_InitStructure.SPI_Direction = SPI_Direction_1Line_Tx; // Initially Tx
    SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
    SPI_InitStructure.SPI_CPOL = SPI_CPOL_High; // Clock steady high
    SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge; // Data write on rising (second) edge
    SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;
    SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_LSB;
    SPI_Init(SPI1, &SPI_InitStructure);
    SPI_RxFIFOThresholdConfig(SPI1, SPI_RxFIFOThreshold_QF);
    SPI_NSSInternalSoftwareConfig(SPI1, SPI_NSSInternalSoft_Set);
    SPI_Cmd(SPI1, ENABLE);

    //GPIOB->BSRR = GPIO_Pin_3;
    GPIOA->BSRR = GPIO_Pin_7;

    GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_0);

    Compass_Setup();

    /* ADC1 Periph clock enable */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

    /* Configure ADC Channel0 and Channel1 as analog inputs */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    ADC_DeInit(ADC1);
    ADC_StructInit(&ADC_InitStructure);
    /* Configure the ADC1 in continuous mode withe a resolution equal to 12 bits*/
    ADC_InitStructure.ADC_Resolution = ADC_Resolution_12b;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_ScanDirection = ADC_ScanDirection_Upward;
    ADC_Init(ADC1, &ADC_InitStructure);

    ADC_VrefintCmd(ENABLE);

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1,ENABLE);

    DMA_DeInit(DMA1_Channel1);

    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)adc_buffer;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = (sizeof(adc_buffer) / sizeof(uint16_t));
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &DMA_InitStructure);

    DMA_Cmd(DMA1_Channel1, ENABLE);

    ADC_ChannelConfig(ADC1, ADC_Channel_0, ADC_SampleTime_239_5Cycles);
    ADC_ChannelConfig(ADC1, ADC_Channel_1, ADC_SampleTime_239_5Cycles);
    ADC_ChannelConfig(ADC1, ADC_Channel_Vrefint, ADC_SampleTime_239_5Cycles);

    ADC_DMARequestModeConfig(ADC1, ADC_DMAMode_Circular);

    adccal = ADC_GetCalibrationFactor(ADC1);
    ADC_DMACmd(ADC1, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = ADC1_COMP_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    ADC_ITConfig(ADC1, ADC_IT_EOSEQ, ENABLE);

    ADC_Cmd(ADC1, ENABLE);
    while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_ADEN));
    //while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_ADRDY));
    //ADC_OverrunModeCmd(ADC1, ENABLE);
    //ADC_WaitModeCmd(ADC1, ENABLE);
    //ADC_DiscModeCmd(ADC1, ENABLE);
    //ADC_StartOfConversion(ADC1);

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);

    TIM_InitStructure.TIM_Prescaler = 0;
    TIM_InitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_InitStructure.TIM_Period = 0xffff;
    TIM_InitStructure.TIM_ClockDivision = TIM_CKD_DIV1;

    TIM_TimeBaseInit(TIM6, &TIM_InitStructure);

    //TIM_PrescalerConfig(TIM6, 0, TIM_PSCReloadMode_Immediate);

    TIM_Cmd(TIM6, ENABLE);

    /* USARTx configured as follow:
    - BaudRate = 115200 baud  
    - Word Length = 8 Bits
    - One Stop Bit
    - No parity
    - Hardware flow control disabled (RTS and CTS signals)
    - Receive and transmit enabled
    */
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

    USART_DeInit(USART1);
    USART_DeInit(USART2);

    RCC_USARTCLKConfig(RCC_USART1CLK_PCLK);

    /* Enable USART clock */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    /* Connect PXx to USARTx_Tx */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource14, GPIO_AF_1);

    /* Connect PXx to USARTx_Rx */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource15, GPIO_AF_1);

    /* Configure USART Tx, Rx as alternate function push-pull */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14 | GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* USART configuration */
    USART_Init(USART2, &USART_InitStructure);

    USART2->BRR = 2500;

    /* Enable USART */
    USART_Cmd(USART2, ENABLE);

    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;

    /* Enable GPIO clock */
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_GPIOA, ENABLE);

    /* Enable USART clock */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    
    /* Connect PXx to USARTx_Tx */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_1);

    /* Connect PXx to USARTx_Rx */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_1);
    
    /* Configure USART Tx, Rx as alternate function push-pull */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* USART configuration */
    USART_Init(USART1, &USART_InitStructure);

    USART1->BRR = 5000;

    /* Enable USART */
    USART_Cmd(USART1, ENABLE);

#if 1
    /* Enable the USART1 Interrupt */
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* Enable the USART1 Interrupt */
    NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
#endif

    cross_x = CROSS_X_DEFAULT;
    cross_y = CROSS_Y_DEFAULT;

    load_settings();

    update_status();

    update_brightness(brightness);
    update_dbrightness(settings.brightness);
    update_dcontrast(settings.contrast);
    set_language();
    send2_packet();

    set_zoom_request = true;
    set_polarity_request = true;
    set_palette_request = true;

    current_fn = draw_nothing;

    while(1)
    {
        PWR_EnterSleepMode(PWR_SLEEPEntry_WFE);
#if 0
        if (send) {
            fputc('A', NULL);
            send = false;
        }
#endif
        //USART_SendData(USART2, 0xa7);
    }
}

/**
  * @brief  Retargets the C library printf function to the USART.
  * @param  None
  * @retval None
  */
int fputc(int ch, FILE *f)
{
    USART_TypeDef* USARTx = (f == stderr) ? USART2 : USART1;
    USART_SendData(USARTx, (uint8_t) ch);

    /* Loop until transmit data register is empty */
    while (USART_GetFlagStatus(USARTx, USART_FLAG_TXE) == RESET)
    {}

    return ch;
}

