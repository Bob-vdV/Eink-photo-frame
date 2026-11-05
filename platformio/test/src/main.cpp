/*
 * ble_min_test.ino
 *
 * Absolute minimum BLE advertising test for the Seeed XIAO nRF52840.
 *
 * Deliberately excludes: the ePaper display driver, Adafruit_SPIFlash, the
 * QSPI descriptor, the ring buffer, and the upload state machine. If BLE
 * cannot advertise from THIS sketch, the problem is the radio, the
 * SoftDevice, or the bootloader - not anything in the gallery code.
 *
 * Differences from the gallery sketch, on purpose:
 *   - The NAME goes in the main advertising packet (addName), not the scan
 *     response. Dumb scanners that ignore scan responses will still see it.
 *   - No 128-bit service UUID is advertised, so the packet stays small and
 *     maximally compatible. Visibility test, not a functional test.
 *   - Prints the return value of Bluefruit.begin() and reads the name back,
 *     so you get a real answer instead of a hopeful Serial.println().
 *
 * Board: Seeed XIAO nRF52840 (Plus)
 * Core:  Seeeduino nRF52 1.1.13 (Adafruit-based)
 */

#include "board/boards/XIAO_ePaper_Boards.h"
#include "driver/epaper/Driver_ED2208.h"
#include "panel/Panel_EPaper.h"
#include "panel/configs/Seeed_Panel_Configs.h"
#include <Adafruit_SPIFlash.h>
#include <Arduino.h>
#include <Seeed_GFX.h>
#include <bluefruit.h>

/* ===========================================================================
 * BLE SERVICE
 * ===========================================================================
 */

BLEUuid svcUuid("19b10000-e8f2-537e-4f6c-d104fc282a4e");
BLEUuid cmdUuid("19b10001-e8f2-537e-4f6c-d104fc282a4e");
BLEUuid respUuid("19b10002-e8f2-537e-4f6c-d104fc282a4e");

BLEService epaperSvc(svcUuid);
BLECharacteristic cmdChar (cmdUuid,  BLEWrite | BLEWriteWithoutResponse, 244);
BLECharacteristic respChar(respUuid, BLENotify, 244);

#define NOTIFY_MAX 240

/* ===========================================================================
 * INCOMING DATA RING BUFFER (SHRUNK FROM 2048 TO 512)
 * ===========================================================================
 * With 240-byte BLE MTU, 512 bytes gives us ~2 writes of headroom.
 * This is tight but should work since we drain aggressively.
 */

#define RX_RING_SIZE 512 // WAS 2048 - saving 1.5KB
static uint8_t rxRing[RX_RING_SIZE];
static volatile uint16_t rxHead = 0;
static volatile uint16_t rxTail = 0;

static inline bool rxPush(uint8_t b) {
  uint16_t nh = (uint16_t)((rxHead + 1) & (RX_RING_SIZE - 1));
  if (nh == rxTail)
    return false;
  rxRing[rxHead] = b;
  rxHead = nh;
  return true;
}

static inline bool rxPop(uint8_t *b) {
  if (rxHead == rxTail)
    return false;
  *b = rxRing[rxTail];
  rxTail = (uint16_t)((rxTail + 1) & (RX_RING_SIZE - 1));
  return true;
}

/* ===========================================================================
 * FLASH
 * ===========================================================================
 */

SPIFlash_Device_t const EPAPER_FLASH = {
    .total_size = (1UL << 21),
    .start_up_time_us = 10000,
    .manufacturer_id = 0x85,
    .memory_type = 0x60,
    .capacity = 0x15,
    .max_clock_speed_mhz = 55,
    .quad_enable_bit_mask = 0x02,
    .has_sector_protection = 1,
    .supports_fast_read = 1,
    .supports_qspi = 1,
    .supports_qspi_writes = 1,
    .write_status_register_split = 1,
    .single_status_byte = 0,
};

Adafruit_FlashTransport_QSPI flashTransport;
Adafruit_SPIFlash flash(&flashTransport);
Seeed_GFX display;

/* ===========================================================================
 * GEOMETRY
 * ===========================================================================
 */

#define IMG_W 800
#define IMG_H 480
#define ROW_BYTES (IMG_W / 2) // 400
#define IMG_BYTES 192000UL

#define CHUNK_ROWS 20
#define CHUNK_BYTES (CHUNK_ROWS * ROW_BYTES) // 8000
#define CHUNKS_PER_IMG (IMG_H / CHUNK_ROWS)  // 24

