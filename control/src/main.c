/**
 * @file main.c
 * @author Lowie Deferme <lowie.deferme@kuleuven.be>
 * @brief BLE FreeBot controller
 * @version 0.1
 * @date 2024-05-10
 *
 * @copyright Copyright (c) 2024
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include "freebot_control.h"

LOG_MODULE_REGISTER(gw_ble_ctrl, LOG_LEVEL_DBG);

// -----------------------------------------------------------------------------
// Structures & methods for inter-thread communication/syncronization
// -----------------------------------------------------------------------------

// Semaphore for ble connection: starts unavailable
static K_SEM_DEFINE(ble_conn_sem, 0, 1);

// -----------------------------------------------------------------------------
// Bluetooth LE constants and callbacks
// -----------------------------------------------------------------------------

#define FB_BLE_ADDR "C0:B2:AC:81:2F:93"
static bt_addr_le_t fb_ble_addr;

static struct bt_conn *ble_conn;

static struct bt_uuid_128 dis_uuid = BT_UUID_INIT_128(BT_UUID_FBCS_VAL);

// -----------------------------------------------------------------------------

/** @brief On BLE device found callback */
static void on_device_found(const bt_addr_le_t *addr, int8_t rssi, uint8_t type, struct net_buf_simple *ad)
{
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));

    int err;

    // Do nothing if connection already has something
    if (ble_conn)
    {
        return;
    }

    // Only continue on connectable events
    if (type != BT_GAP_ADV_TYPE_ADV_IND && type != BT_GAP_ADV_TYPE_ADV_DIRECT_IND)
    {
        return;
    }

    // Only connect to nearby devices
    if (rssi < -70)
    {
        return;
    }

    // Only connect if target device
    if (!bt_addr_le_eq(addr, &fb_ble_addr))
    {
        LOG_DBG("Found dev: %s (RSSI %d)", addr_str, rssi);
        return;
    }
    LOG_INF("Found dev: %s (RSSI %d)", addr_str, rssi);

    // TODO: Only connect if FreeBot Control Service is provided

    // Stop scanning
    err = bt_le_scan_stop();
    if (err)
    {
        return;
    }

    // Create connection
    err = bt_conn_le_create(addr, BT_CONN_LE_CREATE_CONN, BT_LE_CONN_PARAM_DEFAULT, &ble_conn);
    if (err)
    {
        LOG_ERR("Create conn to %s failed (%d)", addr_str, err);
        return;
    }
}

/**
 * @brief Convenience function to start scanning for the target defined by `periph_addr`
 *
 * @retval 0 On success
 */
int scan_target()
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(&fb_ble_addr, addr, sizeof(addr));
    LOG_INF("BLE scanning for %s", addr);

    return bt_le_scan_start(BT_LE_SCAN_ACTIVE, on_device_found);
}

// -----------------------------------------------------------------------------

static uint8_t dis_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, struct bt_gatt_discover_params *params)
{
    int err;

    if (!attr)
    {
        LOG_DBG("%s", "No more attributes found");
        (void)memset(params, 0, sizeof(*params));

        // FIXME: verify that all characteristics are found

        // Unlock connection semaphore since GATT discovery is complete
        k_sem_give(&ble_conn_sem);
        LOG_DBG("Connection semaphore unlocked");

        // Stop GATT discovery
        return BT_GATT_ITER_STOP;
    }

    uint16_t attr_handle = bt_gatt_attr_value_handle(attr);

    // Check which service/characteristic was discovered
    if (bt_uuid_cmp(params->uuid, BT_UUID_FBCS) == 0)
    {
        LOG_DBG("Found robot control service @ 0x%04x", attr_handle);

        memcpy(params->uuid, BT_UUID_FBCS_DRV, sizeof(dis_uuid));
    }
    else if (bt_uuid_cmp(params->uuid, BT_UUID_FBCS_DRV) == 0)
    {
        LOG_DBG("Found drive characteristic @ 0x%04x", attr_handle);

        memcpy(params->uuid, BT_UUID_FBCS_RPM, sizeof(dis_uuid));
    }
    else if (bt_uuid_cmp(params->uuid, BT_UUID_FBCS_RPM) == 0)
    {
        LOG_DBG("Found voltage characteristic @ 0x%04x", attr_handle);

        memcpy(params->uuid, BT_UUID_FBCS_ANGLE, sizeof(dis_uuid));
    }
    else if (bt_uuid_cmp(params->uuid, BT_UUID_FBCS_ANGLE) == 0)
    {
        LOG_DBG("Found rpm characteristic @ 0x%04x", attr_handle);

        memcpy(params->uuid, BT_UUID_FBCS_V, sizeof(dis_uuid));
    }
    else if (bt_uuid_cmp(params->uuid, BT_UUID_FBCS_V) == 0)
    {
        LOG_DBG("Found angle characteristic @ 0x%04x", attr_handle);

        params->uuid = NULL;
    }
    else
    {
        LOG_WRN("Found unknown GATT attribute @ 0x%04x", attr_handle);

        params->uuid = NULL;
    }

    // Set params for next discovery
    params->type = BT_GATT_DISCOVER_CHARACTERISTIC;
    params->start_handle = attr_handle + 1;

    // Next discovery
    err = bt_gatt_discover(ble_conn, params);
    if (err != 0)
    {
        LOG_ERR("Discover failed (err %d)", err);
    }

    return BT_GATT_ITER_STOP;
}

