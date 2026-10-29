#ifndef MQTT_QUEUE_H
#define MQTT_QUEUE_H

#include <stddef.h>
#include <stdint.h>
#include "application_config.h"
#include "FreeRTOS.h"
#include "task.h"

typedef enum {
  MQTT_OPERATION_PUBLISH,
  MQTT_OPERATION_RECIEVE,
  MQTT_OPERATION_SUBSCRIBE
} mqtt_operation_t;

typedef struct {
  mqtt_operation_t operation;
  char topic[MAX_MQTT_TOPIC_SIZE];
  size_t topic_length;
  uint8_t payload[MAX_MQTT_PAYLOAD_SIZE];
  size_t payload_length;
  /* Optional dispatcher notification: 1 = success, 2 = failure. */
  TaskHandle_t completion_task;
} mqtt_queue_item_t;

bool mqtt_dispatch_sync(mqtt_queue_item_t *item);

#endif