#define SECTOR_SIZE 4096UL
#define SLOT_BYTES 196608UL
#define MAX_IMAGES 10

#define REFRESH_AT_END true

// chunkBuf stays here - it's used during display, can't remove it
static uint8_t chunkBuf[CHUNK_BYTES];
static uint32_t flashSize = 0;
static uint32_t dirOffset = 0;
static uint8_t slotFlags[MAX_IMAGES];
static const uint8_t DIR_MAGIC[4] = {'E', 'P', 'A', 'P'};

/* ===========================================================================
 * UPLOAD STATE
 * ===========================================================================
 */

enum UploadState : uint8_t {
  UP_IDLE = 0,
  UP_AWAIT_LEN,
  UP_STREAMING,
  UP_AWAIT_SUM
};

static UploadState upState = UP_IDLE;
static uint8_t upSlot = 0;
static uint8_t hdrBuf[4];
static uint8_t hdrFill = 0;
static uint32_t upWritten = 0;
static uint32_t upSum = 0;
static uint16_t stageFill = 0;
// Removed slack from stageBuf - exact size now
static uint8_t stageBuf[CHUNK_BYTES];

/* ===========================================================================
 * FORWARD DECLARATIONS
 * ===========================================================================
 */

static void bleNotify(const char *s);
static void bleNotifyf(const char *fmt, ...);
static void handleLine(char *line);
static void feedUploadByte(uint8_t b);

/* ===========================================================================
 * BLE NOTIFY
 * ===========================================================================
 */

static void bleNotify(const char *s) {
  size_t n = strlen(s);
  if (n > NOTIFY_MAX)
    n = NOTIFY_MAX;
  respChar.notify((const uint8_t *)s, (uint16_t)n);
}

static void bleNotifyf(const char *fmt, ...) {
  char buf[NOTIFY_MAX + 1];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  bleNotify(buf);
}

/* ===========================================================================
 * FLASH HELPERS
 * ===========================================================================
 */

static inline uint32_t slotBase(uint8_t n) { return (uint32_t)n * SLOT_BYTES; }

static bool eraseSlot(uint8_t n) {
  const uint32_t first = slotBase(n) / SECTOR_SIZE;
  const uint32_t count = SLOT_BYTES / SECTOR_SIZE;
  for (uint32_t i = 0; i < count; i++) {
    if (!flash.eraseSector(first + i))
      return false;
  }
  return true;
}

static bool writeChunkVerified(uint32_t addr, const uint8_t *buf,
                               uint32_t len) {
  if (flash.writeBuffer(addr, buf, len) != len)
    return false;

  uint8_t tmp[256];
  uint32_t done = 0;
  while (done < len) {
    uint32_t n = len - done;
    if (n > sizeof(tmp))
      n = sizeof(tmp);
    if (flash.readBuffer(addr + done, tmp, n) != n)
      return false;
    if (memcmp(tmp, buf + done, n) != 0)
      return false;
    done += n;
  }
  return true;
}

static bool loadSlotTable(void) {
  uint8_t buf[16];
  memset(buf, 0, sizeof(buf));
  if (flash.readBuffer(dirOffset, buf, 16) != 16 ||
      memcmp(buf, DIR_MAGIC, 4) != 0) {
    memset(slotFlags, 0, sizeof(slotFlags));
    return false;
  }
  memcpy(slotFlags, buf + 4, MAX_IMAGES);
  return true;
}

static bool saveSlotTable(void) {
  uint8_t buf[16];
  memset(buf, 0xFF, sizeof(buf));
  memcpy(buf, DIR_MAGIC, 4);
  memcpy(buf + 4, slotFlags, MAX_IMAGES);
  if (!flash.eraseSector(dirOffset / SECTOR_SIZE))
    return false;
  if (flash.writeBuffer(dirOffset, buf, 16) != 16)
    return false;
  return true;
}

static uint32_t largestFreeBlock(void) {
  uint32_t lo = 0, hi = 260 * 1024;
  while (lo < hi) {
    uint32_t mid = lo + (hi - lo + 1) / 2;
    void *p = malloc(mid);
    if (p) {
      free(p);
      lo = mid;
    } else {
      hi = mid - 1;
    }
  }
  return lo;
}

/* ===========================================================================
 * COMMANDS
 * ===========================================================================
 */

