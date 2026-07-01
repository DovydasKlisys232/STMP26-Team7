/*
Date: 30/6/25
Purpose: This is the header file that contains all camera pin connections to the ESP module
*/

#ifndef CAMERA_PINS_H
#define CAMERA_PINS_H

// Power / Control
#define PWDN_GPIO_NUM    16
#define RESET_GPIO_NUM   15

// Clock Signals
#define XCLK_GPIO_NUM    7
#define PCLK_GPIO_NUM    17

// SCCB
#define SIOD_GPIO_NUM    42
#define SIOC_GPIO_NUM    41

// Data Bus
#define Y2_GPIO_NUM      5
#define Y3_GPIO_NUM      6
#define Y4_GPIO_NUM      18
#define Y5_GPIO_NUM      8
#define Y6_GPIO_NUM      9
#define Y7_GPIO_NUM      10
#define Y8_GPIO_NUM      11
#define Y9_GPIO_NUM      12

// Synchronization
#define VSYNC_GPIO_NUM   13
#define HREF_GPIO_NUM    14

#endif