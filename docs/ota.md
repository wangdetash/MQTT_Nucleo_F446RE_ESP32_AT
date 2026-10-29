# OTA job and image contract

The updater accepts one file per job. The custom job document is:

```json
{
  "streamID": "firmware-stream",
  "files": [{"fileId": 0, "fileSize": 12345, "crc32": 305419896}]
}
```

`crc32` is the unsigned decimal CRC-32/ISO-HDLC of the exact firmware bytes
(the same result as Python `zlib.crc32`). Replace the example size and checksum
with the actual image values. File IDs must be 0..255. Images must be 8..131072
bytes. The parser rejects missing checksums, invalid numbers, and multiple files.
The complete Jobs response must fit the existing 512-byte MQTT receive capacity;
keep job documents compact. Stream and job response topics must fit 80 bytes
including the terminator. Oversized topics are rejected before flash erasure.

## Flash layout

- Updater: `0x08000000..0x0801FFFF` (128 KB, enforced by the linker).
- Application: `0x08020000..0x0803FFFF` (sector 5, 128 KB).
- Staging: `0x08040000..0x0805FFFF` (sector 6, 128 KB).

The downloadable application must be linked at `0x08020000` and configure its
vector table for that address. Do not upload the updater ELF/binary as the
application image. Vector checks reject reset handlers outside the application.

The download uses 256-byte JSON/base64 blocks and checks file ID, block index,
exact remaining size, flash write results, whole-image CRC, initial stack pointer,
and reset vector. The final partial word is padded with erased bytes; CRC covers
only the original file bytes. Installation starts only after staging validation.
The application copy is checked again. All MQTT commands run through the AT
dispatcher, including subscriptions, block retries and failure reports. The updater
waits for AWS to accept the success status before transferring control. If this
acknowledgement is missing/rejected it stays in the updater.

CRC detects accidental corruption; it is not firmware signature authentication.
This is not a power-fail-safe bootloader: interruption during the application copy
can leave that slot incomplete. There is no persistent installed-image metadata or
automatic boot/resume after reset yet. A no-job response keeps the updater running.
Hardware testing, signed-image verification and reset recovery remain required
before deployment use.
