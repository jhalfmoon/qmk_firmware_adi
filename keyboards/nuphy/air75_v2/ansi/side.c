/*
Copyright 2023 @ Nuphy <https://nuphy.com/>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "user_kb.h"
#include "ansi.h"
#include "side_table.h"
#include "ws2812.h"
#include "mcu_pwr.h"

#define SIDE_BRIGHT_MAX     5
#define SIDE_SPEED_MAX      4
#define SIDE_COLOUR_MAX     8

#define LEFT_SIDE           0
#define RIGHT_SIDE          1
#define BOTH_SIDES          2

#define SIDE_LINE           6
#define SIDE_LED_NUM        12

#define RF_LED_LINK_PERIOD  500
#define RF_LED_PAIR_PERIOD  250


/* side rgb mode */
enum {
    SIDE_WAVE = 0,
    SIDE_MIX,
    SIDE_STATIC,
    SIDE_BREATH,
    SIDE_OFF,
};

bool     flush_side_leds    = false;
uint8_t  side_mode          = 0;
uint8_t  side_light         = SIDE_LIGHT_INIT;
uint8_t  side_speed         = 2;
uint8_t  side_rgb           = 1;
uint8_t  side_colour        = 0;
uint8_t  side_one           = 0;
uint8_t  side_play_point    = 0;
uint32_t side_one_timer     = 0;
uint8_t  r_temp, g_temp, b_temp;
uint8_t  sys_light          = 2;
rgb_led_t side_leds[SIDE_LED_NUM] = {0};


const uint8_t side_speed_table[5][5] = {
    [SIDE_WAVE]   = {14, 19, 25, 32, 40}, // [SIDE_WAVE]   = {10, 14, 20, 28, 38},
    [SIDE_MIX]    = {14, 19, 25, 32, 40}, // [SIDE_MIX]    = {10, 14, 20, 28, 38},
    [SIDE_STATIC] = {50, 50, 50, 50, 50}, // [SIDE_STATIC] = {50, 50, 50, 50, 50},
    [SIDE_BREATH] = {14, 19, 25, 32, 40}, // [SIDE_BREATH] = {10, 14, 20, 28, 38},
    [SIDE_OFF]    = {50, 50, 50, 50, 50}, // [SIDE_OFF]    = {50, 50, 50, 50, 50},
};

const uint8_t side_light_table[7] = {
    0,
    14,
    22,
    34,
    55,
    79,
    106,
};

const uint8_t side_led_index_tab[SIDE_LINE][2] = {
    {5, 6},
    {4, 7},
    {3, 8},
    {2, 9},
    {1, 10},
    {0, 11},
};

extern DEV_INFO_STRUCT dev_info;
extern user_config_t   user_config;
extern uint8_t         rf_blink_cnt;
extern uint16_t        rf_link_show_time;
extern uint16_t        side_led_last_act;
extern bool            f_bat_hold;
extern bool            f_sys_show;
extern bool            f_sleep_show;
extern bool            f_wakeup_prepare;
extern uint16_t        sleep_time_delay;
extern uint32_t        eeprom_update_timer;
extern bool            side_update;


void side_ws2812_setleds(rgb_led_t *ledarray, uint16_t leds);
void rgb_matrix_update_pwm_buffers(void);

// Copied from old nuphy code. Check if side RGB has values set.
bool is_side_rgb_off(void) {
    for (int i = 0; i < SIDE_LED_NUM; i++) {
        if ((side_leds[i].r != 0) || (side_leds[i].g != 0) || (side_leds[i].b != 0)) {
            return false;
        }
    }
    return true;
}

/**
 * @brief  side leds set color vaule.
 * @param  i: index of side_leds[].
 * @param  ...
 */
void side_rgb_set_color(int i, uint8_t r, uint8_t g, uint8_t b) {
    r >>= 2, g >>= 2, b >>= 2;
    if (side_leds[i].r != r || side_leds[i].g != g || side_leds[i].b != b) {
        flush_side_leds = true;
    }
    side_leds[i].r = r;
    side_leds[i].g = g;
    side_leds[i].b = b;
}

