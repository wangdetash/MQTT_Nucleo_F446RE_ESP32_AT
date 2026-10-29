#include "ota_application.h"
#include "custom_job_parser.h"
#include "ota_storage.h"
#include "mqtt_queue.h"
#include "MQTTFileDownloader.h"
#include "MQTTFileDownloader_config.h"
#include "jobs.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
extern UART_HandleTypeDef huart4;

#define START_TOPIC "$aws/things/" CLIENT_ID "/jobs/start-next"
#define BLOCK_SIZE mqttFileDownloader_CONFIG_BLOCK_SIZE
_Static_assert(4U * ((BLOCK_SIZE + 2U) / 3U) + 128U <= MAX_MQTT_PAYLOAD_SIZE,
               "MQTT payload cannot hold an encoded OTA block");
static MqttFileDownloaderContext_t downloader;
static char job_id[65];
static custom_job_doc_fields_t file;
static uint32_t received, block_index;
static bool downloading, installed;
static TickType_t requested_at;
static unsigned retries;

static bool send_topic(const char *topic, size_t length, mqtt_operation_t operation)
{
  mqtt_queue_item_t item = {0};
  if (length == 0 || length >= sizeof(item.topic)) return false;
  memcpy(item.topic, topic, length);
  item.topic_length = length;
  item.operation = operation;
  return mqtt_dispatch_sync(&item);
}

static bool subscribe_responses(const char *topic, size_t length)
{
  char response[MAX_MQTT_TOPIC_SIZE];
  if (length + sizeof("/accepted") > sizeof(response)) return false;
  memcpy(response, topic, length);
  memcpy(response + length, "/accepted", sizeof("/accepted"));
  if (!send_topic(response, length + sizeof("/accepted") - 1U,
                  MQTT_OPERATION_SUBSCRIBE)) return false;
  memcpy(response + length, "/rejected", sizeof("/rejected"));
  return send_topic(response, length + sizeof("/rejected") - 1U,
                    MQTT_OPERATION_SUBSCRIBE);
}

OTA_Status_t ota_start(void)
{
  mqtt_queue_item_t item = {0};
  if (Jobs_StartNext(item.topic, sizeof(item.topic), CLIENT_ID, strlen(CLIENT_ID),
                     &item.topic_length) != JobsSuccess) return OTA_ERR_TOPIC_BUILD;
  if (!subscribe_responses(item.topic, item.topic_length)) return OTA_ERR_SUBSCRIBE;
  item.payload_length = Jobs_StartNextMsg(OTA_CLIENT_TOKEN, strlen(OTA_CLIENT_TOKEN),
                                          (char *)item.payload, sizeof(item.payload));
  if (item.payload_length == 0 || item.payload_length >= sizeof(item.payload))
    return OTA_ERR_MSG_TRUNC;
  item.operation = MQTT_OPERATION_PUBLISH;
  return mqtt_dispatch_sync(&item) ? OTA_SUCCESS : OTA_ERR_PUBLISH;
}

static bool update_job(JobCurrentStatus_t status)
{
  mqtt_queue_item_t item = {0};
  if (Jobs_Update(item.topic, sizeof(item.topic), CLIENT_ID, strlen(CLIENT_ID),
                   job_id, strlen(job_id), &item.topic_length) != JobsSuccess)
    return false;
  item.payload_length = Jobs_UpdateMsg(status, OTA_CLIENT_TOKEN, strlen(OTA_CLIENT_TOKEN),
                                       (char *)item.payload, sizeof(item.payload));
  if (item.payload_length == 0 || item.payload_length >= sizeof(item.payload)) return false;
  item.operation = MQTT_OPERATION_PUBLISH;
  return mqtt_dispatch_sync(&item);
}

static void fail_download(const char *reason)
{
  downloading = false;
  installed = false;
  printf("OTA failed: %s\r\n", reason);
  if (job_id[0] != '\0' && !update_job(Failed))
    printf("OTA failure status publish failed\r\n");
}

static bool request_block(void)
{
  mqtt_queue_item_t item = {0};
  item.topic_length = downloader.topicGetStreamLength;
  if (item.topic_length >= sizeof(item.topic)) return false;
  memcpy(item.topic, downloader.topicGetStream, item.topic_length);
  item.payload_length = mqttDownloader_createGetDataBlockRequest(DATA_TYPE_JSON,
      file.file_id, BLOCK_SIZE, block_index, 1, (char *)item.payload, sizeof(item.payload));
  if (item.payload_length == 0 || item.payload_length >= sizeof(item.payload)) return false;
  item.operation = MQTT_OPERATION_PUBLISH;
  bool ok = mqtt_dispatch_sync(&item);
  requested_at = xTaskGetTickCount();
  return ok;
}

void ota_poll(void)
{
  if (downloading && (TickType_t)(xTaskGetTickCount() - requested_at) >= pdMS_TO_TICKS(10000))
  {
    if (++retries > 3 || !request_block()) fail_download("block request timed out");
  }
}

