#include "main.h"
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

struct led {
    struct gpio_dt_spec led;
    struct k_work work;
    struct k_timer timer;
    int on_msec;
    int off_msec;
};


static struct led leds[LED_COUNT] = {
    [LED_BT_STATUS] = { .led = GPIO_DT_SPEC_GET(DT_ALIAS(bt_status_led), gpios) },
    [LED_USB_READY] = { .led = GPIO_DT_SPEC_GET(DT_ALIAS(usb_ready_led), gpios) },
    [LED_GYRO_ACTIVE] = { .led = GPIO_DT_SPEC_GET(DT_ALIAS(gyro_active_led), gpios) },
};

LOG_MODULE_REGISTER(leds, LOG_LEVEL_DBG);


void led_on(enum led_id id)
{
    leds[id].on_msec = 0;
    k_timer_stop(&leds[id].timer);
    k_work_cancel(&leds[id].work);
    gpio_pin_set_dt(&leds[id].led, 1);
}

void led_off(enum led_id id)
{
    leds[id].on_msec = 0;
    k_timer_stop(&leds[id].timer);
    k_work_cancel(&leds[id].work);
    gpio_pin_set_dt(&leds[id].led, 0);
}

void led_flash(int on_msec, int off_msec, enum led_id id)
{
    leds[id].on_msec = on_msec;
    leds[id].off_msec = off_msec;
    k_work_cancel(&leds[id].work);
    gpio_pin_set_dt(&leds[id].led, 1);
    k_timer_start(&leds[id].timer, K_MSEC(on_msec), K_NO_WAIT);
}

static void led_work_handler(struct k_work *work)
{
    struct led *led = CONTAINER_OF(work, struct led, work);
    if (!led->on_msec) {
        return;
    }

    int interval = gpio_pin_get_dt(&led->led)
        ? led->off_msec
        : led->on_msec;

    LOG_DBG("flash!");

    gpio_pin_toggle_dt(&led->led);
    k_timer_start(&led->timer, K_MSEC(interval), K_NO_WAIT);
}

static void led_timer_handler(struct k_timer *timer)
{
    struct led *led = CONTAINER_OF(timer, struct led, timer);
    k_work_submit(&led->work);
}

static int led_init(struct led *led)
{
    k_work_init(&led->work, led_work_handler);
    k_timer_init(&led->timer, led_timer_handler, NULL);
    led->on_msec = 0;
    led->off_msec = 0;

    if (!gpio_is_ready_dt(&led->led)) {
        LOG_ERR("GPIO device not ready");
        return -EAGAIN;
    }

    return gpio_pin_configure_dt(&led->led, GPIO_OUTPUT_INACTIVE);
}

int boot_leds()
{
    int err = 0;
    for (size_t i = 0; i < LED_COUNT; ++i) {
        err = led_init(&leds[i]);
        if (err) {
            LOG_ERR("led_init[%zu]: %d", i, err);
            return err;
        }
    }

    return err;
}

// static int led_thread_fn(void *_unused0, void *_unused1, void *_unused2)
// {

// }

// K_THREAD_DEFINE(led_thread, LED_THREAD_STACK_SIZE,
//     led_thread_fn, NULL, NULL, NULL,
//     LED_THREAD_PRIORITY, 0, 1);
