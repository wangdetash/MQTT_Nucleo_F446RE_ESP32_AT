#ifndef MQTT_FILE_DOWNLOADER_CONFIG_H
#define MQTT_FILE_DOWNLOADER_CONFIG_H
/* 256 binary bytes become 344 base64 bytes, leaving room for JSON metadata
 * inside the existing 512-byte MQTT payload. */
#define mqttFileDownloader_CONFIG_BLOCK_SIZE 256U
#endif
