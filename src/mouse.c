#include "main.h"
#include <math.h>
#include <zephyr/logging/log.h>


#define ACCEL_IN_MAX 4096
#define GYRO_IN_MAX 4096
#define TRACKPAD_IN_MAX 255

#define TRACKPAD_ACCELERATION 150
#define TRACKPAD_VELOCITY 2500
#define TRACKPAD_DRAG_DIVISOR 20

#define GYRO_ACCELERATION 20
#define GYRO_VELOCITY 500
#define ACCEL_MIX_VELOCITY 10

#define MOUSE_BTN_LEFT 0
#define MOUSE_BTN_RIGHT 1
#define GRAVITY 550


enum movement_mode {
    MODE_TRACKPAD,
    MODE_GYRO,
};

enum buttons {
    BTN_TRACKPAD,
    BTN_HOME,
    BTN_APP,
    BTN_VDOWN,
    BTN_VUP,
    N_BUTTONS
};

struct trackpad {
    int x;
    int y;
    bool init;
};

struct gyro {
    int x;
    int y;
    bool init;
};


static void mouse_worker_handler(struct k_work *work);
static void mouse_timer_handler(struct k_timer *timer);
static void move_by_trackpad(struct daydream_pkt const *pkt, int8_t *x, int8_t *y);
static void move_by_gyro(struct daydream_pkt const *pkt, int8_t *x, int8_t *y);
static int8_t scroll_velocity(int duration);


LOG_MODULE_REGISTER(mouse, LOG_LEVEL_INF);
K_MSGQ_DEFINE(mouse_hid_queue, MOUSE_REPORT_COUNT, 8, sizeof(void*));

static struct button_state buttons[N_BUTTONS] = {};
static struct trackpad trackpad = {};
static struct gyro gyro = {};

int mouse_push_daydream(struct daydream_pkt const *pkt)
{
    int8_t hid_msg[MOUSE_REPORT_COUNT] = {};

    button_update(pkt->trackpad_btn, pkt->duration, &buttons[BTN_TRACKPAD]);
    button_update(pkt->home, pkt->duration, &buttons[BTN_HOME]);
    button_update(pkt->app, pkt->duration, &buttons[BTN_APP]);
    button_update(pkt->vol_dn, pkt->duration, &buttons[BTN_VDOWN]);
    button_update(pkt->vol_up, pkt->duration, &buttons[BTN_VUP]);


    if (!buttons[BTN_HOME].pressed) {
        led_off(LED_GYRO_ACTIVE);
        move_by_trackpad(pkt, &hid_msg[MOUSE_X_REPORT_IDX], &hid_msg[MOUSE_Y_REPORT_IDX]);
    } else {
        led_on(LED_GYRO_ACTIVE);
        move_by_gyro(pkt, &hid_msg[MOUSE_X_REPORT_IDX], &hid_msg[MOUSE_Y_REPORT_IDX]);
    }

    if (buttons[BTN_VDOWN].pressed && !buttons[BTN_VUP].pressed) {
        hid_msg[MOUSE_WHEEL_REPORT_IDX] = -scroll_velocity(buttons[BTN_VDOWN].duration);
    } else if (buttons[BTN_VUP].pressed && !buttons[BTN_VDOWN].pressed) {
        hid_msg[MOUSE_WHEEL_REPORT_IDX] = scroll_velocity(buttons[BTN_VUP].duration);
    }

    WRITE_BIT(hid_msg[MOUSE_BTN_REPORT_IDX], MOUSE_BTN_LEFT, pkt->trackpad_btn);
    WRITE_BIT(hid_msg[MOUSE_BTN_REPORT_IDX], MOUSE_BTN_RIGHT, pkt->app);

    int err = k_msgq_put(&mouse_hid_queue, hid_msg, K_MSEC(5));
    if (err) {
        LOG_ERR("k_msgq_put: %d", err);
    }
    return err;
}

void mouse_reset()
{
    memset(buttons, 0, sizeof(buttons));
    trackpad.init = 0;
    gyro.init = 0;
    k_msgq_purge(&mouse_hid_queue);
}

