#pragma once
// Seeed Studio XIAO ESP32-S3 Sense — OV2640 camera pin map.
// (Matches Seeed's published camera_pins for the XIAO S3 Sense expansion board.)

#define PWDN_GPIO_NUM   -1
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM   10
#define SIOD_GPIO_NUM   40
#define SIOC_GPIO_NUM   39
#define Y9_GPIO_NUM     48
#define Y8_GPIO_NUM     11
#define Y7_GPIO_NUM     12
#define Y6_GPIO_NUM     14
#define Y5_GPIO_NUM     16
#define Y4_GPIO_NUM     18
#define Y3_GPIO_NUM     17
#define Y2_GPIO_NUM     15
#define VSYNC_GPIO_NUM  38
#define HREF_GPIO_NUM   47
#define PCLK_GPIO_NUM   13

// On-board microSD (SD_MMC 1-bit) clock/cmd/data for the XIAO S3 Sense.
#define HEARTH_SD_CLK   7
#define HEARTH_SD_CMD   9
#define HEARTH_SD_D0    8