void set_side_rgb(uint8_t side, uint8_t r, uint8_t g, uint8_t b) {
    // side = 0 => left
    // side = 1 => right
    // side = 2 => both
    uint8_t start =  0;
    uint8_t end   = 12;
    if (side == 0) end = end - 6;
    if (side == 1) start = start + 6;

    for (int i = start; i < end; i++) {
	side_rgb_set_color(i, r, g, b);
    }
}

/**
 * @brief  refresh side leds.
 */
void side_rgb_refresh(void) {
    if (!is_side_rgb_off() && !f_wakeup_prepare) {
        side_led_last_act = 0;
        pwr_side_led_on(); // power on side LED before refresh
    }
    if (!flush_side_leds) return;
    side_ws2812_setleds(side_leds, SIDE_LED_NUM);
    flush_side_leds = false;
}

/**
 * @brief  Adjusting the brightness of side lights.
 * @param  bright: 0 - decrease, 1 - increase.
 * @note  save to eeprom.
 */
void side_light_control(uint8_t bright) {
    if (bright) {
        if (side_light > SIDE_BRIGHT_MAX) {
            return;
        } else
            side_light++;
    } else {
        if (side_light == 0) {
            return;
        } else
            side_light--;
    }
    user_config.ee_side_light = side_light;
    eeprom_update_timer = timer_read32();
    side_update = 1;
    // eeconfig_update_kb_datablock(&user_config);
}

/**
 * @brief  Adjusting the speed of side lights.
 * @param  fast: 0 - decrease, 1 - increase.
 * @note  save to eeprom.
 */
void side_speed_control(uint8_t fast) {
    if ((side_speed) > SIDE_SPEED_MAX) (side_speed) = SIDE_SPEED_MAX / 2;

    if (fast) {
        if ((side_speed)) side_speed--;
    } else {
        if ((side_speed) < SIDE_SPEED_MAX) side_speed++;
    }
    user_config.ee_side_speed = side_speed;
    eeprom_update_timer = timer_read32();
    side_update = 1;
    // eeconfig_update_kb_datablock(&user_config);
}

/**
 * @brief  Switch to the next color of side lights.
 * @param  color: 0 - prev, 1 - next.
 * @note  save to eeprom.
 */
void side_colour_control(uint8_t color) {
    if (side_mode != SIDE_WAVE) {
        if (side_rgb) {
            side_rgb    = 0;
            side_colour = 0;
        }
    }

    if (color) {
        if (side_rgb) {
            side_rgb    = 0;
            side_colour = 0;
        } else {
            side_colour++;
            if (side_colour >= SIDE_COLOUR_MAX) {
                side_rgb    = 1;
                side_colour = 0;
            }
        }
    } else {
        if (side_rgb) {
            side_rgb    = 0;
            side_colour = SIDE_COLOUR_MAX - 1;
        } else {
            side_colour--;
            if (side_colour >= SIDE_COLOUR_MAX) {
                side_rgb    = 1;
                side_colour = 0;
            }
        }
    }
    user_config.ee_side_rgb    = side_rgb;
    user_config.ee_side_colour = side_colour;
    eeprom_update_timer = timer_read32();
    side_update = 1;
    // eeconfig_update_kb_datablock(&user_config);
}

/**
 * @brief  Change the color mode of side lights.
 * @param  dir: 0 - prev, 1 - next.
 * @note  save to eeprom.
 */
void side_mode_control(uint8_t dir) {
    if (dir) {
        side_mode++;
        if (side_mode > SIDE_OFF) {
            side_mode = 0;
        }
    } else {
        if (side_mode > 0) {
            side_mode--;
        } else {
            side_mode = SIDE_OFF;
        }
    }
    side_play_point          = 0;
    user_config.ee_side_mode = side_mode;
    eeprom_update_timer = timer_read32();
    side_update = 1;
    // eeconfig_update_kb_datablock(&user_config);
}