static struct bt_gatt_discover_params dis_params = {
    .uuid = &dis_uuid.uuid,
    .func = dis_cb,
    .start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE,
    .end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
};

// -----------------------------------------------------------------------------

/** @brief On BLE connected callback */
static void on_connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err)
    {
        LOG_ERR("Failed to connect to %s (%u)", addr, err);

        bt_conn_unref(ble_conn);
        ble_conn = NULL;

        return;
    }

    if (conn != ble_conn)
    {
        return;
    }

    LOG_INF("Connected: %s", addr);

    // err = bt_conn_set_security(conn, BT_SECURITY_L2);
    // if (err)
    // {
    //     LOG_ERR("Failed to set security (%d)", err);
    // }

    // Try to discover FreeBot Control Service on peer
    dis_params.type = BT_GATT_DISCOVER_PRIMARY;
    err = bt_gatt_discover(ble_conn, &dis_params);
    if (err != 0)
    {
        LOG_ERR("Discover failed (err %d)", err);
        return;
    }
    LOG_DBG("GATT Service discovery started");
}

/** @brief On BLE disconnected callback */
static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (conn != ble_conn)
    {
        return;
    }

    // Make ble connection semaphore unavailable
    k_sem_reset(&ble_conn_sem);

    LOG_INF("Disconnected: %s (reason 0x%02x)", addr, reason);

    bt_conn_unref(ble_conn);
    ble_conn = NULL;

    if (scan_target())
    {
        LOG_ERR("BLE scanning failed to start");
        return;
    }
}

/** @brief On BLE security level change callback */
static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err)
    {
        LOG_ERR("Security failed: %s level %u err %d\n", addr, level, err);
    }
    LOG_INF("Security changed: %s level %u\n", addr, level);
}

/** @brief BLE connection callbacks */
static struct bt_conn_cb connection_cb = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .security_changed = security_changed,
};

// -----------------------------------------------------------------------------

/** @brief On BLE MTU update callback */
void mtu_updated(struct bt_conn *conn, uint16_t tx, uint16_t rx)
{
    LOG_INF("Updated MTU: TX: %d RX: %d bytes", tx, rx);
}

/** @brief BLE GATT callbacks */
static struct bt_gatt_cb gatt_cb = {
    .att_mtu_updated = mtu_updated,
};

// -----------------------------------------------------------------------------
// Creation of Zephyr threads
// -----------------------------------------------------------------------------

void t_ble_setup_ep(void *, void *, void *)
{
    int err;

    // Make ble conn semaphore unavailable
    k_sem_reset(&ble_conn_sem);

    // Build target's (FreeBot) BLE address
    err = bt_addr_le_from_str(FB_BLE_ADDR, "random", &fb_ble_addr);
    if (err)
    {
        LOG_ERR("Could not create target address (err %d)", err);
        return;
    }

    // Enable bluetooth & register connection callbacks
    err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth initialization failed (err %d)", err);
        return;
    }
    bt_conn_cb_register(&connection_cb);
    bt_gatt_cb_register(&gatt_cb);

    LOG_DBG("Bluetooth initialized");

    // Scan for target
    err = scan_target();
    if (err)
    {
        LOG_ERR("BLE scanning failed to start (err %d)", err);
        return;
    }
}