static void cmdProbe(void) {
  bleNotify("--- probe ---");
  bleNotifyf("JEDEC 0x%lX", (unsigned long)flash.getJEDECID());
  bleNotifyf("Flash %lu KB", (unsigned long)(flashSize / 1024));
  bleNotifyf("Sectors %lu", (unsigned long)flash.sectorCount());
  bleNotifyf("Slots %d x %lu", MAX_IMAGES, (unsigned long)IMG_BYTES);
  bleNotifyf("Dir 0x%lX", (unsigned long)dirOffset);
  bleNotifyf("Free %lu", (unsigned long)largestFreeBlock());

  char list[96] = "Have:";
  bool any = false;
  for (uint8_t i = 0; i < MAX_IMAGES; i++) {
    if (!slotFlags[i])
      continue;
    char t[6];
    snprintf(t, sizeof(t), " %u", i);
    strncat(list, t, sizeof(list) - strlen(list) - 1);
    any = true;
  }
  if (!any)
    strncat(list, " none", sizeof(list) - strlen(list) - 1);
  bleNotify(list);
  bleNotify("-------------");
}

static void cmdList(void) {
  char list[96] = "Have:";
  bool any = false;
  for (uint8_t i = 0; i < MAX_IMAGES; i++) {
    if (!slotFlags[i])
      continue;
    char t[6];
    snprintf(t, sizeof(t), " %u", i);
    strncat(list, t, sizeof(list) - strlen(list) - 1);
    any = true;
  }
  if (!any)
    strncat(list, " none", sizeof(list) - strlen(list) - 1);
  bleNotify(list);
}

static void cmdVerify(void) {
  bleNotify("--- V: high offsets ---");
  const uint32_t sectors[] = {483, 490, 497, 504};
  uint8_t pat[64], rb[64];
  uint8_t fails = 0;

  for (uint8_t i = 0; i < 4; i++) {
    const uint32_t sec = sectors[i];
    const uint32_t addr = sec * SECTOR_SIZE;
    for (uint8_t j = 0; j < 64; j++)
      pat[j] = (uint8_t)(j ^ 0xA5) ^ (uint8_t)sec;

    if (!flash.eraseSector(sec)) {
      bleNotifyf("%lu ERASE FAIL", (unsigned long)sec);
      fails++;
      continue;
    }
    if (flash.writeBuffer(addr, pat, 64) != 64) {
      bleNotifyf("%lu WRITE FAIL", (unsigned long)sec);
      fails++;
      continue;
    }
    if (flash.readBuffer(addr, rb, 64) != 64) {
      bleNotifyf("%lu READ FAIL", (unsigned long)sec);
      fails++;
      continue;
    }
    if (memcmp(pat, rb, 64) != 0) {
      bleNotifyf("%lu MISMATCH", (unsigned long)sec);
      fails++;
      continue;
    }
    bleNotifyf("%lu PASS", (unsigned long)sec);
  }

  if (fails == 0)
    bleNotify("V: ALL PASS - 2 MB addressable");
  else
    bleNotifyf("V: %u FAILURE(S)", fails);
}

static void cmdDisplay(uint8_t n) {
  if (n >= MAX_IMAGES) {
    bleNotify("ERR slot out of range");
    return;
  }
  if (!slotFlags[n]) {
    bleNotify("ERR slot empty");
    return;
  }

  bleNotifyf("Pushing %d chunks", CHUNKS_PER_IMG);
  const uint32_t t0 = millis();

  for (uint8_t c = 0; c < CHUNKS_PER_IMG; c++) {
    const uint32_t addr = slotBase(n) + (uint32_t)c * CHUNK_BYTES;
    if (flash.readBuffer(addr, chunkBuf, CHUNK_BYTES) != CHUNK_BYTES) {
      bleNotifyf("ERR read fail chunk %u", c);
      return;
    }
    const uint16_t y = (uint16_t)(c * CHUNK_ROWS);
    if (!display.pushImage4BPP(0, y, IMG_W, CHUNK_ROWS, chunkBuf, true)) {
      bleNotifyf("ERR push rejected chunk %u", c);
      return;
    }
  }

  bleNotifyf("Pushed in %lu ms", (unsigned long)(millis() - t0));

#if REFRESH_AT_END
  bleNotify("Refreshing...");
  const GfxResult r = display.refresh();
  if (!r)
    bleNotify(r.message);
  bleNotify("Refresh done");
#endif
}