static int pythag(int x, int y)
{
    return round(sqrt(x * x + y * y));
}

static void move_by_trackpad(struct daydream_pkt const *pkt, int8_t *x, int8_t *y)
{
    if (pkt->trackpad_x == 0 && pkt->trackpad_y == 0) {
        trackpad.init = false;
        return;
    }

    if (!trackpad.init) {
        trackpad.init = true;
        trackpad.x = pkt->trackpad_x;
        trackpad.y = pkt->trackpad_y;
        *x = *y = 0;
        return;
    }

    int delta_x = pkt->trackpad_x - trackpad.x;
    trackpad.x = pkt->trackpad_x;

    int delta_y = pkt->trackpad_y - trackpad.y;
    trackpad.y = pkt->trackpad_y;

    int cx = trackpad.x - 127;
    int cy = trackpad.y - 127;
    if (pythag(cx, cy) >= 100 && pkt->trackpad_btn) {
        *x = cx / TRACKPAD_DRAG_DIVISOR;
        *y = cy / TRACKPAD_DRAG_DIVISOR;
        return;
    }

    int vx = delta_x * TRACKPAD_VELOCITY;
    int vy = delta_y * TRACKPAD_VELOCITY;

    int ax = delta_x * delta_x * TRACKPAD_ACCELERATION;
    if (delta_x < 0) {
        ax = -ax;
    }

    int ay = delta_y * delta_y * TRACKPAD_ACCELERATION;
    if (delta_y < 0) {
        ay = -ay;
    }

    int velocity_x = (vx + ax) / (pkt->duration * TRACKPAD_IN_MAX);
    int velocity_y = (vy + ay) / (pkt->duration * TRACKPAD_IN_MAX);

    *x = MINMAX(INT8_MIN, velocity_x, INT8_MAX);
    *y = MINMAX(INT8_MIN, velocity_y, INT8_MAX);
    LOG_DBG("track move t=%d x=%d y=%d", pkt->duration, (int)*x, (int)*y);
}

static void move_by_gyro(struct daydream_pkt const *pkt, int8_t *x, int8_t *y)
{
    if (!gyro.init) {
        gyro.init = true;
        gyro.x = 0;
        gyro.y = 0;
        *x = *y = 0;
        return;
    }

    int delta_x = pkt->gyro_z - gyro.x;
    int delta_y = pkt->gyro_x - gyro.y;

    int vx = delta_x * GYRO_VELOCITY;
    int vy = delta_y * GYRO_VELOCITY;

    int ax = delta_x * delta_x * GYRO_ACCELERATION;
    if (delta_x < 0) {
        ax = -ax;
    }

    int ay = delta_y * delta_y * GYRO_ACCELERATION;
    if (delta_y < 0) {
        ay = -ay;
    }

    /* mix in a bit of the accelerometer. this feels a bit more natural to me */
    int accel_x = pkt->accel_x * ACCEL_MIX_VELOCITY / ACCEL_IN_MAX;
    int accel_y = pkt->accel_z * ACCEL_MIX_VELOCITY / ACCEL_IN_MAX;

    int velocity_x = (accel_x - (vx + ax)) / (pkt->duration * GYRO_IN_MAX);
    int velocity_y = (accel_y - (vy + ay)) / (pkt->duration * GYRO_IN_MAX);

    *x = MINMAX(INT8_MIN, velocity_x, INT8_MAX);
    *y = MINMAX(INT8_MIN, velocity_y, INT8_MAX);
    LOG_DBG("gyro move x=%d y=%d", (int)*x, (int)*y);
}

static int8_t scroll_velocity(int duration)
{
    if (duration <= 70) {
        return 1;
    } else if (duration >= 1500) {
        return 5;
    } else if (duration >= 700) {
        return (duration - 500) / 200;
    } else {
        return 0;
    }
}

int boot_mouse()
{
    return 0;
}

int mouse_fetch_hid(uint8_t *buf)
{
    return k_msgq_get(&mouse_hid_queue, buf, K_FOREVER);
}