/**
 * @brief  sys sw led show.
 */
void sys_sw_led_show(void) {
    static uint32_t sys_show_timer = 0;
    static bool     sys_show_flag  = false;

    if (f_sys_show) {
        f_sys_show     = false;
        sys_show_timer = timer_read32();
        sys_show_flag  = true;
    }

    if (sys_show_flag) {
        if (dev_info.sys_sw_state == SYS_SW_MAC) {
            r_temp = 0x80;
            g_temp = 0x80;
            b_temp = 0x80;
        } else {
            r_temp = 0x00;
            g_temp = 0x00;
            b_temp = 0x80;
        }
        if ((timer_elapsed32(sys_show_timer) / 500) % 2 == 0) {
            set_side_rgb(RIGHT_SIDE, r_temp / sys_light, g_temp / sys_light, b_temp / sys_light);
        } else {
            set_side_rgb(RIGHT_SIDE, 0x00, 0x00, 0x00);
        }
        if (timer_elapsed32(sys_show_timer) >= 3000) {
            sys_show_flag = false;
        }
    }
}

/**
 * @brief  sleep_sw_led_show.
 */
void sleep_sw_led_show(void) {
    static uint32_t sleep_show_timer = 0;
    static bool     sleep_show_flag  = false;

    if (f_sleep_show) {
        f_sleep_show     = false;
        sleep_show_timer = timer_read32();
        sleep_show_flag  = true;
    }

    if (sleep_show_flag) {
        if (user_config.sleep_enable) {
            if (sleep_time_delay < (100 * 360)) {
                r_temp = 0x00;
                g_temp = 0x80;
                b_temp = 0x00;
            } else {
                r_temp = 0x80;
                g_temp = 0x40;
                b_temp = 0x00;
            }
        } else {
            r_temp = 0x80;
            g_temp = 0x00;
            b_temp = 0x00;
        }
        if ((timer_elapsed32(sleep_show_timer) / 500) % 2 == 0) {
            set_side_rgb(RIGHT_SIDE, r_temp / sys_light, g_temp / sys_light, b_temp / sys_light);
        } else {
            set_side_rgb(RIGHT_SIDE, 0x00, 0x00, 0x00);
        }
        if (timer_elapsed32(sleep_show_timer) >= 3000) {
            sleep_show_flag = false;
        }
    }
}

/**
 * @brief  sys_led_show.
 */
void sys_led_show(void) {
    g_temp = 0x80, b_temp = 0x80, r_temp = 0x00;
    uint8_t led_side = LEFT_SIDE;
    if (host_keyboard_led_state().caps_lock) {
        led_side = RIGHT_SIDE;
        set_side_rgb(LEFT_SIDE, r_temp / sys_light, g_temp / sys_light, b_temp / sys_light);
    }

    if (host_keyboard_led_state().num_lock) {
        r_temp = 0x80;
        set_side_rgb(led_side, r_temp / sys_light, g_temp / sys_light, b_temp / sys_light);
    }
}

/**
 * @brief  light_point_playing.
 * @param trend:
 * @param step:
 * @param len:
 * @param point:
 */
static void light_point_playing(uint8_t trend, uint8_t step, uint8_t len, uint8_t *point) {
    if (trend) {
        *point += step;
        if (*point >= len) *point -= len;
    } else {
        *point -= step;
        if (*point >= len) *point = len - (255 - *point) - 1;
    }
}

/**
 * @brief  count_rgb_light.
 * @param light_temp:
 */
static void count_rgb_light(uint8_t light_temp) {
    uint16_t temp;

    temp   = (light_temp)*r_temp + r_temp;
    r_temp = temp >> 8;

    temp   = (light_temp)*g_temp + g_temp;
    g_temp = temp >> 8;

    temp   = (light_temp)*b_temp + b_temp;
    b_temp = temp >> 8;
}

