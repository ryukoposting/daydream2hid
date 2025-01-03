/* this file is mostly copied from Zephyr's usb HID example */
#include "main.h"
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/bos.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usbd, LOG_LEVEL_WRN);

/*
 * Instantiate a context named sample_usbd using the default USB device
 * controller, the Zephyr project vendor ID, and the sample product ID.
 * Zephyr project vendor ID must not be used outside of Zephyr samples.
 */
USBD_DEVICE_DEFINE(sample_usbd,
           DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)),
           CONFIG_D2H_DEVICE_VID, CONFIG_D2H_DEVICE_PID);

USBD_DESC_LANG_DEFINE(sample_lang);
USBD_DESC_MANUFACTURER_DEFINE(sample_mfr, CONFIG_D2H_DEVICE_MANUFACTURER);
USBD_DESC_PRODUCT_DEFINE(sample_product, CONFIG_D2H_DEVICE_PRODUCT);
USBD_DESC_SERIAL_NUMBER_DEFINE(sample_sn);

USBD_DESC_CONFIG_DEFINE(fs_cfg_desc, "FS Configuration");
USBD_DESC_CONFIG_DEFINE(hs_cfg_desc, "HS Configuration");

static const uint8_t attributes = (IS_ENABLED(CONFIG_D2H_USBD_SELF_POWERED) ?
                   USB_SCD_SELF_POWERED : 0) |
                  (IS_ENABLED(CONFIG_D2H_USBD_REMOTE_WAKEUP) ?
                   USB_SCD_REMOTE_WAKEUP : 0);

USBD_CONFIGURATION_DEFINE(sample_fs_config,
              attributes,
              CONFIG_D2H_USBD_MAX_POWER, &fs_cfg_desc);

USBD_CONFIGURATION_DEFINE(sample_hs_config,
              attributes,
              CONFIG_D2H_USBD_MAX_POWER, &hs_cfg_desc);

static void sample_fix_code_triple(struct usbd_context *uds_ctx,
                   const enum usbd_speed speed)
{
    /* Always use class code information from Interface Descriptors */
    if (IS_ENABLED(CONFIG_USBD_CDC_ACM_CLASS) ||
        IS_ENABLED(CONFIG_USBD_CDC_ECM_CLASS) ||
        IS_ENABLED(CONFIG_USBD_CDC_NCM_CLASS) ||
        IS_ENABLED(CONFIG_USBD_AUDIO2_CLASS)) {
        /*
         * Class with multiple interfaces have an Interface
         * Association Descriptor available, use an appropriate triple
         * to indicate it.
         */
        usbd_device_set_code_triple(uds_ctx, speed,
                        USB_BCC_MISCELLANEOUS, 0x02, 0x01);
    } else {
        usbd_device_set_code_triple(uds_ctx, speed, 0, 0, 0);
    }
}

static struct usbd_context *sample_usbd_setup_device(usbd_msg_cb_t msg_cb)
{
    int err;

    err = usbd_add_descriptor(&sample_usbd, &sample_lang);
    if (err) {
        LOG_ERR("Failed to initialize language descriptor (%d)", err);
        return NULL;
    }

    err = usbd_add_descriptor(&sample_usbd, &sample_mfr);
    if (err) {
        LOG_ERR("Failed to initialize manufacturer descriptor (%d)", err);
        return NULL;
    }

    err = usbd_add_descriptor(&sample_usbd, &sample_product);
    if (err) {
        LOG_ERR("Failed to initialize product descriptor (%d)", err);
        return NULL;
    }

    err = usbd_add_descriptor(&sample_usbd, &sample_sn);
    if (err) {
        LOG_ERR("Failed to initialize SN descriptor (%d)", err);
        return NULL;
    }

    if (usbd_caps_speed(&sample_usbd) == USBD_SPEED_HS) {
        err = usbd_add_configuration(&sample_usbd, USBD_SPEED_HS,
                         &sample_hs_config);
        if (err) {
            LOG_ERR("Failed to add High-Speed configuration");
            return NULL;
        }

        err = usbd_register_all_classes(&sample_usbd, USBD_SPEED_HS, 1);
        if (err) {
            LOG_ERR("Failed to add register classes");
            return NULL;
        }

        sample_fix_code_triple(&sample_usbd, USBD_SPEED_HS);
    }

    err = usbd_add_configuration(&sample_usbd, USBD_SPEED_FS,
                     &sample_fs_config);
    if (err) {
        LOG_ERR("Failed to add Full-Speed configuration");
        return NULL;
    }

    err = usbd_register_all_classes(&sample_usbd, USBD_SPEED_FS, 1);
    if (err) {
        LOG_ERR("Failed to add register classes");
        return NULL;
    }

    sample_fix_code_triple(&sample_usbd, USBD_SPEED_FS);

    if (msg_cb != NULL) {
        err = usbd_msg_register_cb(&sample_usbd, msg_cb);
        if (err) {
            LOG_ERR("Failed to register message callback");
            return NULL;
        }
    }

    return &sample_usbd;
}

struct usbd_context *usbd_init_device(usbd_msg_cb_t msg_cb)
{
    int err;

    if (sample_usbd_setup_device(msg_cb) == NULL) {
        return NULL;
    }

    err = usbd_init(&sample_usbd);
    if (err) {
        LOG_ERR("Failed to initialize device support");
        return NULL;
    }

    return &sample_usbd;
}
