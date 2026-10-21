#pragma once

#include <Arduino_GFX_Library.h>

extern Arduino_GFX *gfx;

bool displayBegin();
void displaySetBacklight(bool on);
void displayPresent(); // swap backbuffer → screen (call once per frame)
uint16_t *displayBackBuffer();