/**
 * @brief  side_wave_mode_show.
 */
static void side_wave_mode_show(void) {
    uint8_t play_index;

    if (side_rgb)
        light_point_playing(0, 3, FLOW_COLOUR_TAB_LEN, &side_play_point);
    else
        light_point_playing(0, 2, WAVE_TAB_LEN, &side_play_point);
    
    play_index = side_play_point;
    for (int i = 0; i < SIDE_LINE; i++) {
        if (side_rgb) {
            r_temp = flow_rainbow_colour_tab[play_index][0];
            g_temp = flow_rainbow_colour_tab[play_index][1];
            b_temp = flow_rainbow_colour_tab[play_index][2];

            light_point_playing(1, 16, FLOW_COLOUR_TAB_LEN, &play_index);
        } else {
            r_temp = colour_lib[side_colour][0];
            g_temp = colour_lib[side_colour][1];
            b_temp = colour_lib[side_colour][2];

            light_point_playing(1, 12, WAVE_TAB_LEN, &play_index);
            count_rgb_light(wave_data_tab[play_index]);
        }

        count_rgb_light(side_light_table[side_light]);

        for (int j = 0; j < 2; j++) {
            side_rgb_set_color(side_led_index_tab[i][j], r_temp, g_temp, b_temp);
        }
    }
}

/**
 * @brief  side_spectrum_mode_show.
 */
static void side_spectrum_mode_show(void) {
    light_point_playing(1, 1, FLOW_COLOUR_TAB_LEN, &side_play_point);

    r_temp = flow_rainbow_colour_tab[side_play_point][0];
    g_temp = flow_rainbow_colour_tab[side_play_point][1];
    b_temp = flow_rainbow_colour_tab[side_play_point][2];

    count_rgb_light(side_light_table[side_light]);

    set_side_rgb(BOTH_SIDES, r_temp, g_temp, b_temp);
}

/**
 * @brief  side_breathe_mode_show.
 */
static void side_breathe_mode_show(void) {
    static uint8_t play_point = 0;

    light_point_playing(0, 1, BREATHE_TAB_LEN, &play_point);

    r_temp = colour_lib[side_colour][0];
    g_temp = colour_lib[side_colour][1];
    b_temp = colour_lib[side_colour][2];

    count_rgb_light(breathe_data_tab[play_point]);
    count_rgb_light(side_light_table[side_light]);

    set_side_rgb(BOTH_SIDES ,r_temp, g_temp, b_temp);
}

/**
 * @brief  side_static_mode_show.
 */
static void side_static_mode_show(void) {
    if (side_play_point >= SIDE_COLOUR_MAX) side_play_point = 0;

    r_temp = colour_lib[side_colour][0];
    g_temp = colour_lib[side_colour][1];
    b_temp = colour_lib[side_colour][2];
 
    count_rgb_light(side_light_table[side_light]);
    set_side_rgb(BOTH_SIDES ,r_temp, g_temp, b_temp);
}

/**
 * @brief  side_off_mode_show.
 */
static void side_off_mode_show(void) {
    r_temp = 0x00, g_temp = 0x00, b_temp = 0x00;
    set_side_rgb(BOTH_SIDES, r_temp, g_temp, b_temp);
}

/**
 * @brief  side_one_control
 */
void side_one_control(uint8_t s_one)
{
    if (s_one) {

        if (side_one == 3) {
            side_one = 0;
        } else {
            side_one++;
            side_one_timer = 1;
        }

        user_config.ee_side_one = side_one;
        // eeprom_update_timer = timer_read32();
        side_update = 1;
        eeconfig_update_kb_datablock(&user_config);
    }
}

/**
 * @brief  side_one_show
 */
