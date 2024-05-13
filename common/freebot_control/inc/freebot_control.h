/**
 * @file freebot_control.h
 * @author Lowie Deferme <lowie.deferme@kuleuven.be>
 * @brief Definitions of a BLE GATT Service for FreeBot control
 * @version 0.1
 * @date 2024-05-13
 *
 * @copyright Copyright (c) 2024
 *
 */

#ifndef FB_CTRL_H
#define FB_CTRL_H

#include <stdint.h>

/* FreeBot Control Service */
#define BT_UUID_FBCS_VAL BT_UUID_128_ENCODE(0x00000030, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb)
#define BT_UUID_FBCS BT_UUID_DECLARE_128(BT_UUID_FBCS_VAL)

/* Drive Characteristic */
#define BT_UUID_FBCS_DRV_VAL BT_UUID_128_ENCODE(0x00000031, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb)
#define BT_UUID_FBCS_DRV BT_UUID_DECLARE_128(BT_UUID_FBCS_DRV_VAL)

/* Motor RPM Characteristic */
#define BT_UUID_FBCS_RPM_VAL BT_UUID_128_ENCODE(0x00000032, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb)
#define BT_UUID_FBCS_RPM BT_UUID_DECLARE_128(BT_UUID_FBCS_RPM_VAL)

/* Motor Angle Characteristic */
#define BT_UUID_FBCS_ANGLE_VAL BT_UUID_128_ENCODE(0x00000033, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb)
#define BT_UUID_FBCS_ANGLE BT_UUID_DECLARE_128(BT_UUID_FBCS_ANGLE_VAL)

/* Voltage Characteristic */
#define BT_UUID_FBCS_V_VAL BT_UUID_128_ENCODE(0x00000034, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb)
#define BT_UUID_FBCS_V BT_UUID_DECLARE_128(BT_UUID_FBCS_V_VAL)

/* Encoding for drive cmd */
#define FBCS_STOP        0b0000
#define FBCS_MV_FORWARD  0b0010
#define FBCS_MV_BACKWARD 0b0011
#define FBCS_MV_RIGHT    0b0100
#define FBCS_MV_LEFT     0b0101
#define FBCS_ROT_CW      0b0110
#define FBCS_ROT_CCW     0b0111

/** @brief Drive cmd format */
typedef uint8_t fbcs_drive_t;

/** @brief Angle data format */
typedef struct
{
    int32_t M1;
    int32_t M2;
    int32_t M3;
    int32_t M4;
} fbcs_angle_t;

/** @brief RPM data format */
typedef struct
{
    int32_t M1;
    int32_t M2;
    int32_t M3;
    int32_t M4;
} fbcs_rpm_t;

/** @brief Voltage data format */
typedef uint16_t fbcs_v_t;

#endif /* FB_CTRL_H */
