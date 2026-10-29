#include "ota_storage.h"
#include "main.h"
#include <string.h>

uint32_t ota_crc32(const uint8_t *data, size_t length)
{
  uint32_t crc = UINT32_MAX;
  while (length-- != 0)
  {
    crc ^= *data++;
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
  }
  return ~crc;
}

bool ota_storage_erase(uint32_t address)
{
  if (address != OTA_APP_ADDRESS && address != OTA_STAGE_ADDRESS) return false;
  FLASH_EraseInitTypeDef erase = {0};
  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.Sector = address == OTA_APP_ADDRESS ? FLASH_SECTOR_5 : FLASH_SECTOR_6;
  erase.NbSectors = 1;
  erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
  uint32_t error = 0;
  if (HAL_FLASH_Unlock() != HAL_OK) return false;
  HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase, &error);
  HAL_FLASH_Lock();
  return status == HAL_OK;
}

bool ota_storage_write(uint32_t address, const uint8_t *data, size_t length)
{
  uint32_t base = address >= OTA_STAGE_ADDRESS ? OTA_STAGE_ADDRESS : OTA_APP_ADDRESS;
  if (data == NULL || length == 0 || (address & 3U) != 0 ||
      address < base || address >= base + OTA_SLOT_SIZE ||
      length > base + OTA_SLOT_SIZE - address) return false;
  if (HAL_FLASH_Unlock() != HAL_OK) return false;
  bool ok = true;
  for (size_t offset = 0; offset < length; offset += 4U)
  {
    uint32_t word = UINT32_MAX;
    size_t count = length - offset < 4U ? length - offset : 4U;
    memcpy(&word, data + offset, count);
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address + offset, word) != HAL_OK ||
        *(volatile uint32_t *)(address + offset) != word)
    {
      ok = false;
      break;
    }
  }
  HAL_FLASH_Lock();
  return ok;
}

bool ota_image_vectors_valid(uint32_t storage_address, size_t length)
{
  if (length < 8U || length > OTA_SLOT_SIZE) return false;
  const uint32_t *vectors = (const uint32_t *)storage_address;
  uint32_t sp = vectors[0], reset = vectors[1];
  return sp > 0x20000000U && sp <= 0x20020000U && (sp & 7U) == 0 &&
         (reset & 1U) != 0 && (reset & ~1U) >= OTA_APP_ADDRESS &&
         (reset & ~1U) < OTA_APP_ADDRESS + length;
}