static void side_one_show(void)
{
    static uint8_t my_side = 0;
    r_temp = 0x00, g_temp = 0x00, b_temp = 0x00;

    if (side_one == 1)
        set_side_rgb(LEFT_SIDE, r_temp, g_temp, b_temp);
    if (side_one == 2)
        set_side_rgb(RIGHT_SIDE, r_temp, g_temp, b_temp);
    if (side_one == 3) {
        if (side_one_timer == 1) {
            my_side = 0;
            side_one_timer = 0;
        }
        if (side_one_timer == 0)
            side_one_timer = timer_read32();

        if (timer_elapsed32(side_one_timer) < 500 ) {
            set_side_rgb(BOTH_SIDES, r_temp, g_temp, b_temp);
            return;
        } else if (timer_elapsed32(side_one_timer) >= (1000 * 60 * 3) ) {
            side_one_timer = 0;
            my_side = my_side == 0 ? 1 : 0;
        }

        if (my_side == 0)
            set_side_rgb(RIGHT_SIDE, r_temp, g_temp, b_temp);
        else
            set_side_rgb(LEFT_SIDE, r_temp, g_temp, b_temp);
    }
}

/**
 * @brief  rf_led_show.
 */
void rf_led_show(void) {
    static uint32_t rf_blink_timer = 0;
    uint16_t        rf_blink_period = 0;

    if (rf_blink_cnt || (rf_link_show_time < RF_LINK_SHOW_TIME)) {
        if (dev_info.link_mode == LINK_RF_24) {
            r_temp = 0x00;
            g_temp = 0x80;
            b_temp = 0x00;
        } else if (dev_info.link_mode == LINK_USB) {
            r_temp = 0x80;
            g_temp = 0x80;
            b_temp = 0x00;
        } else {
            r_temp = 0x00;
            g_temp = 0x00;
            b_temp = 0x80;
        }
    } else {
        rf_blink_timer = timer_read32();
        return;
    }

    if (rf_blink_cnt) {
        if (dev_info.rf_state == RF_PAIRING)
            rf_blink_period = RF_LED_PAIR_PERIOD;
        else
            rf_blink_period = RF_LED_LINK_PERIOD;

        if (timer_elapsed32(rf_blink_timer) > (rf_blink_period >> 1)) {
            r_temp = 0x00;
            g_temp = 0x00;
            b_temp = 0x00;
        }

        if (timer_elapsed32(rf_blink_timer) >= rf_blink_period) {
            rf_blink_cnt--;
            rf_blink_timer = timer_read32();
        }
    }

    set_side_rgb(LEFT_SIDE, r_temp / sys_light, g_temp / sys_light, b_temp / sys_light);

    // light up corresponding BT/RF key
    if (dev_info.link_mode <= LINK_BT_3) {
        uint8_t my_pos = dev_info.link_mode == LINK_RF_24 ? 26 : (30 - dev_info.link_mode);
        rgb_matrix_set_color(my_pos, r_temp, g_temp, b_temp);
    }
}

/**
 * @brief  bat_num_led.
 */
void bat_num_led(uint8_t bat_percent)
{
    uint8_t r, g, b;
    uint8_t last_r, last_g, last_b;
    uint8_t bat_pct = bat_percent / 10;

    // set color
    if (bat_percent <= 15) {
        r = 0xff; g = 0x00; b = 0x00;
    }
    else if (bat_percent <= 50) {
        r = 0xff; g = 0x40; b = 0x00;
    }
    else if (bat_percent <= 80) {
        r = 0xff; g = 0xff; b = 0x00;
    }
    else {
        r = 0x00; g = 0xff; b = 0x00;
    }

    // set percent

    if ((bat_percent % 10) == 0) {
        bat_pct--;
        last_r = 0x00; last_g = 0xff; last_b = 0x00;
    } else if ((bat_percent % 10) <= 3) {
        last_r = 0xff; last_g = 0x40; last_b = 0x00;
    } else if ((bat_percent % 10) <= 6) {
        last_r = 0xff; last_g = 0xff; last_b = 0x00;
    } else {
        last_r = 0x00; last_g = 0xff; last_b = 0x00;
    }

    for(int i=0; i <= (bat_pct - 1); i++)
        rgb_matrix_set_color(29 - i, r, g, b);

    rgb_matrix_set_color(29 - bat_pct, last_r, last_g, last_b);
}

