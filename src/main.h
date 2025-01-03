#pragma once

#include <zephyr/kernel.h>
#include <zephyr/usb/usbd.h>

#define DAYDREAM_PKT_SIZE 20

#define MINMAX(min_, x_, max_) MIN(max_, MAX(min_, x_))

enum scroll_direction {
    SCROLL_NONE,
    SCROLL_UP,
    SCROLL_DOWN,
    SCROLL_LOCK,
};

enum mouse_report_idx {
    MOUSE_BTN_REPORT_IDX,
    MOUSE_X_REPORT_IDX,
    MOUSE_Y_REPORT_IDX,
    MOUSE_WHEEL_REPORT_IDX,
    MOUSE_REPORT_COUNT
};

enum led_id {
    LED_BT_STATUS,
    LED_USB_READY,
    LED_GYRO_ACTIVE,
    LED_COUNT
};

struct daydream_pkt {
    int orient_x;
    int orient_y;
    int orient_z;
    int accel_x;
    int accel_y;
    int accel_z;
    int gyro_x;
    int gyro_y;
    int gyro_z;
    int trackpad_x;
    int trackpad_y;
    int duration;
    uint16_t sqn : 5;
    uint16_t vol_up : 1;
    uint16_t vol_dn : 1;
    uint16_t app : 1;
    uint16_t home : 1;
    uint16_t trackpad_btn : 1;
};

struct scroll_state {
    enum scroll_direction direction;
    int64_t since;
};

struct move_state {
    int64_t since;
};

struct button_state {
    bool pressed;
    int duration;
};


/* buttons */
void button_update(int pressed, int duration, struct button_state *state);

/* daydream */
int daydream_queue_pkt(uint8_t const *pkt, k_timeout_t timeout);

/* leds */
int boot_leds();
void led_on(enum led_id id);
void led_off(enum led_id id);
void led_flash(int on_msec, int off_msec, enum led_id id);

/* mouse */
int boot_mouse();
void mouse_reset();
int mouse_push_daydream(struct daydream_pkt const *pkt);
int mouse_fetch_hid(uint8_t *buf);

/* usb_hid */
int boot_usb();
void usb_rwup_if_suspended();
int usb_write_hid(uint8_t *buf);
int usb_wait_ep();

/* usbd */
struct usbd_context *usbd_init_device(usbd_msg_cb_t msg_cb);

/* bluetooth */
int boot_bluetooth();
int bluetooth_is_connected();
