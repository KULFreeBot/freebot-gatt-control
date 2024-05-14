/**
 * @file main.c
 * @author Lowie Deferme <lowie.deferme@kuleuven.be>
 * @brief FreeBot control over BLE GATT service
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

#include "freebot.h"
#include "freebot_control.h"

LOG_MODULE_REGISTER(fb_ble_ctrl, LOG_LEVEL_INF);

// -----------------------------------------------------------------------------
// Structures & methods for inter-thread communication/syncronization
// -----------------------------------------------------------------------------

K_MUTEX_DEFINE(status_mutex);

enum status_e
{
    BLE_ADVERTISING,
    BLE_CONNECTED,
    BLE_ERROR,
    FB_ERROR,
} status_v;

void update_status(enum status_e s)
{
    k_mutex_lock(&status_mutex, K_FOREVER);
    status_v = s;
    k_mutex_unlock(&status_mutex);
}

// -----------------------------------------------------------------------------
// Bluetooth LE settings & callbacks
// -----------------------------------------------------------------------------

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

/** @brief BLE Advertisement data */
static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

/** @brief BLE Scan response data */
static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_SOME, BT_UUID_FBCS_VAL),
};

/** @brief BLE Preferred radio mode */
static const struct bt_conn_le_phy_param phy = {
    .options = BT_CONN_LE_PHY_OPT_NONE,
    .pref_rx_phy = BT_GAP_LE_PHY_1M,
    .pref_tx_phy = BT_GAP_LE_PHY_1M,
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
        return;
    }

    LOG_INF("Connected: %s", addr);

    update_status(BLE_CONNECTED);

    err = bt_conn_le_phy_update(conn, &phy);
    if (err)
    {
        LOG_ERR("Could not update PHY (0x%02x)", err);
    }
}

/** @brief On BLE disconnected callback */
static void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    fb_stop();
    update_status(BLE_ADVERTISING);

    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected: %s (reason 0x%02x)", addr, reason);

    int err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_ERR("BLE advertising failed to start (err %d)", err);
        update_status(BLE_ERROR);
        return;
    }
}

/** @brief On BLE param update callback */
void on_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout)
{
    LOG_DBG("New param: interval %d, latency %d, timeout %d", interval, latency, timeout);
}

/** @brief On BLE radio mode update */
void on_phy_updated(struct bt_conn *conn, struct bt_conn_le_phy_info *param)
{
    // PHY Updated
    switch (param->tx_phy)
    {
    case BT_CONN_LE_TX_POWER_PHY_1M:
        LOG_DBG("New PHY: 1M");
        break;
    case BT_CONN_LE_TX_POWER_PHY_2M:
        LOG_DBG("New PHY: 2M");
        break;
    case BT_CONN_LE_TX_POWER_PHY_CODED_S8:
        LOG_DBG("New PHY: Long Range (S8)");
        break;
    case BT_CONN_LE_TX_POWER_PHY_CODED_S2:
        LOG_DBG("New PHY: Long Range (S2)");
        break;

    default:
        LOG_ERR("PHY updated to unknown mode: 0x%02x", param->tx_phy);
        break;
    }
}

/** @brief BLE connection callbacks */
struct bt_conn_cb connection_cb = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .le_param_updated = on_param_updated,
    .le_phy_updated = on_phy_updated,
};

// -----------------------------------------------------------------------------
// Implementation of FreeBot Control Service (BLE GATT layer)
// -----------------------------------------------------------------------------

static fbcs_rpm_t fbcs_rpm;
static fbcs_angle_t fbcs_angle;
static fbcs_v_t fbcs_v;

static ssize_t fbcs_drive(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    LOG_DBG("handle: %u, conn: %p", attr->handle, (void *)conn);

    if (len != sizeof(fbcs_drive_t))
    {
        LOG_WRN("Incorrect data length for drive attribute");
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    if (offset != 0)
    {
        LOG_WRN("Incorrect data offset for drive attribute");
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    fbcs_drive_t drive_cmd = *((fbcs_drive_t *)buf);
    switch (drive_cmd)
    {
    case FBCS_STOP:
        fb_stop();
        break;
    case FBCS_MV_FORWARD:
        fb_straight_forw();
        break;
    case FBCS_MV_BACKWARD:
        fb_straight_back();
        break;
    case FBCS_MV_RIGHT:
        fb_side_right();
        break;
    case FBCS_MV_LEFT:
        fb_side_left();
        break;
    case FBCS_ROT_CW:
        fb_rotate_cw();
        break;
    case FBCS_ROT_CCW:
        fb_rotate_ccw();
        break;

    default:
        LOG_WRN("Received unknown drive cmd");
        fb_stop();
        break;
    }

    return len;
}

static ssize_t fbcs_rpm_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    LOG_DBG("handle: %u, conn: %p", attr->handle, (void *)conn);

    // Read motor speeds
    fb_motor_speed_t speed;
    fb_get_motor_speed(&speed);

    // Populate data struct
    fbcs_rpm.M1 = (int32_t)speed.front_left;
    fbcs_rpm.M2 = (int32_t)speed.front_right;
    fbcs_rpm.M3 = (int32_t)speed.back_left;
    fbcs_rpm.M4 = (int32_t)speed.back_right;

    return bt_gatt_attr_read(conn, attr, buf, len, offset, &fbcs_rpm, sizeof(fbcs_rpm));
}

static ssize_t fbcs_angle_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    LOG_DBG("handle: %u, conn: %p", attr->handle, (void *)conn);

    // Read motor angles
    fb_motor_angle_t angle;
    fb_get_motor_angle(&angle);

    // Populate data struct
    fbcs_angle.M1 = (int32_t)angle.front_left;
    fbcs_angle.M2 = (int32_t)angle.front_right;
    fbcs_angle.M3 = (int32_t)angle.back_left;
    fbcs_angle.M4 = (int32_t)angle.back_right;

    return bt_gatt_attr_read(conn, attr, buf, len, offset, &fbcs_angle, sizeof(fbcs_angle));
}