void num_led_show(void)
{
    static uint8_t num_bat_temp         = 0;
    num_bat_temp         = dev_info.rf_battery;
    bat_num_led(num_bat_temp);
}

void bat_led_close(void)
{
    for(int i=20; i<=29; i++) {
        rgb_matrix_set_color(i,0,0,0);
    }
}

/**
 * @brief  bat_percent_led.
 */
void bat_percent_led(uint8_t bat_percent) {
    uint8_t bat_end_led = 0;
    uint8_t bat_r, bat_g, bat_b;
    bat_r = 0x80, bat_g = 0x40, bat_b = 0;

    if (bat_percent <= 15) {
        bat_end_led = 0;
        bat_r = 0x80, bat_g = 0;
    } else if (bat_percent <= 30) {
        bat_end_led = 1;
        bat_r = 0x80, bat_g = 0;
    } else if (bat_percent <= 40) {
        bat_end_led = 1;
    } else if (bat_percent <= 60) {
        bat_end_led = 2;
    } else if (bat_percent <= 70) {
        bat_end_led = 3;
    } else if (bat_percent <= 80) {
        bat_end_led = 4;
    } else if (bat_percent <= 90) {
        bat_end_led = 5;
    } else {
        bat_end_led = 5;
        bat_r = 0, bat_g = 0x80;
    }

    uint8_t i = 0;
    for (; i <= bat_end_led; i++)
        side_rgb_set_color(11 - i, bat_r / sys_light, bat_g / sys_light, bat_b / sys_light);

    for (; i < 6; i++)
        side_rgb_set_color(11 - i, 0, 0, 0);
}

/**
 * @brief  bat_led_show.
 */
void bat_led_show(void) {
    static bool bat_show_flag   = 1;
    static bool bat_show_breath = 0;
    static bool f_init          = 1;

    static uint8_t  play_point     = 0;
    static uint32_t bat_play_timer = 0;
    static uint32_t bat_show_time  = 0;

    static uint32_t bat_sts_debounce = 0;
    static uint32_t bat_per_debounce = 0;
    static uint8_t  charge_state     = 0;
    static uint8_t  bat_percent      = 0;

    if (f_init) {
        f_init        = 0;
        bat_show_time = timer_read32();
        charge_state  = dev_info.rf_charge;
        bat_percent   = dev_info.rf_battery;
    }

    if (charge_state != dev_info.rf_charge) {
        if (timer_elapsed32(bat_sts_debounce) > 1000) {
            if (((charge_state & 0x01) == 0) && ((dev_info.rf_charge & 0x01) != 0)) {
                bat_show_flag   = true;
                bat_show_breath = true;
                bat_show_time   = timer_read32();
            }
            charge_state = dev_info.rf_charge;
        }
    } else {
        bat_sts_debounce = timer_read32();
        if (timer_elapsed32(bat_show_time) > 5000) {
            bat_show_flag   = false;
            bat_show_breath = false;
        }
        if (charge_state == 0x03) {
            bat_show_breath = true;
        } else if (charge_state & 0x01) {
            dev_info.rf_battery = 100;
        }
    }

    if (bat_percent != dev_info.rf_battery) {
        if (timer_elapsed32(bat_per_debounce) > 1000) {
            bat_percent = dev_info.rf_battery;
        }
    } else {
        bat_per_debounce = timer_read32();
        if (bat_percent < 15) {
            bat_show_flag = true;
            bat_show_time = timer_read32();
        }
    }

    if (f_bat_hold || bat_show_flag) {
        if (bat_show_breath) {
            if (timer_elapsed32(bat_play_timer) > 10) {
                bat_play_timer = timer_read32();
                light_point_playing(0, 1, BREATHE_TAB_LEN, &play_point);
            }
            r_temp = 0x80;
            g_temp = 0x40;
            b_temp = 0x00;
            count_rgb_light(breathe_data_tab[play_point]);
            set_side_rgb(RIGHT_SIDE, r_temp / sys_light, g_temp / sys_light, b_temp / sys_light);
        } else {
            bat_percent_led(bat_percent);
        }
    }
}

