#include "storage.h"
#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "hardware/flash.h"
#include "hardware/sync.h"

// ---- Logging ----
#define LOG_INF(fmt, ...) printf("[INF] " fmt "\n", ##__VA_ARGS__)
#define LOG_WRN(fmt, ...) printf("[WRN] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERR(fmt, ...) printf("[ERR] " fmt "\n", ##__VA_ARGS__)

// ---- Config ----
#define MAX_ACTUATORS 16
#define FLASH_SECTOR_SIZE 4096

// ---- Flash Layout ----
typedef struct {
    uint32_t magic;
    uint32_t count;
    uint8_t states[MAX_ACTUATORS];
} storage_block_t;

#define STORAGE_MAGIC 0xAQUA1234

// ---- RAM Cache ----
static uint8_t current_states[MAX_ACTUATORS];
static size_t states_count = 0;
static bool settings_loaded = false;

// ---- Flash pointer ----
static const uint8_t *flash_target =
    (const uint8_t *)(XIP_BASE + STORAGE_FLASH_OFFSET);

// ---- Internal ----

static bool validate_states(const uint8_t *states, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (states[i] != 0 && states[i] != 1) {
            return false;
        }
    }
    return true;
}

// ---- Init ----

int storage_init(void)
{
    memset(current_states, 0, sizeof(current_states));
    states_count = 0;
    settings_loaded = false;

    const storage_block_t *block = (const storage_block_t *)flash_target;

    if (block->magic != STORAGE_MAGIC) {
        LOG_INF("No valid storage found");
        return 0;
    }

    if (block->count == 0 || block->count > MAX_ACTUATORS) {
        LOG_WRN("Invalid stored count");
        return 0;
    }

    if (!validate_states(block->states, block->count)) {
        LOG_WRN("Invalid stored states");
        return 0;
    }

    memcpy(current_states, block->states, block->count);
    states_count = block->count;
    settings_loaded = true;

    LOG_INF("Loaded %u actuator states from flash", (unsigned)states_count);
    return 0;
}

// ---- Save ----

int storage_save_actuator_states(const uint8_t *states, size_t count)
{
    if (!states || count == 0 || count > MAX_ACTUATORS) {
        return -1;
    }

    if (!validate_states(states, count)) {
        LOG_ERR("Invalid state values");
        return -1;
    }

    storage_block_t block;
    memset(&block, 0xFF, sizeof(block));

    block.magic = STORAGE_MAGIC;
    block.count = count;
    memcpy(block.states, states, count);

    uint32_t ints = save_and_disable_interrupts();

    // erase sector
    flash_range_erase(STORAGE_FLASH_OFFSET, FLASH_SECTOR_SIZE);

    // program (must be multiple of 256 bytes)
    flash_range_program(STORAGE_FLASH_OFFSET,
                        (const uint8_t *)&block,
                        ((sizeof(block) + 255) / 256) * 256);

    restore_interrupts(ints);

    memcpy(current_states, states, count);
    states_count = count;
    settings_loaded = true;

    LOG_INF("Saved %u actuator states", (unsigned)count);
    return 0;
}

// ---- Load ----

int storage_load_actuator_states(uint8_t *states, size_t count)
{
    if (!states || count == 0) return -1;

    if (!settings_loaded || states_count == 0) {
        return -2; // equivalent to -ENOENT
    }

    if (count != states_count) {
        return -1;
    }

    memcpy(states, current_states, count);
    return 0;
}

// ---- Check ----

bool storage_has_saved_states(void)
{
    return settings_loaded && (states_count > 0);
}

// ---- Clear ----

int storage_clear_actuator_states(void)
{
    uint32_t ints = save_and_disable_interrupts();

    flash_range_erase(STORAGE_FLASH_OFFSET, FLASH_SECTOR_SIZE);

    restore_interrupts(ints);

    memset(current_states, 0, sizeof(current_states));
    states_count = 0;
    settings_loaded = false;

    LOG_INF("Storage cleared");
    return 0;
}