static ssize_t fbcs_v_read(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    LOG_DBG("handle: %u, conn: %p", attr->handle, (void *)conn);

    fbcs_v = (fbcs_v_t)fb_v_measure();

    return bt_gatt_attr_read(conn, attr, buf, len, offset, &fbcs_v, sizeof(fbcs_v));
}

// -----------------------------------------------------------------------------

BT_GATT_SERVICE_DEFINE(
    // FreeBot Control Service
    fbcs_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_FBCS),
    // FreeBot Drive characteristic
    BT_GATT_CHARACTERISTIC(BT_UUID_FBCS_DRV,
                           BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE,
                           NULL, fbcs_drive, NULL),
    // FreeBot Motor RPM characteristic
    BT_GATT_CHARACTERISTIC(BT_UUID_FBCS_RPM,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           fbcs_rpm_read, NULL, NULL),
    // FreeBot Motor Angle characteristic
    BT_GATT_CHARACTERISTIC(BT_UUID_FBCS_ANGLE,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           fbcs_angle_read, NULL, NULL),
    // FreeBot Voltage characteristic
    BT_GATT_CHARACTERISTIC(BT_UUID_FBCS_V,
                           BT_GATT_CHRC_READ,
                           BT_GATT_PERM_READ,
                           fbcs_v_read, NULL, NULL)
    // TODO: Notify when V drops below setvalue
);

// -----------------------------------------------------------------------------
// Entry points for Zephyr threads
// -----------------------------------------------------------------------------

void t_startup_ep(void *, void *, void *)
{
    int err = 0;

    update_status(BLE_ADVERTISING);

    err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)", err);
        update_status(BLE_ERROR);
        return;
    }
    bt_conn_cb_register(&connection_cb);

    LOG_DBG("Bluetooth initialized");

    err = fb_init();
    err |= fb_v_measure_select(V_CAP);
    if (err)
    {
        LOG_ERR("FreeBot init failed (err %d)", err);
        update_status(FB_ERROR);
        return;
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_ERR("BLE advertising failed to start (err %d)", err);
        update_status(BLE_ERROR);
        return;
    }

    LOG_DBG("BLE advertising started");

    return;
}

/** @brief Thread for controlling status LEDs */
void t_status_led_ep(void *, void *, void *)
{
    uint8_t D15_pattern;
    uint8_t D16_pattern;
    uint8_t index = 0;
    for (;;)
    {
        // Determine led pattern based on status
        k_mutex_lock(&status_mutex, K_FOREVER);
        switch (status_v)
        {
        case BLE_ADVERTISING:
            D15_pattern = 0b10101010;
            D16_pattern = 0b10101010;
            break;
        case BLE_CONNECTED:
            D15_pattern = 0b10101010;
            D16_pattern = 0b01010101;
            break;
        case BLE_ERROR:
            D15_pattern = 0b11110000;
            D16_pattern = 0b00001111;
            break;
        case FB_ERROR:
            D15_pattern = 0b11110000;
            D16_pattern = 0b11110000;
            break;
        default:
            D15_pattern = 0b11110000;
            D16_pattern = 0b00111100;
            break;
        }
        k_mutex_unlock(&status_mutex);

        // Set LEDs according to pattern
        if ((D15_pattern >> index) & 0b1)
        {
            fb_set_led(D15);
        }
        else
        {
            fb_clear_led(D15);
        }

        if ((D16_pattern >> index) & 0b1)
        {
            fb_set_led(D16);
        }
        else
        {
            fb_clear_led(D16);
        }

        // Update pattern index and go to sleep
        index = (index + 1) % 8;
        k_sleep(K_MSEC(100));
    }
}

// -----------------------------------------------------------------------------
// Creation of Zephyr threads
// -----------------------------------------------------------------------------

#define T_STARTUP_STACKSIZE 1024
#define T_STARTUP_PRIORITY 1
#define T_STARTUP_OPTIONS 0
#define T_STARTUP_DELAY 0
K_THREAD_DEFINE(startup, T_STARTUP_STACKSIZE, t_startup_ep, NULL, NULL, NULL, T_STARTUP_PRIORITY, T_STARTUP_OPTIONS, T_STARTUP_DELAY);

#define T_STATUS_LED_STACKSIZE 256
#define T_STATUS_LED_PRIORITY 10
#define T_STATUS_LED_OPTIONS 0
#define T_STATUS_LED_DELAY 0
K_THREAD_DEFINE(status_led, T_STATUS_LED_STACKSIZE, t_status_led_ep, NULL, NULL, NULL, T_STATUS_LED_PRIORITY, T_STATUS_LED_OPTIONS, T_STATUS_LED_DELAY);
