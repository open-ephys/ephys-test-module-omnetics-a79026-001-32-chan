#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <string.h>

// NB: Assumes 32-byte page, 16-bit addressed EEPROM like AT24C64D.

/////////////// Data to be written to EEPROM /////////////////
const char magic[10] = "open-ephys";
const char name[20] = "Omnetics 32 Ch.     "; // pad out to 20 char
const char pcb_rev = 'A';
const uint8_t num_chan = 32;
const uint8_t channel_map[32] = // Taken from PCB schematic
   {47, 46, 45, 44, 43, 42, 41, 40, 72, 73, 74, 75, 76, 77, 78, 79,
    80, 81, 82, 83, 84, 85, 86, 87, 55, 54, 53, 52, 51, 50, 49, 48};
//////////////////////////////////////////////////////////////

// Write Protect pin configuration
#define EEPROM_WP_PIN 5 // GPIO pin for Write Protect

// I2C configurations
#define I2C_PORT i2c1
#define I2C_SDA_PIN 2  // GPIO pin for SDA
#define I2C_SCL_PIN 3  // GPIO pin for SCL
#define I2C_FREQUENCY 100000  // 100 kHz standard mode

// EEPROM configurations
#define EEPROM_ADDR 0x50       // Default address for most EEPROMs (modify as needed)
#define EEPROM_PAGE_SIZE 32    // Page size in bytes
#define EEPROM_WRITE_DELAY 5   // Delay in ms after write operation

// Function to write a page to EEPROM
bool write_eeprom_page(uint16_t memory_address, const uint8_t *data, size_t len) {

    // Check if we're writing more than a page
    if (len > EEPROM_PAGE_SIZE) {
        return false;
    }

    // Buffer for the combined address and data
    // 2 bytes for memory address + data bytes
    uint8_t buffer[2 + EEPROM_PAGE_SIZE];

    // Set the memory address (big endian)
    buffer[0] = (memory_address >> 8) & 0xFF;
    buffer[1] = memory_address & 0xFF;

    // Copy data to the buffer after the address
    for (size_t i = 0; i < len; i++) {
        buffer[i + 2] = data[i];
    }

    // Write the address and data to EEPROM
    int bytes_written = i2c_write_blocking(I2C_PORT, EEPROM_ADDR, buffer, len + 2, false);

    // Check if write was successful
    if (bytes_written != len + 2) {
        return false;
    }
    // Wait for the write cycle to complete
    sleep_ms(EEPROM_WRITE_DELAY);

    return true;
}

// Function to read data from EEPROM for verification
bool read_eeprom(uint16_t memory_address, uint8_t *buffer, size_t len) {

    // Write the address to EEPROM
    if (i2c_write_blocking(I2C_PORT, EEPROM_ADDR, (uint8_t *)&memory_address, 2, true) != 2) {
        return false;
    }

    // Read data from EEPROM
    if (i2c_read_blocking(I2C_PORT, EEPROM_ADDR, buffer, len, false) != len) {
        return false;
    }

    return true;
}

int main()
{
    // Initialize standard I/O
    stdio_init_all();
    sleep_ms(1000);

    // Allow EEPROM writing
    gpio_init(EEPROM_WP_PIN);
    gpio_set_dir(EEPROM_WP_PIN, GPIO_OUT);
    gpio_put(EEPROM_WP_PIN, 0); // Set WP pin low to allow writing

    // Initialize I2C
    i2c_init(I2C_PORT, I2C_FREQUENCY);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    // Build data array
    uint8_t data[10 + 20 + 1 + 1 + num_chan];
    data[0] = '\0';
    strncat(data, magic, 10);
    strncat(data, name, 20);
    strncat(data, &pcb_rev, 1);
    strncat(data, &num_chan, 1);
    strncat(data, channel_map, num_chan);

    // Starting memory address in EEPROM
    uint16_t start_addr = 0x0000;

    printf("Writing data to EEPROM...\n");

    // Write data in pages
    int num_bytes = sizeof(data);
    for (size_t offset = 0; offset < num_bytes; offset += EEPROM_PAGE_SIZE) {
        size_t bytes_to_write = (offset + EEPROM_PAGE_SIZE <= num_bytes) ?
                                EEPROM_PAGE_SIZE : (num_bytes - offset);

        if (!write_eeprom_page(start_addr + offset, &data[offset], bytes_to_write)) {
            printf("Failed to write at address 0x%04x\n", start_addr + offset);
            return -1;
        }

        printf("Wrote %d bytes at address 0x%04x\n", bytes_to_write, start_addr + offset);
    }

    printf("Write complete. Verifying data...\n");

    // Read back for verification
    uint8_t read_buffer[sizeof(data)];
    if (!read_eeprom(start_addr, read_buffer, num_bytes)) {
        printf("Failed to read from EEPROM\n");
        return -1;
    }

    // Verify data
    bool verify_passed = true;
    for (int i = 0; i < sizeof(data); i++) {
        if (read_buffer[i] != data[i]) {
            printf("Verification failed at byte %d: expected %d, got %d\n",
                   i, data[i], read_buffer[i]);
            verify_passed = false;
        }
    }

    if (verify_passed) {
        printf("Verification successful! All data matches.\n");
    } else {
        printf("Verification failed! Data mismatch detected.\n");
    }

    // Main loop
    while (true) {
        sleep_ms(1000);
    }

    return 0;
}
