/*
 * epaper_gallery_sd.ino  (v3 - SD only, random hourly, buttons)
 *
 * XIAO nRF52840 Plus + EN04 ePaper (7.0" Spectra 6, ED2208).
 * All images live on the microSD card as 192,000-byte packed-4bpp files
 * (800x480). The onboard QSPI flash is no longer used.
 *
 * Features
 *   - shows a random image at boot and then every AUTO_MINUTES minutes
 *   - PREV / NEXT buttons step through the files (alphabetical order)
 *   - serial protocol for the website: list / upload / download / delete
 *
 * SD wiring (unchanged): SCK D8, MOSI D10, MISO D9, CS D4, VCC 5V, GND.
 *
 * ---------------------------------------------------------------------------
 * SERIAL PROTOCOL (one command per line, '\n' terminated)
 * ---------------------------------------------------------------------------
 *   P                 probe / status
 *   M                 list files. Output:
 *                         FILE <size> <name>      (one per file)
 *                         CUR <name>              (only if something is shown)
 *                         END
 *   W <name>          upload to SD. Board: "READY\n"
 *                         host: <len u32 LE> <192000 bytes> <sum u32 LE>
 *                         board: "OK <name> <len>\n" or "ERR ...\n"
 *   G <name>          download from SD. Board: "DATA <len>\n"
 *                         then <len bytes> <sum u32 LE> then "OK <name>\n"
 *                         (or a single "ERR ...\n" line instead of DATA)
 *   R <name>          remove file from SD  -> "OK removed <name>"
 *   CD <name>         display a file
 *   N / B             next / previous image
 *   Z                 show a random image
 *   A <minutes>       set auto-change interval (0 = off), runtime only
 *
 * <sum> is the plain sum of all payload bytes, modulo 2^32.
 */

#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <Adafruit_SPIFlash.h>
#include <SdFat.h>
#include <Seeed_GFX.h>
#include "board/boards/XIAO_ePaper_Boards.h"
#include "panel/configs/Seeed_Panel_Configs.h"
#include "driver/epaper/Driver_ED2208.h"
#include "panel/Panel_EPaper.h"

