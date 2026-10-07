#pragma once
#include <stdint.h>
#include <stddef.h>
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#define FLASH_SECTOR_SIZE 4096
#define FLASH_PAGE_SIZE 256
extern uint8_t fake_flash[PICO_FLASH_SIZE_BYTES];
#define XIP_BASE ((uintptr_t)fake_flash)
#define GPIO_IN 0
void gpio_init(unsigned n);
void gpio_set_dir(unsigned n, unsigned d);
void gpio_pull_up(unsigned n);
void sleep_ms(unsigned ms);
int gpio_get(unsigned n);
uint32_t save_and_disable_interrupts(void);
void restore_interrupts(uint32_t state);
void flash_range_erase(unsigned offset, size_t size);
void flash_range_program(unsigned offset, const uint8_t *data, size_t size);
uint64_t get_absolute_time(void);
uint32_t to_ms_since_boot(uint64_t t);
void watchdog_reboot(unsigned a, unsigned b, unsigned c);
