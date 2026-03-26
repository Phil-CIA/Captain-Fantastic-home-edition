#include "displays.h"

Adafruit_7segment ledDisplay = Adafruit_7segment();

const uint16_t segmentPatterns[16] = {
    0b0111111000,
    0b0011000000,
    0b0110110100,
    0b0111100100,
    0b0011001100,
    0b0101101100,
    0b0101111100,
    0b0111000000,
    0b0111111100,
    0b0111101100,
    0b0111011100,
    0b0000111100,
    0b0100110000,
    0b0001110100,
    0b0100111100,
    0b0100011100
};

constexpr uint16_t SEGMENT_G = 0b0101111000;
constexpr uint16_t SEGMENT_O = 0b0111111000;
constexpr uint16_t SEGMENT_D = 0b0011110100;

uint8_t probeI2CAddress(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission();
}

void initDisplay() {
    const uint8_t displayProbe = probeI2CAddress(HT16K33_ADDRESS);
    const uint8_t matrixProbe = probeI2CAddress(CAPTAIN_MATRIX_I2C_ADDRESS);

    Serial.printf("I2C probe display 0x%02X: %s (err=%u)\n",
                  HT16K33_ADDRESS,
                  displayProbe == 0 ? "OK" : "MISSING",
                  displayProbe);
    Serial.printf("I2C probe matrix 0x%02X: %s (err=%u)\n",
                  CAPTAIN_MATRIX_I2C_ADDRESS,
                  matrixProbe == 0 ? "OK" : "MISSING",
                  matrixProbe);

    if (!ledDisplay.begin(HT16K33_ADDRESS)) {
        Serial.println("HT16K33 not found at 0x70");
        return;
    }

    ledDisplay.setBrightness(10);
    ledDisplay.clear();
    ledDisplay.writeDisplay();
}

void displayStartupTest() {
    for (uint8_t flash = 0; flash < 3; flash++) {
        Wire.beginTransmission(HT16K33_ADDRESS);
        Wire.write(0x00);
        for (uint8_t i = 0; i < 6; i++) {
            Wire.write(0b11111100);
            Wire.write(0b00000001);
        }
        Wire.endTransmission();
        delay(150);

        Wire.beginTransmission(HT16K33_ADDRESS);
        Wire.write(0x00);
        for (uint8_t i = 0; i < 12; i++) {
            Wire.write(0x00);
        }
        Wire.endTransmission();
        delay(150);
    }

    for (int8_t num = 9; num >= 0; num--) {
        const uint16_t pattern = segmentPatterns[num];
        Wire.beginTransmission(HT16K33_ADDRESS);
        Wire.write(0x00);
        for (uint8_t i = 0; i < 6; i++) {
            Wire.write(pattern & 0xFF);
            Wire.write((pattern >> 8) & 0xFF);
        }
        Wire.endTransmission();
        delay(80);
    }

    for (uint8_t pos = 0; pos < 6; pos++) {
        Wire.beginTransmission(HT16K33_ADDRESS);
        Wire.write(0x00);
        for (uint8_t i = 0; i < 6; i++) {
            if (i == pos) {
                Wire.write(0b11111100);
                Wire.write(0b00000001);
            } else {
                Wire.write(0x00);
                Wire.write(0x00);
            }
        }
        Wire.endTransmission();
        delay(100);
    }

    Wire.beginTransmission(HT16K33_ADDRESS);
    Wire.write(0x00);
    for (uint8_t i = 0; i < 12; i++) {
        Wire.write(0x00);
    }
    Wire.endTransmission();
}

void displayGoodMessage(uint16_t durationMs) {
    Wire.beginTransmission(HT16K33_ADDRESS);
    Wire.write(0x00);
    Wire.write(0x00);
    Wire.write(0x00);

    const uint16_t goodPatterns[4] = {SEGMENT_G, SEGMENT_O, SEGMENT_O, SEGMENT_D};
    for (uint8_t i = 0; i < 4; i++) {
        Wire.write(goodPatterns[i] & 0xFF);
        Wire.write((goodPatterns[i] >> 8) & 0xFF);
    }

    Wire.endTransmission();
    delay(durationMs);
}

void updateLEDScore(uint32_t score) {
    if (score > MAX_SCORE) {
        score = MAX_SCORE;
    }

    uint8_t digits[6] = {
        static_cast<uint8_t>((score / 100000) % 10),
        static_cast<uint8_t>((score / 10000) % 10),
        static_cast<uint8_t>((score / 1000) % 10),
        static_cast<uint8_t>((score / 100) % 10),
        static_cast<uint8_t>((score / 10) % 10),
        static_cast<uint8_t>(score % 10)
    };

    Wire.beginTransmission(HT16K33_ADDRESS);
    Wire.write(0x00);
    for (uint8_t i = 0; i < 6; i++) {
        const uint16_t pattern = segmentPatterns[digits[i]];
        Wire.write(pattern & 0xFF);
        Wire.write((pattern >> 8) & 0xFF);
    }
    Wire.endTransmission();
}

void clearDisplay() {
    ledDisplay.clear();
    ledDisplay.writeDisplay();
}

void setDisplayBrightness(uint8_t level) {
    if (level > 15) {
        level = 15;
    }
    ledDisplay.setBrightness(level);
}
