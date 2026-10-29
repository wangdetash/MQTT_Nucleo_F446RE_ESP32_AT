/*
 * custom_job_doc.h
 *
 *  Created on: 28-Oct-2026
 *      Author: Tashi George
 */

#ifndef INC_CUSTOM_JOB_DOC_H_
#define INC_CUSTOM_JOB_DOC_H_

#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>

typedef struct {
  const char *image_ref;         // "streamID"
  uint32_t file_size;            // files[0].fileSize

  size_t image_ref_len;
  uint32_t file_id;
  uint32_t crc32;
} custom_job_doc_fields_t;

#endif /* INC_CUSTOM_JOB_DOC_H_ */
