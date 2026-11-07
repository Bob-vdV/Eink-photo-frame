/*
 * epaper_gallery_sd.ino
 *
 * Store packed-4bpp Spectra 6 images in the onboard QSPI flash of a
 * XIAO nRF52840 Plus on an EN04 ePaper board, display them on demand,
 * and load them from a microSD card wired to the shared SPI bus.
 *
 * Board:  Seeed XIAO nRF52840 Plus
 * Core:   Seeeduino nRF52 (Adafruit-based, v1.x)
 *
 * ---------------------------------------------------------------------------
 * HARDWARE
 * ---------------------------------------------------------------------------
 *   ePaper  : EN04 driver board, 7.0" Spectra 6, ED2208 (Seeed_GFX2)
 *   Flash   : onboard P25Q16H QSPI, 2 MB
 *   SD card : SPI microSD module, shared bus with the ePaper
 *
 *   SD module   XIAO pin
 *   ---------   --------
 *   SCK         D8      (top solder joint, shared with ePaper)
 *   MOSI        D10     (top solder joint, shared)
 *   MISO        D9      (top solder joint, shared; panel never drives it)
 *   CS          D4      (free per Seeed_GFX2's nrfDisplayPins())
 *   VCC         3V3
 *   GND         GND
 *
 *   Seeed_GFX2 claims for EN04: D3, D6, D7, D8, D10 + raw GPIO 35/30
 *   (D16/D11 via underside pads). D0-D2, D4, D5, D9 are free.
 *
 * ---------------------------------------------------------------------------
 * FLASH LAYOUT
 * ---------------------------------------------------------------------------
 *   0x000000 .. 0x1DFFFF   slot 0..9, 196,608 bytes each (48 sectors)
 *   0x1E0000 .. 0x1FEFFF   unused
 *   0x1FF000 .. 0x1FFFFF   directory sector (slot table)
 *
 *   slot n starts at n * 196608
 *
 *   Each chunk is exactly 20 image rows (8,000 bytes), keeping
 *   pushImage4BPP() calls row-aligned.
 *
 * ---------------------------------------------------------------------------
 * SERIAL PROTOCOL  (115200 baud)
 * ---------------------------------------------------------------------------
 *   P              probe: chip ID, geometry, slot table, SD status
 *   L              list stored slots
 *   D <n>          display slot n
 *   X <n>          clear slot n
 *   F              clear the slot table (image data stays but is unreachable)
 *   V              write/read test at high offsets - proves the full 2 MB
 *   U <n>          upload slot n; binary payload follows
 *   M              list files on the SD card (marks exactly-192000 as VALID)
 *   C <name> <slot>  copy an SD file into flash slot <slot>
 *   CD <name>      display an SD file directly (no flash, no slot limit)
 *
 * Upload framing (U):
 *   host -> board   'U' ' ' <n> '\n'
 *   board -> host   "READY\n"
 *   host -> board   <len:uint32 LE> <payload:len> <sum:uint32 LE>
 *   board -> host   "OK <n> <len>\n"   or   "ERR <reason>\n"
 *
 * SD files must be exactly 192,000 bytes (800x480 packed 4bpp) - exactly
 * what the web converter's "Save .bin" button produces. Filenames are
 * matched case-insensitively but keep them 8.3-ish (IMG001.4BP) for sanity.
 *
 * ---------------------------------------------------------------------------
 * FLASH WEAR
 * ---------------------------------------------------------------------------
 * Uploading erases 48 sectors. Never put flash writes in loop().
 */

#include <string.h>
#include <ctype.h>
#include <Adafruit_SPIFlash.h>
#include <SdFat.h>
#include <Seeed_GFX.h>
#include "board/boards/XIAO_ePaper_Boards.h"
#include "panel/configs/Seeed_Panel_Configs.h"
#include "driver/epaper/Driver_ED2208.h"
#include "panel/Panel_EPaper.h"

/* ===========================================================================
 * CHIP DESCRIPTOR
 *
 * Built from the P25Q16H datasheet. JEDEC ID 0x856015 - manufacturer 0x85,
 * memory type 0x60, capacity 0x15 (2^21 = 2 MB).
 *
 * NOTE: named EPAPER_FLASH, NOT P25Q16H - current Adafruit SPIFlash defines
 * P25Q16H as a macro in flash_devices.h, and colliding with it breaks the
 * build ("expected unqualified-id before '{' token").
 * ===========================================================================
 */

