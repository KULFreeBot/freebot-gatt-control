cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(fb_ble_ctrl)

include_directories(lib/freebot/inc)

if(DEFINED FB_DUMMY_API)
    target_sources(app PRIVATE lib/freebot/src/fb_dummy.c)
else()
    target_sources(app PRIVATE  lib/freebot/src/fb_motor.c lib/freebot/src/fb_io.c lib/freebot/src/fb_pwr.c)
endif()

target_sources(app PRIVATE src/main.c)
