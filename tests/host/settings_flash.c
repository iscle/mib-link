#include "settings_test_platform.h"
#include "settings.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
uint8_t fake_flash[PICO_FLASH_SIZE_BYTES];
static uint32_t millis;
static unsigned erased,programmed,reboots;
static int power_cut=-1,recovery;
void gpio_init(unsigned n){assert(n==15);}
void gpio_set_dir(unsigned n,unsigned d){}
void gpio_pull_up(unsigned n){}
void sleep_ms(unsigned ms){millis+=ms;}
int gpio_get(unsigned n){return !recovery;}
uint32_t save_and_disable_interrupts(void){return 1;}
void restore_interrupts(uint32_t s){assert(s==1);}
void flash_range_erase(unsigned o,size_t n){assert(o>=PICO_FLASH_SIZE_BYTES-8192 && n==4096);memset(fake_flash+o,255,n);erased++;}
void flash_range_program(unsigned o,const uint8_t *p,size_t n){assert(o>=PICO_FLASH_SIZE_BYTES-8192 && n==256);if(power_cut>=0 && (size_t)power_cut<n)n=power_cut;memcpy(fake_flash+o,p,n);programmed++;}
uint64_t get_absolute_time(void){return millis;}
uint32_t to_ms_since_boot(uint64_t t){return t;}
void watchdog_reboot(unsigned a,unsigned b,unsigned c){reboots++;}
int main(void){
    memset(fake_flash,255,sizeof(fake_flash));settings_init();assert(!strcmp(settings.ssid,"MST-Link"));
    struct mst_settings next=settings;strcpy(next.ssid,"Personal network");assert(settings_save(&next));
    assert(erased==1 && programmed==1);assert(!settings_save(&next));
    settings_poll();assert(!reboots);millis+=2001;settings_poll();assert(reboots==1);
    settings_init();assert(!strcmp(settings.ssid,"Personal network"));
    strcpy(next.ssid,"Second network");
    uint8_t saved[8192];memcpy(saved,fake_flash+sizeof(fake_flash)-8192,8192);
    for(int cut=0;cut<128;cut++){
        memcpy(fake_flash+sizeof(fake_flash)-8192,saved,8192);settings_init();power_cut=cut;
        bool success=settings_save(&next);settings_init();
        assert(!strcmp(settings.ssid,success?"Second network":"Personal network"));
    }
    power_cut=-1;memcpy(fake_flash+sizeof(fake_flash)-8192,saved,8192);settings_init();assert(settings_save(&next));settings_init();assert(!strcmp(settings.ssid,"Second network"));
    recovery=1;settings_init();assert(!strcmp(settings.ssid,"MST-Link"));recovery=0;settings_init();assert(!strcmp(settings.ssid,"Second network"));
    strcpy(next.password,"short");unsigned before=erased;assert(!settings_save(&next) && erased==before);
    puts("PASS: settings CRC journal, 128 interrupted writes, reboot delay, recovery pin and rejected settings");
}
