#include "custom_job_parser.h"
#include "core_json.h"
#include "ota_storage.h"

static bool read_u32(const char *doc, size_t length, const char *query,
                     uint32_t *number)
{
  const char *value = NULL;
  size_t size = 0;
  JSONTypes_t type;
  if (JSON_SearchConst(doc, length, query, strlen(query), &value, &size,
                       &type) != JSONSuccess || type != JSONNumber || size == 0)
    return false;
  uint32_t result = 0;
  for (size_t i = 0; i < size; ++i)
  {
    if (value[i] < '0' || value[i] > '9') return false;
    uint32_t digit = (uint32_t)(value[i] - '0');
    if (result > (UINT32_MAX - digit) / 10U) return false;
    result = result * 10U + digit;
  }
  *number = result;
  return true;
}

int8_t custom_parser_parse_job_doc_file(const char *doc, size_t length,
                                       custom_job_doc_fields_t *result)
{
  custom_job_doc_fields_t parsed = {0};
  JSONTypes_t type;
  if (doc == NULL || result == NULL || JSON_Validate(doc, length) != JSONSuccess)
    return -1;
  if (JSON_SearchConst(doc, length, "streamID", sizeof("streamID") - 1U,
                       &parsed.image_ref, &parsed.image_ref_len, &type) != JSONSuccess ||
      type != JSONString || parsed.image_ref_len == 0 ||
      !read_u32(doc, length, "files[0].fileSize", &parsed.file_size) ||
      !read_u32(doc, length, "files[0].fileId", &parsed.file_id) ||
      !read_u32(doc, length, "files[0].crc32", &parsed.crc32) ||
      parsed.file_size < 8U || parsed.file_size > OTA_SLOT_SIZE ||
      parsed.file_id > 255U) return -1;
  for (size_t i = 0; i < parsed.image_ref_len; ++i)
  {
    char c = parsed.image_ref[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-')) return -1;
  }
  const char *extra;
  size_t extra_length;
  if (JSON_SearchConst(doc, length, "files[1]", sizeof("files[1]") - 1U,
                       &extra, &extra_length, NULL) == JSONSuccess) return -1;
  *result = parsed;
  return 0;
}