#define T_BLE_SETUP_STACKSIZE 1024
#define T_BLE_SETUP_PRIORITY -1 /* Should be smaller than main since it must execute first */
#define T_BLE_SETUP_OPTIONS 0
#define T_BLE_SETUP_DELAY 0
K_THREAD_DEFINE(ble_setup, T_BLE_SETUP_STACKSIZE, t_ble_setup_ep, NULL, NULL, NULL, T_BLE_SETUP_PRIORITY, T_BLE_SETUP_OPTIONS, T_BLE_SETUP_DELAY);

// -----------------------------------------------------------------------------
// Gateway's main()
// -----------------------------------------------------------------------------

int main(void)
{
    int err;
    static fbcs_drive_t cmd;

    if (k_sem_take(&ble_conn_sem, K_FOREVER))
    {
        LOG_WRN("Could not take connection semaphore (err %d)", err);
    }
    else
    {
        LOG_DBG("Command send: move forward");
        cmd = FBCS_MV_FORWARD;
        bt_gatt_write_without_response(ble_conn, 0x0012, &cmd, sizeof(cmd), false);
        k_sem_give(&ble_conn_sem);
    }

    k_sleep(K_MSEC(500));

    if (k_sem_take(&ble_conn_sem, K_FOREVER))
    {
        LOG_WRN("Could not take connection semaphore (err %d)", err);
    }
    else
    {
        cmd = FBCS_MV_BACKWARD;
        bt_gatt_write_without_response(ble_conn, 0x0012, &cmd, sizeof(cmd), false);
        LOG_DBG("Command send: move backward");
        k_sem_give(&ble_conn_sem);
    }

    k_sleep(K_MSEC(500));

    if (k_sem_take(&ble_conn_sem, K_FOREVER))
    {
        LOG_WRN("Could not take connection semaphore (err %d)", err);
    }
    else
    {
        cmd = FBCS_MV_LEFT;
        bt_gatt_write_without_response(ble_conn, 0x0012, &cmd, sizeof(cmd), false);
        LOG_DBG("Command send: move left");
        k_sem_give(&ble_conn_sem);
    }

    k_sleep(K_MSEC(500));

    if (k_sem_take(&ble_conn_sem, K_FOREVER))
    {
        LOG_WRN("Could not take connection semaphore (err %d)", err);
    }
    else
    {
        cmd = FBCS_MV_RIGHT;
        bt_gatt_write_without_response(ble_conn, 0x0012, &cmd, sizeof(cmd), false);
        LOG_DBG("Command send: move right");
        k_sem_give(&ble_conn_sem);
    }

    k_sleep(K_MSEC(500));

    if (k_sem_take(&ble_conn_sem, K_FOREVER))
    {
        LOG_WRN("Could not take connection semaphore (err %d)", err);
    }
    else
    {
        cmd = FBCS_ROT_CW;
        bt_gatt_write_without_response(ble_conn, 0x0012, &cmd, sizeof(cmd), false);
        LOG_DBG("Command send: rotate clockwise");
        k_sem_give(&ble_conn_sem);
    }

    k_sleep(K_MSEC(500));

    if (k_sem_take(&ble_conn_sem, K_FOREVER))
    {
        LOG_WRN("Could not take connection semaphore (err %d)", err);
    }
    else
    {
        cmd = FBCS_ROT_CCW;
        bt_gatt_write_without_response(ble_conn, 0x0012, &cmd, sizeof(cmd), false);
        LOG_DBG("Command send: rotate counterclockwise");
        k_sem_give(&ble_conn_sem);
    }

    k_sleep(K_MSEC(500));

    if (k_sem_take(&ble_conn_sem, K_FOREVER))
    {
        LOG_WRN("Could not take connection semaphore (err %d)", err);
    }
    else
    {
        cmd = FBCS_STOP;
        bt_gatt_write_without_response(ble_conn, 0x0012, &cmd, sizeof(cmd), false);
        LOG_DBG("Command send: stop");
        k_sem_give(&ble_conn_sem);
    }
    // Sleep forever: suspends thread
    k_sleep(K_FOREVER);
}
