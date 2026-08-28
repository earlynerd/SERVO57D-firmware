#include "mks57d/ssd1306.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum
{
    SSD1306_COMMAND_CONTROL = 0x00u,
    SSD1306_DATA_CONTROL = 0x40u,
    SSD1306_RAM_COLUMNS = 128u,
    SSD1306_RAM_PAGES = 8u,
    /* One control byte plus 31 data bytes fits the I2C1 32-byte bound. */
    SSD1306_DATA_CHUNK_BYTES = 31u
};

const ssd1306_panel_config_t SSD1306_PANEL_SERVO57D = {
    .address_7bit = 0x3Cu,
    .width = 72u,
    .height = 40u,
    .column_offset = 28u,
    .page_offset = 0u,
    .clock_divide = 0xF0u,
    .multiplex_ratio = 0x27u,
    .display_offset = 0u,
    .start_line = 0u,
    .com_pins = 0x12u,
    .contrast = 0x2Fu,
    .precharge = 0x22u,
    .vcomh = 0x20u,
    .segment_remap = true,
    .com_scan_remap = true,
    .use_internal_iref = true,
};

static bool bus_is_valid(const i2c_bus_t* bus)
{
    return (bus != NULL) && (bus->write != NULL);
}

bool ssd1306_config_is_valid(const ssd1306_panel_config_t* config)
{
    uint16_t last_column;
    uint16_t page_count;
    uint16_t last_page;

    if ((config == NULL) ||
        (config->address_7bit > 0x7Fu) ||
        (config->width == 0u) ||
        (config->height == 0u) ||
        ((config->height % 8u) != 0u) ||
        (config->multiplex_ratio >= 64u) ||
        (((uint16_t)config->multiplex_ratio + 1u) < config->height) ||
        (config->display_offset >= 64u) ||
        (config->start_line >= 64u))
    {
        return false;
    }

    last_column = (uint16_t)config->column_offset + config->width;
    page_count = (uint16_t)config->height / 8u;
    last_page = (uint16_t)config->page_offset + page_count;

    return (last_column <= SSD1306_RAM_COLUMNS) &&
           (last_page <= SSD1306_RAM_PAGES);
}

i2c_status_t ssd1306_initialize(const i2c_bus_t* bus,
                                const ssd1306_panel_config_t* config)
{
    if (!bus_is_valid(bus) || !ssd1306_config_is_valid(config))
    {
        return I2C_STATUS_INVALID_ARGUMENT;
    }

    const uint8_t sequence[] = {
        SSD1306_COMMAND_CONTROL,
        0xAEu,
        0xD5u, config->clock_divide,
        0xA8u, config->multiplex_ratio,
        0xD3u, config->display_offset,
        0x20u, 0x00u,
        0xADu, config->use_internal_iref ? 0x30u : 0x20u,
        0x8Du, 0x14u,
        (uint8_t)(0x40u | config->start_line),
        0xA6u,
        0xA4u,
        config->segment_remap ? 0xA1u : 0xA0u,
        config->com_scan_remap ? 0xC8u : 0xC0u,
        0xDAu, config->com_pins,
        0x81u, config->contrast,
        0xD9u, config->precharge,
        0xDBu, config->vcomh,
        0xAFu,
    };

    return bus->write(bus->context,
                      config->address_7bit,
                      sequence,
                      sizeof(sequence));
}

static i2c_status_t write_pages(const i2c_bus_t* bus,
                                const ssd1306_panel_config_t* config,
                                uint8_t first_page,
                                uint8_t page_count,
                                const uint8_t* pixels,
                                size_t length)
{
    uint8_t packet[SSD1306_DATA_CHUNK_BYTES + 1u];
    size_t offset = 0u;
    i2c_status_t result;
    i2c_status_t first_error = I2C_STATUS_OK;

    const uint8_t window[] = {
        SSD1306_COMMAND_CONTROL,
        0x21u,
        config->column_offset,
        (uint8_t)(config->column_offset + config->width - 1u),
        0x22u,
        (uint8_t)(config->page_offset + first_page),
        (uint8_t)(config->page_offset + first_page + page_count - 1u),
    };

    result = bus->write(bus->context,
                        config->address_7bit,
                        window,
                        sizeof(window));
    if (result != I2C_STATUS_OK)
    {
        first_error = result;
    }

    packet[0] = SSD1306_DATA_CONTROL;
    while (offset < length)
    {
        size_t chunk = length - offset;
        if (chunk > SSD1306_DATA_CHUNK_BYTES)
        {
            chunk = SSD1306_DATA_CHUNK_BYTES;
        }

        memcpy(&packet[1], &pixels[offset], chunk);
        result = bus->write(bus->context,
                            config->address_7bit,
                            packet,
                            chunk + 1u);
        if ((result != I2C_STATUS_OK) &&
            (first_error == I2C_STATUS_OK))
        {
            first_error = result;
        }
        offset += chunk;
    }

    return first_error;
}

i2c_status_t ssd1306_write_pages(const i2c_bus_t* bus,
                                 const ssd1306_panel_config_t* config,
                                 uint8_t first_page,
                                 uint8_t page_count,
                                 const uint8_t* pixels,
                                 size_t length)
{
    const uint8_t visible_pages =
        (config == NULL) ? 0u : (uint8_t)(config->height / 8u);
    const size_t expected_length =
        (config == NULL) ? 0u : (size_t)config->width * page_count;

    if (!bus_is_valid(bus) || !ssd1306_config_is_valid(config) ||
        (pixels == NULL) || (page_count == 0u) ||
        (first_page >= visible_pages) ||
        ((uint16_t)first_page + page_count > visible_pages) ||
        (length != expected_length))
    {
        return I2C_STATUS_INVALID_ARGUMENT;
    }

    return write_pages(bus,
                       config,
                       first_page,
                       page_count,
                       pixels,
                       length);
}

i2c_status_t ssd1306_write_frame(const i2c_bus_t* bus,
                                 const ssd1306_panel_config_t* config,
                                 const uint8_t* pixels,
                                 size_t length)
{
    const uint8_t page_count =
        (config == NULL) ? 0u : (uint8_t)(config->height / 8u);

    return ssd1306_write_pages(bus,
                               config,
                               0u,
                               page_count,
                               pixels,
                               length);
}
