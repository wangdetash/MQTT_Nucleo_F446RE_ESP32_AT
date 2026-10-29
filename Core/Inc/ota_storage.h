#ifndef OTA_STORAGE_H
#define OTA_STORAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define OTA_APP_ADDRESS 0x08020000U
#define OTA_STAGE_ADDRESS 0x08040000U
#define OTA_SLOT_SIZE (128U * 1024U)
bool ota_storage_erase(uint32_t address);
bool ota_storage_write(uint32_t address, const uint8_t *data, size_t length);
uint32_t ota_crc32(const uint8_t *data, size_t length);
bool ota_image_vectors_valid(uint32_t storage_address, size_t length);
#endif
