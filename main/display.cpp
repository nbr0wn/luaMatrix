#include "esp_log.h"
#include "hub75.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* TAG = "display";

Hub75Driver *driver;

extern "C" void display_init() {
    // Configure for your panel
    Hub75Config config{};

    config.panel_width = 64;
    config.panel_height = 64;
    config.layout_rows = 1;
    config.layout_cols = 3;
    config.rotation = Hub75Rotation::ROTATE_0;
    config.layout = Hub75PanelLayout::HORIZONTAL;

    config.scan_wiring = Hub75ScanWiring::STANDARD_TWO_SCAN;  // Most panels
    //config.scan_wiring = Hub75ScanWiring::SCAN_1_8_64PX_HIGH;  // 1/8 scan, 64-pixel high panels
    config.shift_driver = Hub75ShiftDriver::GENERIC;
    //config.shift_driver = Hub75ShiftDriver::FM6126A;
    config.clk_phase_inverted = false;  // Invert clock phase (default: false, needed for MBI5124)
    config.output_clock_speed = Hub75ClockSpeed::HZ_20M;
    config.min_refresh_rate = 24;
    config.latch_blanking = 1;
    config.double_buffer = false;

    config.brightness = 128;

    // Set GPIO pins
    config.pins.r1 = 25;
    config.pins.g1 = 26;
    config.pins.b1 = 27;
    config.pins.r2 = 14;
    config.pins.g2 = 12;
    config.pins.b2 = 13;
    config.pins.a = 23;
    config.pins.b = 19;
    config.pins.c = 5;
    config.pins.d = 17;
    config.pins.e = 18;
    config.pins.lat = 4;
    config.pins.oe = 15;
    config.pins.clk = 16;

    // Create and start driver
    driver = new Hub75Driver(config);

    driver->clear();

    driver->begin();  // Starts continuous refresh
}

extern "C" void clear_display() {
    driver->clear();
}

extern "C" void set_pixel(int x, int y, int r, int g, int b) {
    driver->set_pixel(x, y, r, g, b);
}

extern "C" void vert_line(int x, int y, int len, int r, int g, int b) {
    driver->fill(x,y,1,len,r,g,b);
}

extern "C" void horiz_line(int x, int y, int len, int r, int g, int b) {
    driver->fill(x,y,len,1,r,g,b);
}

extern "C" void fill_rect(int x, int y, int w, int h, int r, int g, int b) {
    driver->fill(x, y, w, h, r, g, b);
}

extern "C" void set_brightness(int b) {
    driver->set_brightness(b);
}

extern "C" int get_width(void) {
    return driver->get_width();
}

extern "C" int get_height(void) {
    return driver->get_height();
}