SPIFlash_Device_t const EPAPER_FLASH = {
  .total_size                  = (1UL << 21),   // 2 MiB
  .start_up_time_us            = 10000,
  .manufacturer_id             = 0x85,
  .memory_type                 = 0x60,
  .capacity                    = 0x15,
  .max_clock_speed_mhz         = 55,
  .quad_enable_bit_mask        = 0x02,
  .has_sector_protection       = 1,
  .supports_fast_read          = 1,
  .supports_qspi               = 1,
  .supports_qspi_writes        = 1,
  .write_status_register_split = 1,
  .single_status_byte          = 0,
};

Adafruit_FlashTransport_QSPI flashTransport;
Adafruit_SPIFlash flash(&flashTransport);

Seeed_GFX display;

/* ===========================================================================
 * SD CARD
 * ===========================================================================
 */

#define SD_CS           D4
#define SD_SCK_SPEED    SD_SCK_MHZ(2)   // drop to SD_SCK_MHZ(2) if flaky

SdFat sd;
static bool sdOK = false;

/* ===========================================================================
 * GEOMETRY
 * ===========================================================================
 */

#define IMG_W           800
#define IMG_H           480
#define ROW_BYTES       (IMG_W / 2)                     // 400
#define IMG_BYTES       192000UL                        // 800*480/2

#define CHUNK_ROWS      20
#define CHUNK_BYTES     (CHUNK_ROWS * ROW_BYTES)        // 8000
#define CHUNKS_PER_IMG  (IMG_H / CHUNK_ROWS)            // 24

#define SECTOR_SIZE     4096UL
#define SLOT_BYTES      196608UL                        // 48 sectors
#define MAX_IMAGES      10

#define RX_TIMEOUT      15000                           // ms

#define REFRESH_AT_END  true

// 0 = silent upload (single-line verdict). 1 = progress chatter for hand
// debugging in a serial monitor. Leave at 0 when driving from a script.
#define VERBOSE_UPLOAD  0

#if CHUNK_BYTES > 60000
#error "CHUNK_BYTES too large for a single read"
#endif

static uint8_t chunkBuf[CHUNK_BYTES];
static char    lineBuf[64];
static uint8_t lineLen = 0;

static uint32_t flashSize = 0;
static uint32_t dirOffset = 0;
static uint8_t  slotFlags[MAX_IMAGES];

static const uint8_t DIR_MAGIC[4] = { 'E', 'P', 'A', 'P' };


/* ===========================================================================
 * LOW-LEVEL HELPERS
 * ===========================================================================
 */

static uint32_t slotBase(uint8_t n) {
    return (uint32_t)n * SLOT_BYTES;
}

static bool readInto(uint8_t *dst, uint32_t len, uint32_t timeoutMs) {
    uint32_t got  = 0;
    uint32_t last = millis();

    while (got < len) {
        if (Serial.available()) {
            int n = Serial.readBytes(dst + got, len - got);
            if (n > 0) {
                got += n;
                last = millis();
            }
        } else if (millis() - last > timeoutMs) {
            return false;
        }
    }
    return true;
}

static bool writeAndVerify(uint32_t addr, const uint8_t *buf, uint32_t len) {
    if (flash.writeBuffer(addr, buf, len) != len) return false;

    uint8_t tmp[256];
    uint32_t done = 0;
    while (done < len) {
        uint32_t n = len - done;
        if (n > sizeof(tmp)) n = sizeof(tmp);

        if (flash.readBuffer(addr + done, tmp, n) != n) return false;
        if (memcmp(tmp, buf + done, n) != 0) return false;

        done += n;
    }
    return true;
}

static bool eraseSlot(uint8_t n, bool verbose) {
    uint32_t first = slotBase(n) / SECTOR_SIZE;
    uint32_t count = SLOT_BYTES / SECTOR_SIZE;

    for (uint32_t i = 0; i < count; i++) {
        if (!flash.eraseSector(first + i)) return false;
#if VERBOSE_UPLOAD
        if (verbose && (i % 12 == 0)) {
            Serial.print(".");
            Serial.flush();
        }
#else
        (void)verbose;
#endif
    }
    return true;
}


