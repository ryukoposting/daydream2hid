#include "main.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/gpio.h>


#define MSEC_TO_ISO(msec_) (msec_)

#define CONN_INTERVAL_MIN_MSEC      15
#define CONN_INTERVAL_MAX_MSEC      15
#define CONN_TIMEOUT_MSEC           2000


static void start_scan();
static void on_scan_device_found(
    const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
    struct net_buf_simple *ad);
static void on_connected(struct bt_conn *conn, uint8_t err);
static void on_disconnected(struct bt_conn *conn, uint8_t reason);
static void on_mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx);
static void on_gatt_exchange_mtu(struct bt_conn *conn, uint8_t err,
    struct bt_gatt_exchange_params *params);
static uint8_t on_notify(struct bt_conn *conn,
    struct bt_gatt_subscribe_params *params,
    const void *data, uint16_t length);



static struct bt_conn *open_conn = NULL;
static struct bt_uuid_128 discover_uuid = {};
static struct bt_gatt_discover_params discover_params = {};
static struct bt_gatt_subscribe_params subscribe_params = {};

static const struct gpio_dt_spec bt_status_led = 
    GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct bt_uuid_16 google_service_uuid = BT_UUID_INIT_16(0xfe55);
static const struct bt_uuid_128 daydream_data_uuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0x00000001, 0x1000, 0x1000, 0x8000, 0x00805f9b34fb));

BT_CONN_CB_DEFINE(conn_cbs) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

static struct bt_gatt_cb gatt_callbacks = {
    .att_mtu_updated = on_mtu_updated
};

static struct bt_gatt_exchange_params mtu_exchange_params = {
    .func = on_gatt_exchange_mtu
};

LOG_MODULE_REGISTER(bluetooth, LOG_LEVEL_INF);


static void start_scan()
{
    int err = bt_le_scan_start(BT_LE_SCAN_ACTIVE, on_scan_device_found);
    if (err) {
        LOG_ERR("bt_le_scan_start: %d", err);
        return;
    }

    led_flash(750, 250, LED_BT_STATUS);

    LOG_INF("Starting scan");
}

static bool is_daydream_controller(struct bt_data *data, void *user_data)
{
    bool *is_daydream = user_data;

    if (data->type != BT_DATA_NAME_COMPLETE) {
        return true;
    }

    char cmp[] = "Daydream controller";
    if (data->data_len != sizeof(cmp) - 1) {
        return false;
    }

    *is_daydream = 0 == memcmp(data->data, cmp, sizeof(cmp) - 1);
    return false;
}

static void on_scan_device_found(
    const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
    struct net_buf_simple *ad)
{
    if (type != BT_GAP_ADV_TYPE_ADV_IND &&
        type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
        return;
    }

    LOG_HEXDUMP_DBG(ad->data, ad->len, "ad");

    bool is_daydream = false;
    bt_data_parse(ad, is_daydream_controller, &is_daydream);

    if (is_daydream) {
        char dev[BT_ADDR_LE_STR_LEN];
        bt_addr_le_to_str(addr, dev, sizeof(dev));
        LOG_INF("Found daydream! %s", dev);
    } else {
        return;
    }

    int err = bt_le_scan_stop();
    if (err) {
        LOG_ERR("bt_le_scan_stop: %d", err);
        return;
    }

    struct bt_conn *conn;
    err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN,
        BT_LE_CONN_PARAM_DEFAULT, &conn);
    if (err) {
        LOG_ERR("bt_conn_le_create: %d", err);
        start_scan();
        return;
    }

    bt_conn_unref(conn);
}

static void set_conn_params(struct k_work *work)
{
    static struct bt_le_conn_param params = BT_LE_CONN_PARAM_INIT(
        BT_GAP_MS_TO_CONN_INTERVAL(CONN_INTERVAL_MIN_MSEC),
        BT_GAP_MS_TO_CONN_INTERVAL(CONN_INTERVAL_MAX_MSEC),
        1,
        BT_GAP_MS_TO_CONN_TIMEOUT(CONN_TIMEOUT_MSEC)
    );
    bt_conn_le_param_update(open_conn, &params);
}
K_WORK_DEFINE(conn_params_work, set_conn_params);

