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

#include <bluefruit.h>

// Short name so it comfortably fits the 31-byte advertising packet.
#define DEVICE_NAME "EPAPER-TEST"

void setup(void) {
    Serial.begin(115200);
    uint32_t t0 = millis();
    while (!Serial && (millis() - t0) < 4000) { }

    Serial.println();
    Serial.println("=================================");
    Serial.println("  BLE MINIMAL ADVERTISING TEST");
    Serial.println("=================================");

    /* ---- 1. Bring up the SoftDevice ---- */
    // Everything except config*() calls must come AFTER this.
    bool began = Bluefruit.begin();

    Serial.print("[1] Bluefruit.begin() -> ");
    Serial.println(began ? "TRUE" : "FALSE  <-- RADIO STACK DID NOT START");

    if (!began) {
        Serial.println();
        Serial.println("If this is FALSE, the SoftDevice is not running.");
        Serial.println("Usual causes:");
        Serial.println("  - Bootloader/SoftDevice mismatch (reflash the");
        Serial.println("    Seeed nRF52 bootloader for this exact board)");
        Serial.println("  - Wrong board selected in the IDE");
        Serial.println("  - Insufficient RAM for the SoftDevice config");
        Serial.println();
        Serial.println("Halting here - nothing else can work.");
        while (true) delay(1000);
    }

    /* ---- 2. Radio parameters ---- */
    Bluefruit.setTxPower(4);
    Bluefruit.setName(DEVICE_NAME);

    char nm[40] = {0};
    Bluefruit.getName(nm, sizeof(nm));
    Serial.print("[2] Name readback: '");
    Serial.print(nm);
    Serial.println("'");

    if (strlen(nm) == 0) {
        Serial.println("    Name is EMPTY - setName() did not stick.");
    }

    /* ---- 3. Advertising payload ---- */
    Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
    Bluefruit.Advertising.addTxPower();
    Bluefruit.Advertising.addName();          // name in the MAIN packet

    Bluefruit.Advertising.restartOnDisconnect(true);
    Bluefruit.Advertising.setInterval(32, 244);   // 20 ms fast / 152.5 ms slow
    Bluefruit.Advertising.setFastTimeout(30);     // 30 s in fast mode

    /* ---- 4. Go ---- */
    Bluefruit.Advertising.start(0);           // 0 = never stop

    Serial.println("[3] Advertising.start(0) called");
    Serial.println();
    Serial.println(">>> Now scan from another device. <<<");
    Serial.println("    Linux:  bluetoothctl  ->  scan on");
    Serial.println("    Phone:  nRF Connect app (NOT Settings > Bluetooth)");
    Serial.println();
    Serial.println("Heartbeat below proves loop() is still running.");
    Serial.println("If the heartbeat stops, the board crashed.");
    Serial.println();
}

void loop(void) {
    static uint32_t last = 0;
    if (millis() - last >= 5000) {
        last = millis();
        Serial.print("[alive ");
        Serial.print(millis() / 1000);
        Serial.println("s]");
    }
    delay(50);
}