static void start_job(char *message, size_t length)
{
  if (downloading || installed) return; /* Do not erase on duplicate job responses. */
  const char *doc = NULL, *id = NULL;
  size_t doc_length = Jobs_GetJobDocument(message, length, &doc);
  size_t id_length = Jobs_GetJobId(message, length, &id);
  if (doc_length == 0 || id_length == 0)
  {
    printf("No pending OTA job\r\n");
    return;
  }
  custom_job_doc_fields_t parsed;
  if (id_length >= sizeof(job_id) ||
      custom_parser_parse_job_doc_file(doc, doc_length, &parsed) != 0)
  {
    printf("Invalid OTA job: require streamID, fileId, fileSize and crc32\r\n");
    return;
  }
  memcpy(job_id, id, id_length);
  job_id[id_length] = '\0';
  mqtt_queue_item_t update = {0};
  if (Jobs_Update(update.topic, sizeof(update.topic), CLIENT_ID, strlen(CLIENT_ID),
                   job_id, id_length, &update.topic_length) != JobsSuccess ||
      !subscribe_responses(update.topic, update.topic_length))
  {
    fail_download("job response topic too long or subscription failed");
    return;
  }
  if (mqttDownloader_init(&downloader, parsed.image_ref, parsed.image_ref_len,
                          CLIENT_ID, strlen(CLIENT_ID), DATA_TYPE_JSON) != MQTTFileDownloaderSuccess ||
      !send_topic(downloader.topicStreamData, downloader.topicStreamDataLength,
                   MQTT_OPERATION_SUBSCRIBE))
  {
    fail_download("stream subscription failed");
    return;
  }
  file = parsed;
  file.image_ref = NULL; /* Parsed strings refer to the temporary RX buffer. */
  if (!ota_storage_erase(OTA_STAGE_ADDRESS))
  {
    fail_download("staging erase failed");
    return;
  }
  received = block_index = retries = 0;
  downloading = true;
  if (!request_block()) fail_download("block request failed");
}

static void finish_download(void)
{
  downloading = false;
  const uint8_t *staged = (const uint8_t *)OTA_STAGE_ADDRESS;
  if (received != file.file_size || ota_crc32(staged, received) != file.crc32 ||
      !ota_image_vectors_valid(OTA_STAGE_ADDRESS, received))
  {
    fail_download("image checksum or vectors invalid");
    return;
  }
  if (!ota_storage_erase(OTA_APP_ADDRESS) ||
      !ota_storage_write(OTA_APP_ADDRESS, staged, received) ||
      ota_crc32((const uint8_t *)OTA_APP_ADDRESS, received) != file.crc32)
  {
    fail_download("application copy or readback failed");
    return;
  }
  installed = true;
  if (!update_job(Succeeded))
    printf("Image installed; success publication failed, staying in updater\r\n");
}

bool ota_handle_incoming_mqtt_message(char *topic, size_t topic_length,
                                       char *message, size_t message_length)
{
  if (topic == NULL || message == NULL) return false;
  if (Jobs_IsStartNextAccepted(topic, topic_length, CLIENT_ID, strlen(CLIENT_ID)))
  {
    start_job(message, message_length);
    return true;
  }
  if (topic_length == sizeof(START_TOPIC "/rejected") - 1U &&
      memcmp(topic, START_TOPIC "/rejected", topic_length) == 0)
  {
    printf("Jobs start-next rejected: %.*s\r\n", (int)message_length, message);
    return true;
  }
  if (job_id[0] != '\0')
  {
    if (Jobs_IsJobUpdateStatus(topic, topic_length, job_id, strlen(job_id),
                               CLIENT_ID, strlen(CLIENT_ID), JobUpdateStatus_Accepted))
    {
      if (installed) run_application();
      return true;
    }
    if (Jobs_IsJobUpdateStatus(topic, topic_length, job_id, strlen(job_id),
                               CLIENT_ID, strlen(CLIENT_ID), JobUpdateStatus_Rejected))
    {
      printf("Jobs update rejected; staying in updater\r\n");
      return true;
    }
  }
  if (!downloading || mqttDownloader_isDataBlockReceived(&downloader, topic, topic_length)
                       != MQTTFileDownloaderSuccess) return false;
  uint8_t data[BLOCK_SIZE];
  size_t length = 0;
  int32_t file_id = -1, block_id = -1, block_size = 0;
  MQTTFileDownloaderStatus_t status = mqttDownloader_processReceivedDataBlock(
      &downloader, (uint8_t *)message, message_length, &file_id, &block_id,
      &block_size, data, &length);
  size_t expected = file.file_size - received;
  if (expected > BLOCK_SIZE) expected = BLOCK_SIZE;
  if (status != MQTTFileDownloaderSuccess || file_id != (int32_t)file.file_id ||
      block_id != (int32_t)block_index || block_size != (int32_t)expected || length != expected)
  {
    printf("Ignored invalid, duplicate or unexpected OTA block\r\n");
    return true;
  }
  if (!ota_storage_write(OTA_STAGE_ADDRESS + received, data, length))
  {
    fail_download("block flash write failed");
    return true;
  }
  received += length;
  ++block_index;
  retries = 0;
  if (received == file.file_size) finish_download();
  else if (!request_block()) fail_download("next block request failed");
  return true;
}

/* No C stack accesses are allowed after switching away from the RTOS PSP. */
__attribute__((naked, noreturn)) static void enter_application(uint32_t sp, uint32_t reset)
{
  __asm volatile("msr msp, r0\n"
                 "movs r2, #0\n"
                 "msr control, r2\n"
                 "msr basepri, r2\n"
                 "isb\n"
                 "cpsie i\n"
                 "bx r1\n");
}

void run_application(void)
{
  if (!installed || !ota_image_vectors_valid(OTA_APP_ADDRESS, file.file_size)) return;
  __disable_irq();
  HAL_UART_DMAStop(&huart4);
  HAL_RCC_DeInit();
  HAL_DeInit();
  SysTick->CTRL = 0;
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
  for (unsigned i = 0; i < 8; ++i)
  {
    NVIC->ICER[i] = UINT32_MAX;
    NVIC->ICPR[i] = UINT32_MAX;
  }
  SCB->VTOR = OTA_APP_ADDRESS;
  __DSB();
  __ISB();
  const uint32_t *vectors = (const uint32_t *)OTA_APP_ADDRESS;
  enter_application(vectors[0], vectors[1]);
}