static void cmdClearSlot(uint8_t n) {
  if (n >= MAX_IMAGES) {
    bleNotify("ERR slot out of range");
    return;
  }
  if (!slotFlags[n]) {
    bleNotify("ERR slot already empty");
    return;
  }
  slotFlags[n] = 0;
  bleNotify(saveSlotTable() ? "OK cleared" : "ERR slot table write failed");
}

static void cmdClearAll(void) {
  memset(slotFlags, 0, sizeof(slotFlags));
  bleNotify(saveSlotTable() ? "OK all cleared" : "ERR slot table write failed");
}

static void cmdBeginUpload(uint8_t n) {
  if (n >= MAX_IMAGES) {
    bleNotify("ERR slot out of range");
    return;
  }

  upSlot = n;

  if (slotFlags[n]) {
    slotFlags[n] = 0;
    saveSlotTable();
  }

  bleNotify("READY");

  if (!eraseSlot(n)) {
    bleNotify("ERR erase failed");
    upState = UP_IDLE;
    return;
  }

  hdrFill = 0;
  upWritten = 0;
  upSum = 0;
  stageFill = 0;
  upState = UP_AWAIT_LEN;
}

static void feedUploadByte(uint8_t b) {

  if (upState == UP_AWAIT_LEN) {
    hdrBuf[hdrFill++] = b;
    if (hdrFill < 4)
      return;

    const uint32_t len = (uint32_t)hdrBuf[0] | ((uint32_t)hdrBuf[1] << 8) |
                         ((uint32_t)hdrBuf[2] << 16) |
                         ((uint32_t)hdrBuf[3] << 24);

    if (len != IMG_BYTES) {
      bleNotifyf("ERR bad length %lu", (unsigned long)len);
      upState = UP_IDLE;
      return;
    }
    upState = UP_STREAMING;
    return;
  }

  if (upState == UP_STREAMING) {
    stageBuf[stageFill++] = b;
    upSum += b;
    upWritten++;

    if (stageFill == CHUNK_BYTES) {
      const uint32_t addr = slotBase(upSlot) + (upWritten - CHUNK_BYTES);
      if (!writeChunkVerified(addr, stageBuf, CHUNK_BYTES)) {
        bleNotifyf("ERR write/verify fail at %lu", (unsigned long)addr);
        upState = UP_IDLE;
        return;
      }
      stageFill = 0;
    }

    if (upWritten == IMG_BYTES) {
      if (stageFill > 0) {
        const uint32_t addr = slotBase(upSlot) + (upWritten - stageFill);
        if (!writeChunkVerified(addr, stageBuf, stageFill)) {
          bleNotify("ERR tail write fail");
          upState = UP_IDLE;
          return;
        }
        stageFill = 0;
      }
      hdrFill = 0;
      upState = UP_AWAIT_SUM;
    }
    return;
  }

  if (upState == UP_AWAIT_SUM) {
    hdrBuf[hdrFill++] = b;
    if (hdrFill < 4)
      return;

    const uint32_t hostSum = (uint32_t)hdrBuf[0] | ((uint32_t)hdrBuf[1] << 8) |
                             ((uint32_t)hdrBuf[2] << 16) |
                             ((uint32_t)hdrBuf[3] << 24);

    if (hostSum != upSum) {
      bleNotifyf("ERR checksum board %lu host %lu", (unsigned long)upSum,
                 (unsigned long)hostSum);
      upState = UP_IDLE;
      return;
    }

    slotFlags[upSlot] = 1;
    if (!saveSlotTable()) {
      slotFlags[upSlot] = 0;
      bleNotify("ERR could not commit slot table");
      upState = UP_IDLE;
      return;
    }

    bleNotifyf("OK %u %lu", upSlot, (unsigned long)IMG_BYTES);
    upState = UP_IDLE;
    return;
  }
}

/* ===========================================================================
 * LINE PARSER
 * ===========================================================================
 */

static char lineBuf[80];
static uint8_t lineLen = 0;

static void handleLine(char *line) {
  int len = strlen(line);
  while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) {
    line[--len] = '\0';
  }
  if (len == 0)
    return;

  const char cmd = toupper((unsigned char)line[0]);
  const int arg = atoi(line + 1);

  switch (cmd) {
  case 'P':
    cmdProbe();
    break;
  case 'L':
    cmdList();
    break;
  case 'D':
    cmdDisplay((uint8_t)arg);
    break;
  case 'X':
    cmdClearSlot((uint8_t)arg);
    break;
  case 'F':
    cmdClearAll();
    break;
  case 'V':
    cmdVerify();
    break;
  case 'U':
    cmdBeginUpload((uint8_t)arg);
    break;
  default:
    bleNotify("ERR unknown command (P L D X F V U)");
    break;
  }
}

