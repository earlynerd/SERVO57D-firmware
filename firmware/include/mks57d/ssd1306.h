#ifndef MKS57D_SSD1306_H
#define MKS57D_SSD1306_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mks57d/i2c_bus.h"

typedef struct
{
    uint8_t address_7bit;
    uint8_t width;
    uint8_t height;
    uint8_t column_offset;
    uint8_t page_offset;
    uint8_t clock_divide;
    uint8_t multiplex_ratio;
    uint8_t display_offset;
    uint8_t start_line;
    uint8_t com_pins;
    uint8_t contrast;
    uint8_t precharge;
    uint8_t vcomh;
    bool segment_remap;
    bool com_scan_remap;
    bool use_internal_iref;
} ssd1306_panel_config_t;

/* Bench-proven profile for the fitted 72-by-40 SSD1306-compatible panel. */
extern const ssd1306_panel_config_t SSD1306_PANEL_SERVO57D_CANDIDATE;

bool ssd1306_config_is_valid(const ssd1306_panel_config_t* config);
i2c_status_t ssd1306_initialize(const i2c_bus_t* bus,
                                const ssd1306_panel_config_t* config);
i2c_status_t ssd1306_write_frame(const i2c_bus_t* bus,
                                 const ssd1306_panel_config_t* config,
                                 const uint8_t* pixels,
                                 size_t length);
i2c_status_t ssd1306_write_pages(const i2c_bus_t* bus,
                                 const ssd1306_panel_config_t* config,
                                 uint8_t first_page,
                                 uint8_t page_count,
                                 const uint8_t* pixels,
                                 size_t length);

#endif