/* ===========================================================================
 * SLOT TABLE
 * ===========================================================================
 */

static bool loadSlotTable(void) {
    uint8_t buf[16];
    memset(buf, 0, sizeof(buf));

    if (flash.readBuffer(dirOffset, buf, 16) != 16) {
        memset(slotFlags, 0, sizeof(slotFlags));
        return false;
    }

    if (memcmp(buf, DIR_MAGIC, 4) != 0) {
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

    if (!flash.eraseSector(dirOffset / SECTOR_SIZE)) return false;
    if (flash.writeBuffer(dirOffset, buf, 16) != 16) return false;

    return true;
}


/* ===========================================================================
 * MEMORY MEASUREMENT (portable, no freeHeapAvailable)
 * ===========================================================================
 */

static uint32_t largestFreeBlock(void) {
    uint32_t lo = 0;
    uint32_t hi = 296 * 1024;

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

static void listSlots(void) {
    uint8_t found = 0;

    for (uint8_t i = 0; i < MAX_IMAGES; i++) {
        if (!slotFlags[i]) continue;

        Serial.print("  [");
        Serial.print(i);
        Serial.print("] ");
        Serial.print(IMG_BYTES);
        Serial.print(" bytes at 0x");
        Serial.println(slotBase(i), HEX);

        found++;
    }

    if (!found) Serial.println("  (no images stored)");
}

static void probe(void) {
    Serial.println("--- probe ---");

    Serial.print("JEDEC ID:     0x");
    Serial.println(flash.getJEDECID(), HEX);

    Serial.print("Flash size:   ");
    Serial.print(flashSize);
    Serial.print(" bytes (");
    Serial.print(flashSize / 1024);
    Serial.println(" KB)");

    Serial.print("Sectors:      ");
    Serial.println(flash.sectorCount());

    Serial.print("Slot size:    ");
    Serial.print(SLOT_BYTES);
    Serial.print(" bytes (");
    Serial.print(SLOT_BYTES / SECTOR_SIZE);
    Serial.println(" sectors)");

    Serial.print("Slots:        ");
    Serial.print(MAX_IMAGES);
    Serial.print(" x ");
    Serial.print(IMG_BYTES);
    Serial.print(" = ");
    Serial.print((uint32_t)MAX_IMAGES * IMG_BYTES);
    Serial.println(" bytes");

    Serial.print("Dir sector:   0x");
    Serial.println(dirOffset, HEX);

    Serial.print("Chunks/img:   ");
    Serial.print(CHUNKS_PER_IMG);
    Serial.print(" x ");
    Serial.print(CHUNK_BYTES);
    Serial.println(" bytes");

    Serial.print("Largest free block: ");
    Serial.print(largestFreeBlock());
    Serial.println(" bytes");

    Serial.print("SD card:      ");
    Serial.println(sdOK ? "OK" : "not initialized");

    Serial.println("Stored images:");
    listSlots();
    Serial.println("-------------");
}

static void verifyHighOffsets(void) {
    Serial.println("--- V: high-offset write test ---");
    Serial.print("Testing in the free region above the last slot (0x");
    Serial.print(slotBase(MAX_IMAGES), HEX);
    Serial.println(")");

    const uint32_t sectors[] = { 483, 490, 497, 504 };
    uint8_t pattern[256];
    uint8_t readback[256];
    uint8_t failures = 0;

    for (uint8_t i = 0; i < sizeof(sectors) / sizeof(sectors[0]); i++) {
        uint32_t sec  = sectors[i];
        uint32_t addr = sec * SECTOR_SIZE;

        for (uint16_t j = 0; j < sizeof(pattern); j++) {
            pattern[j] = (uint8_t)(j ^ 0xA5) ^ (uint8_t)sec;
        }

        Serial.print("  sector ");
        Serial.print(sec);
        Serial.print(" (0x");
        Serial.print(addr, HEX);
        Serial.print("): ");

        if (!flash.eraseSector(sec)) {
            Serial.println("ERASE FAILED");
            failures++;
            continue;
        }

        if (flash.writeBuffer(addr, pattern, sizeof(pattern)) != sizeof(pattern)) {
            Serial.println("WRITE FAILED");
            failures++;
            continue;
        }

        if (flash.readBuffer(addr, readback, sizeof(readback)) != sizeof(readback)) {
            Serial.println("READ FAILED");
            failures++;
            continue;
        }

        if (memcmp(pattern, readback, sizeof(pattern)) != 0) {
            Serial.println("MISMATCH");
            failures++;
            continue;
        }

        Serial.println("PASS");
    }

    Serial.print("--- V: ");
    if (failures == 0) {
        Serial.println("ALL PASS - the full chip is addressable ---");
    } else {
        Serial.print(failures);
        Serial.println(" FAILURE(S) - chip is not behaving as 2 MB ---");
    }
}

static bool displayImage(uint8_t n) {
    if (n >= MAX_IMAGES) {
        Serial.println("ERR slot out of range");
        return false;
    }
    if (!slotFlags[n]) {
        Serial.println("ERR slot empty");
        return false;
    }

    Serial.print("Pushing ");
    Serial.print(CHUNKS_PER_IMG);
    Serial.print(" chunks of ");
    Serial.print(CHUNK_ROWS);
    Serial.println(" rows...");
    Serial.flush();

    uint32_t t0 = millis();

    for (uint8_t c = 0; c < CHUNKS_PER_IMG; c++) {
        uint32_t addr = slotBase(n) + (uint32_t)c * CHUNK_BYTES;

        if (flash.readBuffer(addr, chunkBuf, CHUNK_BYTES) != CHUNK_BYTES) {
            Serial.print("ERR flash read failed at chunk ");
            Serial.println(c);
            return false;
        }

        uint16_t y = (uint16_t)(c * CHUNK_ROWS);

        if (!display.pushImage4BPP(0, y, IMG_W, CHUNK_ROWS, chunkBuf, true)) {
            Serial.print("ERR push rejected at chunk ");
            Serial.print(c);
            Serial.print(" (y=");
            Serial.print(y);
            Serial.println(")");
            return false;
        }
    }

    Serial.print("All ");
    Serial.print(CHUNKS_PER_IMG);
    Serial.print(" chunks pushed in ");
    Serial.print(millis() - t0);
    Serial.println(" ms");

#if REFRESH_AT_END
    Serial.println("Refreshing panel...");
    const GfxResult refreshResult = display.refresh();
    if (!refreshResult) Serial.println(refreshResult.message);
    Serial.println("Refresh done");
#endif

    return true;
}

static bool receiveImage(uint8_t n) {
    if (n >= MAX_IMAGES) {
        Serial.println("ERR slot out of range");
        return false;
    }

    Serial.println("READY");

    uint8_t hdr[4];
    if (!readInto(hdr, 4, RX_TIMEOUT)) {
        Serial.println("ERR timeout on length");
        return false;
    }

    uint32_t len = (uint32_t)hdr[0]
                 | ((uint32_t)hdr[1] << 8)
                 | ((uint32_t)hdr[2] << 16)
                 | ((uint32_t)hdr[3] << 24);

    if (len != IMG_BYTES) {
        Serial.print("ERR bad length ");
        Serial.println(len);
        return false;
    }

    // Mark the slot unusable before touching it, so an interrupted upload
    // cannot leave a half-written image looking valid.
    if (slotFlags[n]) {
        slotFlags[n] = 0;
        saveSlotTable();
    }

#if VERBOSE_UPLOAD
    Serial.print("Erasing slot ");
    Serial.print(n);
    Serial.print(" (");
    Serial.print(SLOT_BYTES / SECTOR_SIZE);
    Serial.print(" sectors)");
    Serial.flush();
#endif

    if (!eraseSlot(n, true)) {
        Serial.println("ERR erase failed");
        return false;
    }

#if VERBOSE_UPLOAD
    Serial.println(" done");
    Serial.print("Receiving...");
    Serial.flush();
#endif

    uint32_t sum = 0;

    for (uint8_t c = 0; c < CHUNKS_PER_IMG; c++) {
        if (!readInto(chunkBuf, CHUNK_BYTES, RX_TIMEOUT)) {
            Serial.println("ERR timeout mid-payload");
            return false;
        }

        for (uint32_t i = 0; i < CHUNK_BYTES; i++) sum += chunkBuf[i];

        uint32_t addr = slotBase(n) + (uint32_t)c * CHUNK_BYTES;

        if (!writeAndVerify(addr, chunkBuf, CHUNK_BYTES)) {
            Serial.print("ERR write/verify failed at chunk ");
            Serial.print(c);
            Serial.print(" offset 0x");
            Serial.println(addr, HEX);
            return false;
        }

#if VERBOSE_UPLOAD
        Serial.print(".");
        Serial.flush();
#endif
    }

#if VERBOSE_UPLOAD
    Serial.print(" written. Checksum...");
    Serial.flush();
#endif

    uint8_t sumBytes[4];
    if (!readInto(sumBytes, 4, RX_TIMEOUT)) {
        Serial.println("ERR timeout on checksum");
        return false;
    }

    uint32_t hostSum = (uint32_t)sumBytes[0]
                     | ((uint32_t)sumBytes[1] << 8)
                     | ((uint32_t)sumBytes[2] << 16)
                     | ((uint32_t)sumBytes[3] << 24);

    if (hostSum != sum) {
        Serial.print("ERR checksum mismatch board ");
        Serial.print(sum);
        Serial.print(" host ");
        Serial.println(hostSum);
        return false;
    }

    slotFlags[n] = 1;
    if (!saveSlotTable()) {
        slotFlags[n] = 0;
        Serial.println("ERR could not commit slot table");
        return false;
    }

    Serial.print("OK ");
    Serial.print(n);
    Serial.print(" ");
    Serial.println(len);
    return true;
}

static void clearSlot(uint8_t n) {
    if (n >= MAX_IMAGES) {
        Serial.println("ERR slot out of range");
        return;
    }
    if (!slotFlags[n]) {
        Serial.println("ERR slot already empty");
        return;
    }

    slotFlags[n] = 0;
    if (saveSlotTable()) {
        Serial.println("OK cleared (image data remains in flash until reused)");
    } else {
        Serial.println("ERR could not update slot table");
    }
}

static void clearAllSlots(void) {
    Serial.println("Clearing slot table...");
    memset(slotFlags, 0, sizeof(slotFlags));

    if (saveSlotTable()) {
        Serial.println("OK all slots cleared");
        Serial.println("(image data remains in flash until a slot is reused)");
    } else {
        Serial.println("ERR could not update slot table");
    }
}


/* ===========================================================================
 * SD COMMANDS
 * ===========================================================================
 */

// Find a file in the SD root by name (case-insensitive). On success, f is
// left open and must be closed by the caller.
static bool sdFindByName(const char *name, File &f) {
    File root = sd.open("/");
    if (!root) return false;

    while (f.openNext(&root, O_RDONLY)) {
        if (f.isDir()) { f.close(); continue; }

        char nm[64];
        f.getName(nm, sizeof(nm));

        if (strcasecmp(nm, name) == 0) {
            root.close();
            return true;
        }
        f.close();
    }

    root.close();
    return false;
}

static void sdList(void) {
    if (!sdOK) {
        Serial.println("ERR SD not initialized");
        return;
    }

    File root = sd.open("/");
    if (!root) {
        Serial.println("ERR cannot open SD root");
        return;
    }

    Serial.println("SD files:");
    File f;
    uint8_t found = 0;

    while (f.openNext(&root, O_RDONLY)) {
        if (f.isDir()) { f.close(); continue; }

        char nm[64];
        f.getName(nm, sizeof(nm));
        uint32_t sz = f.fileSize();

        Serial.print("  ");
        Serial.print(nm);
        Serial.print("  ");
        Serial.print(sz);
        Serial.print(" bytes");
        Serial.println(sz == IMG_BYTES ? "  [VALID]" : "  [wrong size]");

        found++;
        f.close();
    }

    if (!found) Serial.println("  (no files)");
    root.close();
}

// C <name> <slot> : copy an SD file into a flash slot.
static bool sdCopyToSlot(const char *name, uint8_t slot) {
    if (!sdOK) {
        Serial.println("ERR SD not initialized");
        return false;
    }
    if (slot >= MAX_IMAGES) {
        Serial.println("ERR slot out of range");
        return false;
    }

    File f;
    if (!sdFindByName(name, f)) {
        Serial.print("ERR file not found: ");
        Serial.println(name);
        return false;
    }

    uint32_t sz = f.fileSize();
    if (sz != IMG_BYTES) {
        Serial.print("ERR wrong size ");
        Serial.print(sz);
        Serial.print(" (need ");
        Serial.print(IMG_BYTES);
        Serial.println(")");
        f.close();
        return false;
    }

    Serial.print("Copying ");
    Serial.print(name);
    Serial.print(" (");
    Serial.print(sz);
    Serial.print(" bytes) to slot ");
    Serial.print(slot);
    Serial.print(": erasing ");
    Serial.print(SLOT_BYTES / SECTOR_SIZE);
    Serial.println(" sectors...");
    Serial.flush();

    // Mark slot invalid first, same safety as interrupted USB uploads.
    slotFlags[slot] = 0;
    saveSlotTable();

    if (!eraseSlot(slot, false)) {
        Serial.println("ERR erase failed");
        f.close();
        return false;
    }

    uint32_t sum = 0;

    for (uint8_t c = 0; c < CHUNKS_PER_IMG; c++) {
        if (f.read(chunkBuf, CHUNK_BYTES) != (int)CHUNK_BYTES) {
            Serial.print("ERR SD read failed at chunk ");
            Serial.println(c);
            f.close();
            return false;
        }

        for (uint32_t i = 0; i < CHUNK_BYTES; i++) sum += chunkBuf[i];

        uint32_t addr = slotBase(slot) + (uint32_t)c * CHUNK_BYTES;

        if (!writeAndVerify(addr, chunkBuf, CHUNK_BYTES)) {
            Serial.print("ERR write/verify failed at chunk ");
            Serial.print(c);
            Serial.print(" offset 0x");
            Serial.println(addr, HEX);
            f.close();
            return false;
        }

#if VERBOSE_UPLOAD
        Serial.print(".");
        Serial.flush();
#endif
    }
    f.close();

    slotFlags[slot] = 1;
    if (!saveSlotTable()) {
        slotFlags[slot] = 0;
        Serial.println("ERR could not commit slot table");
        return false;
    }

    Serial.print("OK copied ");
    Serial.print(name);
    Serial.print(" to slot ");
    Serial.print(slot);
    Serial.print(" (sum ");
    Serial.print(sum);
    Serial.println(")");
    return true;
}

// CD <name> : display a file straight from the SD card.
// Streams chunk-by-chunk, no flash involvement, no slot limit.
static bool sdDisplay(const char *name) {
    if (!sdOK) {
        Serial.println("ERR SD not initialized");
        return false;
    }

    File f;
    if (!sdFindByName(name, f)) {
        Serial.print("ERR file not found: ");
        Serial.println(name);
        return false;
    }

    uint32_t sz = f.fileSize();
    if (sz != IMG_BYTES) {
        Serial.print("ERR wrong size ");
        Serial.print(sz);
        Serial.print(" (need ");
        Serial.print(IMG_BYTES);
        Serial.println(")");
        f.close();
        return false;
    }

    Serial.print("Streaming ");
    Serial.print(name);
    Serial.print(" from SD: ");
    Serial.print(CHUNKS_PER_IMG);
    Serial.print(" chunks of ");
    Serial.print(CHUNK_ROWS);
    Serial.println(" rows...");
    Serial.flush();

    uint32_t t0 = millis();

    for (uint8_t c = 0; c < CHUNKS_PER_IMG; c++) {
        if (f.read(chunkBuf, CHUNK_BYTES) != (int)CHUNK_BYTES) {
            Serial.print("ERR SD read failed at chunk ");
            Serial.println(c);
            f.close();
            return false;
        }

        uint16_t y = (uint16_t)(c * CHUNK_ROWS);

        if (!display.pushImage4BPP(0, y, IMG_W, CHUNK_ROWS, chunkBuf, true)) {
            Serial.print("ERR push rejected at chunk ");
            Serial.print(c);
            Serial.print(" (y=");
            Serial.print(y);
            Serial.println(")");
            f.close();
            return false;
        }
    }
    f.close();

    Serial.print("All ");
    Serial.print(CHUNKS_PER_IMG);
    Serial.print(" chunks streamed in ");
    Serial.print(millis() - t0);
    Serial.println(" ms");

    Serial.println("Refreshing panel...");
    const GfxResult refreshResult = display.refresh();
    if (!refreshResult) Serial.println(refreshResult.message);
    Serial.println("Refresh done");

    return true;
}


/* ===========================================================================
 * COMMAND DISPATCH
 * ===========================================================================
 */

static void handleLine(char *line) {
    int len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) {
        line[--len] = '\0';
    }
    if (len == 0) return;

    char cmd = toupper((unsigned char)line[0]);
    int  arg = atoi(line + 1);

    switch (cmd) {
        case 'P': probe(); break;

        case 'L':
            Serial.println("Stored images:");
            listSlots();
            break;

        case 'D': displayImage((uint8_t)arg); break;

        case 'X': clearSlot((uint8_t)arg); break;

        case 'F': clearAllSlots(); break;

        case 'V': verifyHighOffsets(); break;

        case 'U': receiveImage((uint8_t)arg); break;

        case 'M':
            sdList();
            break;

        case 'C':
            if (toupper((unsigned char)line[1]) == 'D') {
                // CD <name>
                const char *name = line + 2;
                while (*name == ' ') name++;
                sdDisplay(name);
            } else {
                // C <name> <slot>  - name is everything before the last space
                const char *rest = line + 1;
                while (*rest == ' ') rest++;

                const char *lastSpace = strrchr(rest, ' ');
                if (!lastSpace) {
                    Serial.println("ERR usage: C <name> <slot>");
                    break;
                }

                char nameBuf[48];
                size_t nl = (size_t)(lastSpace - rest);
                if (nl == 0 || nl >= sizeof(nameBuf)) {
                    Serial.println("ERR bad name");
                    break;
                }
                memcpy(nameBuf, rest, nl);
                nameBuf[nl] = '\0';

                int slot = atoi(lastSpace + 1);
                sdCopyToSlot(nameBuf, (uint8_t)slot);
            }
            break;

        default:
            Serial.println("ERR unknown command (P L D X F V U M C CD)");
            break;
    }
}


/* ===========================================================================
 * ARDUINO ENTRY POINTS
 * ===========================================================================
 */

void setup(void) {
    Serial.begin(115200);

    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 3000) { }

    Serial.println();
    Serial.println("ePaper gallery - raw QSPI flash + SD");
    Serial.println("Commands: P L D<n> X<n> F V U<n> M C<name> <slot> CD<name>");

    if (!flash.begin(&EPAPER_FLASH)) {
        Serial.println("CRITICAL: QSPI flash init failed");
        while (true) {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(200);
            digitalWrite(LED_BUILTIN, LOW);
            delay(200);
        }
    }

    flashSize = flash.size();
    dirOffset = flashSize - SECTOR_SIZE;

    Serial.print("Flash ready: ");
    Serial.print(flashSize);
    Serial.print(" bytes, JEDEC 0x");
    Serial.println(flash.getJEDECID(), HEX);

    if (flashSize < (uint32_t)MAX_IMAGES * SLOT_BYTES + SECTOR_SIZE) {
        Serial.println("WARNING: chip smaller than the layout assumes");
    }

    loadSlotTable();

    if (!display.begin<Board_XIAO_ePaper_EN04, Config_Seeed_ePaper_7inch3_Colorful_ED2208>()) {
        Serial.println(display.lastResult().message);
        return;
    }

    Serial.print("SD card (CS=D4): ");
    if (sd.begin(SD_CS, SD_SCK_SPEED)) {
        sdOK = true;
        Serial.println("OK");
    } else {
        Serial.println("init FAILED:");
        sd.initErrorPrint(&Serial);
    }

    probe();
}

void loop(void) {
    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\n') {
            lineBuf[lineLen] = '\0';
            handleLine(lineBuf);
            lineLen = 0;
        } else if (lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = c;
        }
    }
}
