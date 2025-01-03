#include "main.h"
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void)
{
    int ret;

    ret = boot_leds();
    if (ret < 0) {  
        return 0;
    }

    ret = boot_usb();
    if (ret < 0) {  
        return 0;
    }

    ret = boot_mouse();
    if (ret < 0) {  
        return 0;
    }

    ret = boot_bluetooth();
    if (ret < 0) {  
        return 0;
    }

    while (true) {
        UDC_STATIC_BUF_DEFINE(report, MOUSE_REPORT_COUNT);

        mouse_fetch_hid(report);

        ret = usb_write_hid(report);
        if (ret) {
            LOG_ERR("HID write error, %d", ret);
        } else {
            usb_wait_ep();
        }
    }
    return 0;
}
