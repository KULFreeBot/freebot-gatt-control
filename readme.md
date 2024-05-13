# FreeBot BLE control

## FreeBot

> **NOTE:** The robot is setup such that it stops driving on connection loss and starts advertising again.

In this example the robot behaves as a Bluetooth Low Energy peripheral hosting a GATT server with a custom "FreeBot Control Service".
This service has the following characteristics:

| Characteristic                |                 UUID                 |
|-------------------------------|:------------------------------------:|
| Send drive command to FreeBot |`00000031-0000-1000-8000-00805f9b34fb`|
| Read motor's RPM              |`00000032-0000-1000-8000-00805f9b34fb`|
| Read motor's angles           |`00000033-0000-1000-8000-00805f9b34fb`|
| Read capacitor voltage        |`00000034-0000-1000-8000-00805f9b34fb`|

## Controller

TODO