// Onboard QSPI flash, used ONLY as a scratch buffer: the display and the SD
// card share one SPI bus, so an image is copied SD -> flash first, then
// flash -> display. (Named EPAPER_FLASH to avoid the P25Q16H macro clash.)
SPIFlash_Device_t const EPAPER_FLASH = {
  .total_size                  = (1UL << 21),
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
static bool flashOK = false;

Seeed_GFX display;
SdFat sd;
static bool sdOK      = false;
static bool displayOK = false;

/* ---- configuration ------------------------------------------------------ */

#define SD_CS            D4
#define SD_SCK_SPEED     SD_SCK_MHZ(1)   // 4 MHz was unreliable on the shared bus; raise to 2 after testing with T

// Buttons: wire between pin and GND (internal pull-up, active LOW).
// !! Set these to the pins your buttons are really on. They must not be
// !! pins that Seeed_GFX claims (D3, D6, D7, D8, D10) or the SD pins.
#define SD_FIRST         0      // 1 = initialize SD before the display
#define DEBUG_NO_DISPLAY 0      // 1 = skip display init (to test the SD card alone)
#define ENABLE_BUTTONS   0      // 0 = buttons fully disabled until pins are known
#define BTN_PREV_PIN     D1     // placeholder - NOT the real pins
#define BTN_NEXT_PIN     D2     // placeholder - NOT the real pins
#define DEBOUNCE_MS      30

#define AUTO_MINUTES     60

#define IMG_W            800
#define IMG_H            480
#define ROW_BYTES        (IMG_W / 2)
#define IMG_BYTES        192000UL
#define CHUNK_ROWS       20
#define CHUNK_BYTES      (CHUNK_ROWS * ROW_BYTES)       // 8000
#define CHUNKS_PER_IMG   (IMG_H / CHUNK_ROWS)           // 24

#define SECTOR_SIZE      4096UL
#define STAGE_SLOT_BYTES 196608UL     // 48 sectors per staging slot
#define STAGE_SLOTS      8            // rotate to spread flash wear (1.5 MB used)

#define RX_TIMEOUT       15000
#define MAX_FILES        256
#define NAME_LEN         48

/* ---- state -------------------------------------------------------------- */

static uint8_t  chunkBuf[CHUNK_BYTES];
static char     lineBuf[96];
static uint8_t  lineLen = 0;

static char     names[MAX_FILES][NAME_LEN];   // valid images, sorted
static int      fileCount = 0;
static int      curIdx    = -1;

static uint32_t autoMs    = (uint32_t)AUTO_MINUTES * 60000UL;
static uint32_t lastShown = 0;

struct Btn { uint8_t pin; bool stable; uint32_t t; };
static Btn btnPrev = { BTN_PREV_PIN, false, 0 };
static Btn btnNext = { BTN_NEXT_PIN, false, 0 };

/* ---- helpers ------------------------------------------------------------ */

static uint32_t le32(const uint8_t *b) {
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8)
         | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static bool readInto(uint8_t *dst, uint32_t len, uint32_t timeoutMs) {
    uint32_t got = 0, last = millis();
    while (got < len) {
        if (Serial.available()) {
            int n = Serial.readBytes(dst + got, len - got);
            if (n > 0) { got += n; last = millis(); }
        } else if (millis() - last > timeoutMs) {
            return false;
        }
    }
    return true;
}

// Strict filename check for uploads: alnum . - _ only, no paths.
static bool sanitizeName(const char *in, char *out, size_t cap) {
    const char *base = in, *s;
    while ((s = strchr(base, '/'))  != NULL) base = s + 1;
    while ((s = strchr(base, '\\')) != NULL) base = s + 1;

    size_t o = 0;
    for (const char *p = base; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (o >= cap) return false;
        if (isalnum(c) || c == '.' || c == '-' || c == '_') out[o++] = (char)c;
        else return false;
    }
    out[o] = '\0';
    if (o == 0 || out[0] == '.') return false;
    if (!strchr(out, '.')) {
        if (o + 5 > cap) return false;
        strcat(out, ".bin");
    }
    return true;
}

static const char *skipSpaces(const char *s) {
    while (*s == ' ') s++;
    return s;
}

/* ---- file index --------------------------------------------------------- */

// Rebuild names[] from the SD root: regular, non-hidden files of exactly
// IMG_BYTES. Keeps curIdx pointing at the same file if it still exists.
static void rescan(void) {
    char keep[NAME_LEN];
    keep[0] = '\0';
    if (curIdx >= 0 && curIdx < fileCount) strcpy(keep, names[curIdx]);

    fileCount = 0;
    curIdx = -1;
    if (!sdOK) return;

    File root = sd.open("/");
    if (!root) return;

    File f;
    while (f.openNext(&root, O_RDONLY)) {
        if (!f.isDir() && f.fileSize() == IMG_BYTES && fileCount < MAX_FILES) {
            char nm[NAME_LEN];
            nm[0] = '\0';
            f.getName(nm, sizeof(nm));
            // skip hidden / macOS "._" junk and over-long names
            if (nm[0] && nm[0] != '.' && strlen(nm) < NAME_LEN - 1) {
                strcpy(names[fileCount++], nm);
            }
        }
        f.close();
    }
    root.close();

    // insertion sort, case-insensitive
    for (int i = 1; i < fileCount; i++) {
        char tmp[NAME_LEN];
        strcpy(tmp, names[i]);
        int j = i - 1;
        while (j >= 0 && strcasecmp(names[j], tmp) > 0) {
            strcpy(names[j + 1], names[j]);
            j--;
        }
        strcpy(names[j + 1], tmp);
    }

    if (keep[0]) {
        for (int i = 0; i < fileCount; i++) {
            if (strcasecmp(names[i], keep) == 0) { curIdx = i; break; }
        }
    }
}

/* ---- display ------------------------------------------------------------ */

static uint8_t stageSlot = 0xFF;

static bool sdDisplay(const char *name) {
    if (!sdOK)      { Serial.println("ERR SD not initialized"); return false; }
    if (!displayOK) { Serial.println("ERR display not initialized"); return false; }
    if (!flashOK)   { Serial.println("ERR flash staging not available"); return false; }

    File f = sd.open(name, O_RDONLY);
    if (!f) {
        Serial.print("ERR file not found: ");
        Serial.println(name);
        return false;
    }
    if (f.fileSize() != IMG_BYTES) {
        Serial.print("ERR wrong size ");
        Serial.println(f.fileSize());
        f.close();
        return false;
    }

    uint32_t t0 = millis();

    // Phase 1: SD -> flash. The SD card is fully finished before the display
    // is touched, because both share the same SPI bus.
    stageSlot = (stageSlot >= STAGE_SLOTS) ? (uint8_t)random(STAGE_SLOTS)
                                           : (uint8_t)((stageSlot + 1) % STAGE_SLOTS);
    uint32_t base = (uint32_t)stageSlot * STAGE_SLOT_BYTES;
    uint32_t firstSector = base / SECTOR_SIZE;

    for (uint32_t i = 0; i < STAGE_SLOT_BYTES / SECTOR_SIZE; i++) {
        if (!flash.eraseSector(firstSector + i)) {
            Serial.println("ERR flash erase failed");
            f.close();
            return false;
        }
    }
    for (uint8_t c = 0; c < CHUNKS_PER_IMG; c++) {
        if (f.read(chunkBuf, CHUNK_BYTES) != (int)CHUNK_BYTES) {
            Serial.print("ERR SD read failed at chunk ");
            Serial.println(c);
            f.close();
            return false;
        }
        if (flash.writeBuffer(base + (uint32_t)c * CHUNK_BYTES, chunkBuf, CHUNK_BYTES) != CHUNK_BYTES) {
            Serial.print("ERR flash write failed at chunk ");
            Serial.println(c);
            f.close();
            return false;
        }
    }
    f.close();

    // Phase 2: flash -> display. No SD access from here on.
    for (uint8_t c = 0; c < CHUNKS_PER_IMG; c++) {
        if (flash.readBuffer(base + (uint32_t)c * CHUNK_BYTES, chunkBuf, CHUNK_BYTES) != CHUNK_BYTES) {
            Serial.print("ERR flash read failed at chunk ");
            Serial.println(c);
            return false;
        }
        uint16_t y = (uint16_t)(c * CHUNK_ROWS);
        if (!display.pushImage4BPP(0, y, IMG_W, CHUNK_ROWS, chunkBuf, true)) {
            Serial.print("ERR push rejected at chunk ");
            Serial.println(c);
            return false;
        }
    }

    const GfxResult r = display.refresh();
    if (!r) Serial.println(r.message);

    lastShown = millis();
    Serial.print("OK shown ");
    Serial.print(name);
    Serial.print(" in ");
    Serial.print(millis() - t0);
    Serial.println(" ms");
    return true;
}

static void showIndex(int i) {
    if (fileCount == 0) { Serial.println("ERR no images on SD"); return; }
    i = ((i % fileCount) + fileCount) % fileCount;
    if (sdDisplay(names[i])) curIdx = i;
}

static void showNext(void) { showIndex(curIdx < 0 ? 0 : curIdx + 1); }
static void showPrev(void) { showIndex(curIdx < 0 ? fileCount - 1 : curIdx - 1); }

static void showRandom(void) {
    if (fileCount == 0) { Serial.println("ERR no images on SD"); return; }
    int i = 0;
    if (fileCount > 1) {
        do { i = (int)random(fileCount); } while (i == curIdx);
    }
    showIndex(i);
}

/* ---- SD commands -------------------------------------------------------- */

// Print the card's low-level error state (helps tell wiring/power from format).
static void sdDiag(File *f) {
    Serial.print("  SD error code 0x");
    Serial.print(sd.sdErrorCode(), HEX);
    Serial.print(" data 0x");
    Serial.println(sd.sdErrorData(), HEX);
    if (f) {
        Serial.print("  file write error flag: ");
        Serial.println(f->getWriteError());
    }
}

// One write attempt at the currently configured SD settings.
static bool sdWriteOnce(void) {
    File f = sd.open("wtest.tmp", O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) {
        Serial.println("    create FAILED");
        sdDiag(NULL);
        return false;
    }
    memset(chunkBuf, 0xA5, sizeof(chunkBuf));
    size_t a = f.write(chunkBuf, 512);
    size_t b = f.write(chunkBuf, CHUNK_BYTES);
    bool synced = f.sync();
    Serial.print("    write 512 -> "); Serial.print(a);
    Serial.print(", write 8000 -> "); Serial.print(b);
    Serial.print(", sync "); Serial.println(synced ? "ok" : "FAILED");
    bool ok = (a == 512 && b == CHUNK_BYTES && synced);
    if (!ok) sdDiag(&f);
    f.close();
    sd.remove("wtest.tmp");
    return ok;
}

// T : write test, repeated at several SPI speeds, then restores the normal one.
static void sdWriteTest(void) {
    if (!sdOK) { Serial.println("ERR SD not initialized"); return; }

    Serial.print("T: card type ");
    Serial.print(sd.card()->type());
    Serial.print(", size ");
    Serial.print((uint32_t)(sd.card()->sectorCount() / 2048));
    Serial.println(" MB");

    const uint8_t mhz[] = { 4, 2, 1 };
    bool any = false;
    for (uint8_t i = 0; i < sizeof(mhz); i++) {
        Serial.print("T: ");
        Serial.print(mhz[i]);
        Serial.println(" MHz:");
        if (!sd.begin(SD_CS, SD_SCK_MHZ(mhz[i]))) {
            Serial.println("    init FAILED");
            sdDiag(NULL);
            continue;
        }
        if (sdWriteOnce()) any = true;
    }

    // restore normal configuration
    sdOK = sd.begin(SD_CS, SD_SCK_SPEED);
    Serial.println(any ? "OK T pass (at least one speed works)" : "ERR T write failed at all speeds");
}

static void sdList(void) {
    if (!sdOK) { Serial.println("ERR SD not initialized"); return; }

    File root = sd.open("/");
    if (!root) { Serial.println("ERR cannot open SD root"); return; }

    File f;
    while (f.openNext(&root, O_RDONLY)) {
        if (!f.isDir()) {
            char nm[64];
            nm[0] = '\0';
            f.getName(nm, sizeof(nm));
            if (nm[0] && nm[0] != '.') {
                Serial.print("FILE ");
                Serial.print(f.fileSize());
                Serial.print(" ");
                Serial.println(nm);
            }
        }
        f.close();
    }
    root.close();

    if (curIdx >= 0 && curIdx < fileCount) {
        Serial.print("CUR ");
        Serial.println(names[curIdx]);
    }
    Serial.println("END");
}

static void sdRemove(const char *name) {
    if (!sdOK) { Serial.println("ERR SD not initialized"); return; }
    if (!*name) { Serial.println("ERR usage: R <name>"); return; }

    if (!sd.exists(name)) {
        Serial.print("ERR file not found: ");
        Serial.println(name);
        return;
    }
    if (!sd.remove(name)) {
        Serial.println("ERR remove failed");
        return;
    }
    rescan();
    Serial.print("OK removed ");
    Serial.println(name);
}

static void sdSend(const char *name) {
    if (!sdOK) { Serial.println("ERR SD not initialized"); return; }

    File f = sd.open(name, O_RDONLY);
    if (!f) {
        Serial.print("ERR file not found: ");
        Serial.println(name);
        return;
    }
    uint32_t sz = f.fileSize();
    if (sz != IMG_BYTES) {
        Serial.print("ERR wrong size ");
        Serial.println(sz);
        f.close();
        return;
    }

    Serial.print("DATA ");
    Serial.println(sz);
    Serial.flush();

    uint32_t sum = 0;
    for (uint8_t c = 0; c < CHUNKS_PER_IMG; c++) {
        if (f.read(chunkBuf, CHUNK_BYTES) != (int)CHUNK_BYTES) {
            f.close();
            return;   // host will time out / see bad checksum
        }
        for (uint32_t i = 0; i < CHUNK_BYTES; i++) sum += chunkBuf[i];
        Serial.write(chunkBuf, CHUNK_BYTES);
    }
    f.close();

    uint8_t sb[4] = { (uint8_t)sum, (uint8_t)(sum >> 8),
                      (uint8_t)(sum >> 16), (uint8_t)(sum >> 24) };
    Serial.write(sb, 4);
    Serial.flush();
    Serial.print("OK ");
    Serial.println(name);
}

static bool sdReceive(const char *rawName) {
    if (!sdOK) { Serial.println("ERR SD not initialized"); return false; }

    char name[NAME_LEN];
    if (!sanitizeName(rawName, name, sizeof(name) - 1)) {
        Serial.print("ERR bad name: ");
        Serial.println(rawName);
        return false;
    }

    Serial.println("READY");

    uint8_t hdr[4];
    if (!readInto(hdr, 4, RX_TIMEOUT)) {
        Serial.println("ERR timeout on length");
        return false;
    }
    uint32_t len = le32(hdr);
    if (len != IMG_BYTES) {
        Serial.print("ERR bad length ");
        Serial.println(len);
        return false;
    }

    File f = sd.open(name, O_WRONLY | O_CREAT | O_TRUNC);
    if (!f) {
        Serial.print("ERR cannot create ");
        Serial.println(name);
        return false;
    }

    uint32_t sum = 0;
    for (uint8_t c = 0; c < CHUNKS_PER_IMG; c++) {
        if (!readInto(chunkBuf, CHUNK_BYTES, RX_TIMEOUT)) {
            Serial.println("ERR timeout mid-payload");
            f.close(); sd.remove(name);
            return false;
        }
        for (uint32_t i = 0; i < CHUNK_BYTES; i++) sum += chunkBuf[i];

        if (f.write(chunkBuf, CHUNK_BYTES) != CHUNK_BYTES) {
            Serial.print("ERR SD write failed at chunk ");
            Serial.println(c);
            sdDiag(&f);
            f.close(); sd.remove(name);
            return false;
        }
    }
    f.close();

    uint8_t sb[4];
    if (!readInto(sb, 4, RX_TIMEOUT)) {
        Serial.println("ERR timeout on checksum");
        sd.remove(name);
        return false;
    }
    if (le32(sb) != sum) {
        Serial.print("ERR checksum mismatch board ");
        Serial.print(sum);
        Serial.print(" host ");
        Serial.println(le32(sb));
        sd.remove(name);
        return false;
    }

    rescan();
    Serial.print("OK ");
    Serial.print(name);
    Serial.print(" ");
    Serial.println(len);
    return true;
}

static void probe(void) {
    Serial.println("--- probe ---");
    Serial.print("SD card:    "); Serial.println(sdOK ? "OK" : "not initialized");
    Serial.print("Display:    "); Serial.println(displayOK ? "OK" : "not initialized");
    Serial.print("Flash buf:  "); Serial.println(flashOK ? "OK" : "not initialized");
    Serial.print("Images:     "); Serial.println(fileCount);
    Serial.print("Current:    ");
    Serial.println(curIdx >= 0 ? names[curIdx] : "(none)");
    Serial.print("Auto every: ");
    Serial.print(autoMs / 60000UL);
    Serial.println(" min (0 = off)");
    Serial.println("-------------");
}

/* ---- command dispatch --------------------------------------------------- */

static void handleLine(char *line) {
    int len = strlen(line);
    while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) line[--len] = '\0';
    if (len == 0) return;

    // split into command word + argument (rest of line)
    char *arg = line;
    while (*arg && *arg != ' ') arg++;
    if (*arg) *arg++ = '\0';
    arg = (char *)skipSpaces(arg);

    if      (!strcasecmp(line, "P"))  probe();
    else if (!strcasecmp(line, "M"))  sdList();
    else if (!strcasecmp(line, "T"))  sdWriteTest();
    else if (!strcasecmp(line, "W"))  sdReceive(arg);
    else if (!strcasecmp(line, "G"))  sdSend(arg);
    else if (!strcasecmp(line, "R"))  sdRemove(arg);
    else if (!strcasecmp(line, "CD")) {
        if (sdDisplay(arg)) {
            for (int i = 0; i < fileCount; i++)
                if (!strcasecmp(names[i], arg)) { curIdx = i; break; }
        }
    }
    else if (!strcasecmp(line, "N"))  showNext();
    else if (!strcasecmp(line, "B"))  showPrev();
    else if (!strcasecmp(line, "Z"))  showRandom();
    else if (!strcasecmp(line, "A")) {
        autoMs = (uint32_t)atoi(arg) * 60000UL;
        lastShown = millis();
        Serial.print("OK auto ");
        Serial.println(atoi(arg));
    }
    else Serial.println("ERR unknown command (P M W G R CD N B Z A)");
}

