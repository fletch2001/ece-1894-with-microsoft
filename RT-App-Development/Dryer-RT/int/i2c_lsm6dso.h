#include <stdint.h>

int32_t platform_write(void *handle, uint8_t Reg, const uint8_t *Bufp, uint16_t len);

int32_t platform_read(void *handle, uint8_t Reg, uint8_t *Bufp, uint16_t len);