static void drainRx(void) {
  uint8_t b;

  while (rxPop(&b)) {

    if (upState != UP_IDLE) {
      feedUploadByte(b);
      continue;
    }

    if (b == '\n') {
      if (lineLen > 0) {
        lineBuf[lineLen] = '\0';
        handleLine(lineBuf);
        lineLen = 0;
      }
      continue;
    }

    if (b == '\r')
      continue;

    if (lineLen < sizeof(lineBuf) - 1) {
      lineBuf[lineLen++] = b;
    } else {
      lineLen = 0;
      bleNotify("ERR command too long");
    }
  }
}

/* ===========================================================================
 * BLE CALLBACKS
 * ===========================================================================
 */

static void onConnect(uint16_t conn_hdl) {
  (void)conn_hdl;
  Serial.println("BLE: connected");
  bleNotify("connected");
}

static void onDisconnect(uint16_t conn_hdl, uint8_t reason) {
  (void)conn_hdl;
  Serial.print("BLE: disconnected, reason 0x");
  Serial.println(reason, HEX);

  upState = UP_IDLE;
  lineLen = 0;
  hdrFill = 0;
  stageFill = 0;
}

static void onCmdWritten(uint16_t conn_hdl, BLECharacteristic *chr,
                         uint8_t *data, uint16_t len) {
  (void)conn_hdl;
  (void)chr;

  for (uint16_t i = 0; i < len; i++) {
    if (!rxPush(data[i])) {
      Serial.println("WARNING: rxRing full, dropping byte");
      break;
    }
  }
}

/* ===========================================================================
 * SETUP / LOOP
 * ===========================================================================
 */

void setup(void) {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && (millis() - t0) < 3000) {
  }

  Serial.println();
  Serial.println("ePaper gallery - BLE (memory-optimized)");
  Serial.println("Commands: P L D<n> X<n> F V U<n>");

  /* ---- flash ---- */
  if (!flash.begin(&EPAPER_FLASH)) {
    Serial.println("CRITICAL: QSPI flash init failed");
    while (true) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(150);
      digitalWrite(LED_BUILTIN, LOW);
      delay(150);
    }
  }

  flashSize = flash.size();
  dirOffset = flashSize - SECTOR_SIZE;

  Serial.print("Flash: ");
  Serial.print(flashSize);
  Serial.print(" bytes, JEDEC 0x");
  Serial.println(flash.getJEDECID(), HEX);
  Serial.print("Free heap before display: ");
  Serial.println(largestFreeBlock());

  loadSlotTable();

  /* ---- display ---- */
  // Report memory status before attempting display init
  Serial.println("=== DISPLAY INIT START ===");
  Serial.print("Heap before: ");
  Serial.println(largestFreeBlock());

  bool displayOk = display.begin<Board_XIAO_ePaper_EN04,
                                 Config_Seeed_ePaper_7inch3_Colorful_ED2208>();

  Serial.print("Heap after: ");
  Serial.println(largestFreeBlock());
  Serial.println("=== DISPLAY INIT END ===");

  if (!displayOk) {
    Serial.println(
        "CRITICAL: Display init failed - probably insufficient heap");
    Serial.println("");
    Serial.println("OPTIONS:");
    Serial.println(
        "1. Remove display from board and re-flash to test BLE alone");
    Serial.println("2. Use smaller image resolution if driver supports it");
    Serial.println("3. Use USB-serial version instead (works fine)");
    Serial.println("");
    Serial.println(
        "Continuing anyway - BLE will work, display commands will fail");
  }

  /* ---- BLE ---- */
  Bluefruit.begin();
  Bluefruit.autoConnLed(false);
  Bluefruit.setTxPower(4);
  Bluefruit.setName("ePaper Gallery");

  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);

  epaperSvc.begin();

  cmdChar.setWriteCallback(onCmdWritten);
  cmdChar.begin();

  respChar.begin();

  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addService(epaperSvc);
  Bluefruit.ScanResponse.addName();

  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);

  Serial.println("Advertising as 'ePaper Gallery'");
  Serial.print("MTU max: ");
  Serial.println(BLE_GATT_ATT_MTU_MAX);

  cmdProbe();
}

void loop(void) {
  drainRx();
  yield();
}