static uint8_t on_notify(struct bt_conn *conn,
   struct bt_gatt_subscribe_params *params,
   const void *data, uint16_t length)
{
    if (!data) {
        return BT_GATT_ITER_STOP;
    } else if (length != DAYDREAM_PKT_SIZE) {
        LOG_HEXDUMP_ERR(data, length, "incorrect packet length");
        return BT_GATT_ITER_STOP;
    }

    LOG_HEXDUMP_DBG(data, length, "notification");

    int err = daydream_queue_pkt(data, K_MSEC(1));
    if (err) {
        LOG_ERR("daydream_queue_pkt: %d", err);
    }

    return BT_GATT_ITER_CONTINUE;
}

static uint8_t on_gatt_svc_discover(struct bt_conn *conn,
    const struct bt_gatt_attr *attr,
    struct bt_gatt_discover_params *params)
{
    if (!attr) {
        return BT_GATT_ITER_STOP;
    }

    int err = 0;

    if (!bt_uuid_cmp(params->uuid, &google_service_uuid.uuid)) {
        LOG_DBG("Discovered service!");
        memcpy(&discover_uuid, &daydream_data_uuid, sizeof(struct bt_uuid_128));
        discover_params.start_handle = attr->handle + 1;
        discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;
        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            LOG_ERR("bt_gatt_discover: %d", err);
        }
    } else if (!bt_uuid_cmp(params->uuid, &daydream_data_uuid.uuid)) {
        LOG_DBG("Discovered characteristic!");
        memcpy(&discover_uuid, BT_UUID_GATT_CCC, sizeof(struct bt_uuid_16));
        discover_params.start_handle = attr->handle + 2;
        discover_params.type = BT_GATT_DISCOVER_DESCRIPTOR;
        subscribe_params.value_handle = bt_gatt_attr_value_handle(attr);
        err = bt_gatt_discover(conn, &discover_params);
        if (err) {
            LOG_ERR("bt_gatt_discover: %d", err);
        }
    } else {
        LOG_DBG("Enabling notification!");
        subscribe_params.notify = on_notify;
        subscribe_params.value = BT_GATT_CCC_NOTIFY;
        subscribe_params.ccc_handle = attr->handle;
        err = bt_gatt_subscribe(conn, &subscribe_params);
        if (err && err != -EALREADY) {
            LOG_ERR("bt_gatt_subscribe: %d", err);
        } else {
            led_off(LED_BT_STATUS);
        }
        k_work_submit(&conn_params_work);
    }

    return BT_GATT_ITER_STOP;
}

static void on_connected(struct bt_conn *conn, uint8_t status)
{
    if (status) {
        LOG_WRN("Error %u", status);
        open_conn = NULL;
        mouse_reset();
        start_scan();
    }

    led_flash(150, 250, LED_BT_STATUS);

    int err;
    open_conn = bt_conn_ref(conn);

    err = bt_gatt_exchange_mtu(open_conn, &mtu_exchange_params);
    if (err) {
        LOG_ERR("bt_gatt_exchange_mtu: %d", err);
    }

    err = bt_conn_set_security(open_conn, BT_SECURITY_L2);
    if (err) {
        LOG_ERR("bt_conn_set_security: %d", err);
    }

    memcpy(&discover_uuid, &google_service_uuid, sizeof(struct bt_uuid_16));
    discover_params.uuid = &discover_uuid.uuid;
    discover_params.func = on_gatt_svc_discover;
    discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    discover_params.type = BT_GATT_DISCOVER_PRIMARY;
    err = bt_gatt_discover(open_conn, &discover_params);
    if (err) {
        LOG_ERR("bt_gatt_discover: %d", err);
    }
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_WRN("Disconnected %u", reason);
    open_conn = NULL;
    mouse_reset();
    start_scan();
}

static void on_mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
    LOG_INF("Updated MTU: TX: %d RX: %d bytes\n", tx, rx);
}

static void on_gatt_exchange_mtu(struct bt_conn *conn, uint8_t err,
    struct bt_gatt_exchange_params *params)
{
    LOG_INF("on_gatt_exchange_mtu err=%u", err);
}

int bluetooth_is_connected()
{
    return open_conn != NULL;
}

int boot_bluetooth()
{
    int err;

    led_on(LED_BT_STATUS);

    err = bt_enable(NULL);
    if (err) {
        LOG_ERR("Bluetooth init failed: %d", err);
        return err;
    }

    bt_gatt_cb_register(&gatt_callbacks);

    start_scan();

    return err;
}