/* ---- buttons ------------------------------------------------------------ */

// true once per press (on the press edge), debounced
static bool btnPressed(Btn &b) {
    bool down = (digitalRead(b.pin) == LOW);
    if (down != b.stable && (millis() - b.t) > DEBOUNCE_MS) {
        b.t = millis();
        b.stable = down;
        return down;
    }
    return false;
}

/* ---- Arduino entry points ----------------------------------------------- */

// Boot-stage LED codes: blink n times. Works without any serial connection.
//   1 = setup() reached   2 = display init done   3 = SD init done
//   4 = file scan done (setup finished)
#define DEBUG_BOOT 1
#ifndef LED_STATE_ON
#define LED_STATE_ON LOW          // XIAO nRF52840 LED is active-low
#endif
static void stage(uint8_t n) {
#if DEBUG_BOOT
    for (uint8_t i = 0; i < n; i++) {
        digitalWrite(LED_BUILTIN, LED_STATE_ON);  delay(150);
        digitalWrite(LED_BUILTIN, !LED_STATE_ON); delay(150);
    }
    delay(600);
#else
    (void)n;
#endif
}

static void sdInit(void) {
    if (sd.begin(SD_CS, SD_SCK_SPEED)) {
        sdOK = true;
        Serial.println("SD: OK");
    } else {
        Serial.println("SD: init FAILED");
        sd.initErrorPrint(&Serial);
    }
}

