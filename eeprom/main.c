#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <string.h>

// NB: Assumes 32-byte page, 16-bit addressed EEPROM like AT24C64D.

/////////////// Data to be written to EEPROM /////////////////
const char magic[10] = "open-ephys";
const uint8_t layout_version[2] = {1, 0};
const char module_name[32] = "Omnetics 32ch module";
const char pcb_rev = 'B';

typedef struct {
    const char name[32];
    uint8_t num_chan;
    const uint8_t *channel_map;
} map_config_t;


const uint8_t channel_map_32m[32] = // Taken from PCB schematic
   {47, 46, 45, 44, 43, 42, 41, 40, 72, 73, 74, 75, 76, 77, 78, 79,
    80, 81, 82, 83, 84, 85, 86, 87, 55, 54, 53, 52, 51, 50, 49, 48};
    
const uint8_t channel_map_16b[32] = // first positive channels then negative
  {47, 45, 43, 41, 72, 74, 76, 78, 80, 82, 84, 86, 55, 53, 51, 49, 
   46, 44, 42, 40, 73, 75, 77, 79, 81, 83, 85, 87, 54, 52, 50, 48};

const map_config_t maps[] = {
    {"32ch monopolar", 32, channel_map_32m},
    {"16ch bipolar", 32, channel_map_16b}
};

const uint8_t num_maps = sizeof(maps) / sizeof(maps[0]);
//////////////////////////////////////////////////////////////

#define EEPROM_WP_PIN 5
#define I2C_PORT i2c1
#define I2C_SDA_PIN 2
#define I2C_SCL_PIN 3
#define I2C_FREQUENCY 100000
#define EEPROM_ADDR 0x50
#define EEPROM_PAGE_SIZE 32
#define EEPROM_WRITE_DELAY 5

#define MAX_ALLOWABLE_CHAN 128

#define EEPROM_MAGIC_OFFSET 0x0000
#define EEPROM_LAYOUT_VER_OFFSET 0x000A
#define EEPROM_NAME_OFFSET 0x000C
#define EEPROM_PCB_REV_OFFSET 0x002C
#define EEPROM_NUM_MAPS_OFFSET 0x002D
#define EEPROM_MAP_BASE_OFFSET 0x0400
#define EEPROM_MAP_SIZE 0x0400

bool write_eeprom_byte(uint16_t memory_address, const uint8_t data)
{
    uint8_t buffer[3];
    buffer[0] = (memory_address >> 8) & 0xFF;
    buffer[1] = memory_address & 0xFF;
    buffer[2] = data;

    int bytes_written = i2c_write_blocking(I2C_PORT, EEPROM_ADDR, buffer, 3, false);
    if (bytes_written != 3) {
        return false;
    }

    sleep_ms(EEPROM_WRITE_DELAY);
    return true;
}

bool write_eeprom_data(uint16_t start_address, const uint8_t *data, size_t len)
{
    printf("Writing %d bytes starting at address 0x%04X...\n", len, start_address);

     for (int i = 0; i < len; i++) {
        if (!write_eeprom_byte(start_address + i, data[i])) {
            printf("Failed to write at address 0x%04X\n", start_address + i);
            return false;
        }
    }

    printf("  Wrote %d bytes at address 0x%04X\n", len, start_address);

    return true;
}

bool read_eeprom(uint16_t memory_address, uint8_t *buffer, size_t len)
{
    uint8_t addr_buf[2] = {(memory_address >> 8) & 0xFF, memory_address & 0xFF};

    if (i2c_write_blocking(I2C_PORT, EEPROM_ADDR, addr_buf, 2, true) != 2) {
        return false;
    }

    if (i2c_read_blocking(I2C_PORT, EEPROM_ADDR, buffer, len, false) != (int)len) {
        return false;
    }

    return true;
}

