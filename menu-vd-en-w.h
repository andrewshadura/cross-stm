#include "menu-vd-en.h"

const unsigned char menu_en_widths[] = {
    11,
    12,
    //13,
    10,
    8,
    23,
    //9,
    8,
    5
};

#define SAVING_EN_MIN 3
#define SAVING_EN_MAX 8

#undef CROSS_X_DEFAULT
#define CROSS_X_DEFAULT (LEFT_OFFSET + 175)

#include "helper-en.h"
