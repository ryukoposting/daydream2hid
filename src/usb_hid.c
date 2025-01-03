/* mostly copied from Zephyr's USB HID example */
#include "main.h"
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usb_hid.h>

LOG_MODULE_REGISTER(usb_hid);

static const uint8_t hid_report_desc[] = HID_MOUSE_REPORT_DESC(2);
static enum usb_dc_status_code usb_status;

static K_SEM_DEFINE(ep_write_sem, 0, 1);

static inline void status_cb(enum usb_dc_status_code status, const uint8_t *param)
{
    usb_status = status;
}

static void int_in_ready_cb(const struct device *dev)
{
    ARG_UNUSED(dev);
    k_sem_give(&ep_write_sem);
}

void usb_rwup_if_suspended()
{
    if (IS_ENABLED(CONFIG_USB_DEVICE_REMOTE_WAKEUP)) {
        if (usb_status == USB_DC_SUSPEND) {
            usb_wakeup_request();
            return;
        }
    }
}

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
static int enable_usb_device_next()
{
    struct usbd_context *sample_usbd;
    int err;

    sample_usbd = usbd_init_device(NULL);
    if (sample_usbd == NULL) {
        LOG_ERR("Failed to initialize USB device");
        return -ENODEV;
    }

    err = usbd_enable(sample_usbd);
    if (err) {
        LOG_ERR("Failed to enable device support");
        return err;
    }

    LOG_DBG("USB device support enabled");

    return 0;
}
#endif /* defined(CONFIG_USB_DEVICE_STACK_NEXT) */

static const struct device *hid_dev;
static const struct hid_ops ops = {
    .int_in_ready = int_in_ready_cb,
};

int boot_usb()
{
    int ret;

#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
    hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
#else
    hid_dev = device_get_binding("HID_0");
#endif
    if (hid_dev == NULL) {
        LOG_ERR("Cannot get USB HID Device");
        return -ENOENT;
    }

    usb_hid_register_device(hid_dev,
                hid_report_desc, sizeof(hid_report_desc),
                &ops);

    usb_hid_init(hid_dev);


#if defined(CONFIG_USB_DEVICE_STACK_NEXT)
    ret = enable_usb_device_next();
#else
    ret = usb_enable(status_cb);
#endif
    if (ret != 0) {
        LOG_ERR("Failed to enable USB");
        return ret;
    }

    led_on(LED_USB_READY);

    return ret;
}

int usb_wait_ep()
{
    return k_sem_take(&ep_write_sem, K_FOREVER);
}

int usb_write_hid(uint8_t *buf)
{
    return hid_int_ep_write(hid_dev, buf, MOUSE_REPORT_COUNT, NULL);
}
