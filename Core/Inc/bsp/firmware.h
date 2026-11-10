/**
 * @file    firmware.h
 * @brief   Glue between the board (bsp) and the portable application (app).
 *
 * main() calls fw_init() once after the CubeMX init and fw_poll() forever.
 */
#ifndef BSP_FIRMWARE_H
#define BSP_FIRMWARE_H

#ifdef __cplusplus
extern "C" {
#endif

void fw_init(void);
void fw_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_FIRMWARE_H */
