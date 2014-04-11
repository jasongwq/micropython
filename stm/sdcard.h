// this is a fixed size and should not be changed
#define SDCARD_BLOCK_SIZE (512)

bool sdcard_init(void);
bool sdcard_is_present(void);
bool sdcard_power_on(void);
void sdcard_power_off(void);
uint64_t sdcard_get_capacity_in_bytes(void);
bool sdcard_read(uint8_t *dest, uint32_t sector, uint32_t count);
bool sdcard_write(const uint8_t *src, uint32_t sector, uint32_t count);