void setup(void) {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, !LED_STATE_ON);
    stage(1);

    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 3000) { }

#if ENABLE_BUTTONS
    pinMode(BTN_PREV_PIN, INPUT_PULLUP);
    pinMode(BTN_NEXT_PIN, INPUT_PULLUP);
#endif

    // Seed RNG from timing jitter (no analog pins touched).
    randomSeed(micros() ^ (millis() << 7));

    Serial.println();
    Serial.println("ePaper gallery - SD only");

    if (flash.begin(&EPAPER_FLASH)) {
        flashOK = true;
    } else {
        Serial.println("WARNING: QSPI flash init failed - display staging unavailable");
    }

#if SD_FIRST
    sdInit();
#endif

#if DEBUG_NO_DISPLAY
    Serial.println("DEBUG_NO_DISPLAY: display init skipped");
#else
    if (display.begin<Board_XIAO_ePaper_EN04, Config_Seeed_ePaper_7inch3_Colorful_ED2208>()) {
        displayOK = true;
    } else {
        Serial.println(display.lastResult().message);
    }
#endif
    stage(2);

#if !SD_FIRST
    sdInit();
#endif

    stage(3);
    rescan();
    stage(4);
    probe();

    // The boot image is shown from loop() a few seconds later, so the serial
    // port stays responsive and the banner/probe above can be read first.
    lastShown = millis();
}

static bool bootShown = false;
static bool wasConnected = false;

void loop(void) {
    // A reset drops the USB serial port, so anything printed in setup() is
    // usually missed. Re-announce whenever a host (re)connects.
    bool conn = (bool)Serial;
    if (conn && !wasConnected) {
        delay(100);
        Serial.println();
        Serial.println("ePaper gallery - SD only (host connected)");
        probe();
    }
    wasConnected = conn;

    // boot image, deferred so serial is usable first
    if (!bootShown && millis() > 5000) {
        bootShown = true;
        if (displayOK && fileCount > 0) { showRandom(); lastShown = millis(); }
    }

    // serial commands
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

    // buttons
#if ENABLE_BUTTONS
    if (btnPressed(btnNext))      showNext();
    else if (btnPressed(btnPrev)) showPrev();
#endif

    // random image on a timer (unsigned subtraction survives millis() rollover)
    if (autoMs && displayOK && (millis() - lastShown) >= autoMs) {
        showRandom();
        lastShown = millis();   // also advance if the show failed, avoid retry storm
    }
}