/**
 * @brief  device_reset_show.
 */
void device_reset_show(void) {

    pwr_rgb_led_on();
    pwr_side_led_on();

    for (int blink_cnt = 0; blink_cnt < 3; blink_cnt++) {
        rgb_matrix_set_color_all(0x10, 0x10, 0x10);
        set_side_rgb(BOTH_SIDES, 0x40, 0x40, 0x40);
        rgb_matrix_update_pwm_buffers();
        side_rgb_refresh();
        wait_ms(200);

        rgb_matrix_set_color_all(0x00, 0x00, 0x00);
        set_side_rgb(BOTH_SIDES, 0x00, 0x00, 0x00);
        rgb_matrix_update_pwm_buffers();
        side_rgb_refresh();
        wait_ms(200);
    }
}

/**
 * @brief  device_reset_init.
 */
void device_reset_init(void) {
    side_play_point = 0;
    f_bat_hold      = false;

    rgb_matrix_enable();
    rgb_matrix_mode(RGB_MATRIX_DEFAULT_MODE);
    rgb_matrix_set_speed(255 - RGB_MATRIX_SPD_STEP * 2);

    user_config_reset();
}

/**
 *       RGB test
*/

void rgb_test_show(void) {
    // open power control
    pwr_rgb_led_on();
    pwr_side_led_on();

    uint8_t colours[7][3] = {
        { 0xFF, 0x00, 0x00 },
        { 0x00, 0xFF, 0x00 },
        { 0x00, 0x00, 0xFF },
        { 0x80, 0x80, 0x80 },
        { 0x80, 0x80, 0x00 },
        { 0x80, 0x00, 0x80 },
        { 0x00, 0x80, 0x80 }
    };

    for (uint8_t i = 0; i < 7; i++) {
        uint8_t r = colours[i][0];
        uint8_t g = colours[i][1];
        uint8_t b = colours[i][2];

        rgb_matrix_set_color_all(r, g, b);
        rgb_matrix_update_pwm_buffers();
        set_side_rgb(BOTH_SIDES, r, g, b);
        side_rgb_refresh();
        wait_ms(500);
    }
}

/**
 * @brief  side_led_show.
 */
void side_led_show(void) {
    static uint32_t side_refresh_time = 0;
    static uint32_t side_update_time  = 0;

    // side_mode & side_speed should always be valid...
    // refresh side LED animation based on speed.
    uint8_t update_interval = side_speed_table[side_mode][side_speed];
    if (timer_elapsed32(side_update_time) >= update_interval) {

        sys_light = side_light > 5 ? 1 : (3 - side_light / 2);
        side_update_time = timer_read32();

        switch (side_mode) {
            case SIDE_WAVE:
                side_wave_mode_show();
                break;
            case SIDE_MIX:
                side_spectrum_mode_show();
                break;
            case SIDE_BREATH:
                side_breathe_mode_show();
                break;
            case SIDE_STATIC:
                side_static_mode_show();
                break;
            case SIDE_OFF:
                side_off_mode_show();
                break;
        }
    }

    side_one_show();
    bat_led_show();
    sleep_sw_led_show();
    sys_sw_led_show();

    sys_led_show();
    rf_led_show();

    // This only refreshes if LEDs change anyways. Fixes stuttering side LEDs
    // The interval should be at least as fast as the fastest side refresh interval...
    if (timer_elapsed32(side_refresh_time) >= 14) {
        side_refresh_time = timer_read32();
        side_rgb_refresh();
    }

}