int main()
{
    stdio_init_all();
    sleep_ms(1000);

    gpio_init(EEPROM_WP_PIN);
    gpio_set_dir(EEPROM_WP_PIN, GPIO_OUT);
    gpio_put(EEPROM_WP_PIN, 0);

    i2c_init(I2C_PORT, I2C_FREQUENCY);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    printf("Programming EEPROM with new layout...\n");
    printf("\nWriting header data...\n");

    if (!write_eeprom_data(EEPROM_MAGIC_OFFSET, (const uint8_t*)magic, sizeof(magic))) {
        printf("Failed to write magic string\n");
        return -1;
    }

    if (!write_eeprom_data(EEPROM_LAYOUT_VER_OFFSET, layout_version, sizeof(layout_version))) {
        printf("Failed to write layout version\n");
        return -1;
    }

    if (!write_eeprom_data(EEPROM_NAME_OFFSET, (const uint8_t*)module_name, sizeof(module_name))) {
        printf("Failed to write module name\n");
        return -1;
    }

    if (!write_eeprom_data(EEPROM_PCB_REV_OFFSET, (const uint8_t*)&pcb_rev, 1)) {
        printf("Failed to write PCB revision\n");
        return -1;
    }

    if (!write_eeprom_data(EEPROM_NUM_MAPS_OFFSET, &num_maps, 1)) {
        printf("Failed to write number of maps\n");
        return -1;
    }

    for (int i = 0; i < num_maps; i++) {
        printf("\nWriting Map %d...\n", i);
        uint16_t map_base = EEPROM_MAP_BASE_OFFSET + (i * EEPROM_MAP_SIZE);

        if (maps[i].num_chan > MAX_ALLOWABLE_CHAN)
        {
            printf("Failed to write map %d channel count. %d channels were requested, but the maximum number is %d\n",
                i, maps[i].num_chan, MAX_ALLOWABLE_CHAN);
            return -1;
        }

        if (!write_eeprom_data(map_base + 0x0000, &maps[i].num_chan, 1)) {
            printf("Failed to write map %d channel count\n", i);
            return -1;
        }

        if (!write_eeprom_data(map_base + 0x0001, (const uint8_t*)maps[i].name, sizeof(maps[i].name))) {
            printf("Failed to write map %d name\n", i);
            return -1;
        }

        if (!write_eeprom_data(map_base + 0x0021, maps[i].channel_map, maps[i].num_chan)) {
            printf("Failed to write map %d channel map\n", i);
            return -1;
        }
    }

    printf("\nWrite complete. Performing verification...\n");
    printf("Verifying header data...\n");

    char read_magic[10];
    if (!read_eeprom(EEPROM_MAGIC_OFFSET, (uint8_t*)read_magic, sizeof(magic))) {
        printf("Failed to read back magic string\n");
        return -1;
    }
    if (memcmp(magic, read_magic, sizeof(magic)) != 0) {
        printf("Magic string verification failed\n");
        return -1;
    }

    uint8_t read_version[2];
    if (!read_eeprom(EEPROM_LAYOUT_VER_OFFSET, read_version, 2)) {
        printf("Failed to read back layout version\n");
        return -1;
    }
    if (read_version[0] != 1 || read_version[1] != 0) {
        printf("Layout version verification failed: got %d.%d\n", read_version[0], read_version[1]);
        return -1;
    }

    uint8_t read_num_maps;
    if (!read_eeprom(EEPROM_NUM_MAPS_OFFSET, &read_num_maps, 1)) {
        printf("Failed to read back number of maps\n");
        return -1;
    }

    if (read_num_maps != num_maps) {
        printf("Number of maps verification failed: expected %d, got %d\n", num_maps, read_num_maps);
        return -1;
    }

    printf("Header verification successful!\n");

    for (int i = 0; i < num_maps; i++) {
        printf("Verifying Map %d...\n", i);
        uint16_t map_base = EEPROM_MAP_BASE_OFFSET + (i * EEPROM_MAP_SIZE);

        uint8_t num_chan;
        if (!read_eeprom(map_base + 0x0000, &num_chan, 1)) {
            printf("Failed to read back number of channels in map %d\n", i);
            return -1;
        }

        if (maps[i].num_chan != num_chan) {
            printf("Map %d name verification failed: expected %d channels, got '%d'\n",
                   i, maps[i].num_chan, num_chan);
            return -1;
        }

        char read_map_name[32];
        if (!read_eeprom(map_base + 0x0001, (uint8_t*)read_map_name, 32)) {
            printf("Failed to read back map %d name\n", i);
            return -1;
        }

        if (strcmp(maps[i].name, read_map_name) != 0) {
            printf("Map %d name verification failed: expected '%s', got '%s'\n",
                   i, maps[i].name, read_map_name);
            return -1;
        }

        uint8_t chan_map[MAX_ALLOWABLE_CHAN];

        if (!read_eeprom(map_base + 0x0021, chan_map, maps[i].num_chan)) {
            printf("Failed to read back channel map %d\n", i);
            return -1;
        }

        for (int j = 0; j < maps[i].num_chan; j++)
        {
            if (chan_map[j] != maps[i].channel_map[j])
            {
                printf("Channel map %d verification failed: expected '%d' at map index %d, got '%d'\n",
                   i, maps[i].channel_map[j], j, chan_map[j]);

                return -1;
            }
        }

        printf("Map %d verification successful!\n", i);
    }

    printf("\nAll verification successful! EEPROM programming complete.\n");

    while (true) {
        sleep_ms(1000);
    }

    return 0;
}