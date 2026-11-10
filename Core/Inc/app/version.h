/**
 * @file    version.h
 * @brief   Firmware identity.
 *
 * FW_GIT_HASH is injected by the Makefile (-DFW_GIT_HASH=\"abc1234\").
 * STM32CubeIDE builds don't define it, so they report "ide".
 */
#ifndef APP_VERSION_H
#define APP_VERSION_H

#define FW_NAME          "IRSense"
#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0
#define FW_VERSION       "1.0.0"
#define FW_BOARD         "NUCLEO-F401RE"

#ifndef FW_GIT_HASH
#define FW_GIT_HASH "ide"
#endif

#endif /* APP_VERSION_H */
