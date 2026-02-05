/**
 * displays_oled.cpp
 * 
 * SSD1306 OLED Display System Implementation
 * Renders vintage-style slanted 7-segment characters as pixel bitmaps
 * 
 * Architecture:
 * - 5 SSD1306 displays (128x64 each) = 640x64 pixel canvas
 * - 6 characters per row @ 74px wide + 33px spacing
 * - Virtual pixel buffer rendered to physical displays via I2C mux
 * - Custom segment rendering mimics Lumex LDS-C814 style
 */

#include "displays.h"
#include <string.h>

// ============= GLOBAL VARIABLES =============

// Pixel buffer - 640 × 64 pixels = 5120 bytes (monochrome)
uint8_t pixelBuffer[BUFFER_SIZE_BYTES];

// Display objects (allocated dynamically)
Adafruit_SSD1306* displays[NUM_DISPLAYS] = {nullptr};

// Display mapping configuration
DisplayMapping displayMap[NUM_DISPLAYS];

// Display state
DisplayState displayState = {
    .player1Score = 0,
    .player2Score = 0,
    .player3Score = 0,
    .player4Score = 0,
    .currentPlayer = 1,
    .currentBall = 1,
    .bonusValue = 0,
    .needsUpdate = true,
    .brightness = DISPLAY_BRIGHTNESS_DEFAULT,
    .activeRow = ROW_PLAYER1
};

// Character X positions
const uint16_t CHAR_X_POSITIONS[NUM_CHARS_PER_ROW] = {
    CHAR_POS_0, CHAR_POS_1, CHAR_POS_2,
    CHAR_POS_3, CHAR_POS_4, CHAR_POS_5
};

// ============= INITIALIZATION =============

/**
 * Configure display mapping - which pixels each display shows
 */
void configureDisplayMapping() {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        displayMap[i].startX = i * SCREEN_WIDTH;
        displayMap[i].width = SCREEN_WIDTH;
        displayMap[i].displayIndex = i;
    }
    
    Serial.println("Display mapping configured:");
    printDisplayMapping();
}

/**
 * Initialize a single SSD1306 display
 */
bool initSingleDisplay(uint8_t displayNum) {
    if (displayNum >= NUM_DISPLAYS) {
        return false;
    }
    
    // Select multiplexer channel
    if (!selectMuxChannel(displayNum)) {
        Serial.print("ERROR: Cannot select channel ");
        Serial.println(displayNum);
        return false;
    }
    
    // Allocate display object if not already done
    if (displays[displayNum] == nullptr) {
        displays[displayNum] = new Adafruit_SSD1306(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
    }
    
    // Initialize SSD1306
    if (!displays[displayNum]->begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDRESS)) {
        Serial.print("ERROR: SSD1306 allocation failed for display ");
        Serial.println(displayNum);
        disableMuxChannels();
        return false;
    }
    
    // Configure display
    displays[displayNum]->clearDisplay();
    displays[displayNum]->setTextColor(SSD1306_WHITE);
    displays[displayNum]->display();
    
    disableMuxChannels();
    
    Serial.print("Display ");
    Serial.print(displayNum);
    Serial.println(" initialized");
    
    return true;
}

/**
 * Initialize all displays and system
 */
void initDisplays() {
    Serial.println("\n=== Initializing OLED Display System ===");
    
    // Initialize I2C
    Wire.begin(21, 22);  // SDA=21, SCL=22 for ESP32
    Wire.setClock(400000);  // 400kHz fast mode
    
    // Initialize multiplexer
    initMux();
    
    // Configure display mapping
    configureDisplayMapping();
    
    // Initialize each display
    uint8_t successCount = 0;
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        if (initSingleDisplay(i)) {
            successCount++;
        }
        delay(100);
    }
    
    Serial.print("Initialized ");
    Serial.print(successCount);
    Serial.print(" of ");
    Serial.print(NUM_DISPLAYS);
    Serial.println(" displays");
    
    // Clear pixel buffer
    clearPixelBuffer();
    
    // Initialize display state
    displayState.needsUpdate = true;
    
    Serial.println("=== Display System Ready ===\n");
}

// ============= PIXEL BUFFER OPERATIONS =============

/**
 * Clear the entire pixel buffer
 */
void clearPixelBuffer() {
    memset(pixelBuffer, 0, BUFFER_SIZE_BYTES);
}

/**
 * Set a single pixel in the buffer
 * 
 * @param x X coordinate (0-639)
 * @param y Y coordinate (0-63)
 * @param color true=white/on, false=black/off
 */
void setPixel(int16_t x, int16_t y, bool color) {
    if (x < 0 || x >= BUFFER_WIDTH || y < 0 || y >= BUFFER_HEIGHT) {
        return;  // Out of bounds
    }
    
    // Calculate byte position and bit position
    // Buffer is organized as horizontal rows
    uint16_t byteIndex = (y / 8) * BUFFER_WIDTH + x;
    uint8_t bitMask = 1 << (y % 8);
    
    if (color) {
        pixelBuffer[byteIndex] |= bitMask;   // Set bit
    } else {
        pixelBuffer[byteIndex] &= ~bitMask;  // Clear bit
    }
}

/**
 * Get pixel state from buffer
 */
bool getPixel(int16_t x, int16_t y) {
    if (x < 0 || x >= BUFFER_WIDTH || y < 0 || y >= BUFFER_HEIGHT) {
        return false;
    }
    
    uint16_t byteIndex = (y / 8) * BUFFER_WIDTH + x;
    uint8_t bitMask = 1 << (y % 8);
    
    return (pixelBuffer[byteIndex] & bitMask) != 0;
}

/**
 * Draw a horizontal line
 */
void drawHorizontalLine(int16_t x, int16_t y, int16_t length, bool color) {
    for (int16_t i = 0; i < length; i++) {
        setPixel(x + i, y, color);
    }
}

/**
 * Draw a vertical line
 */
void drawVerticalLine(int16_t x, int16_t y, int16_t length, bool color) {
    for (int16_t i = 0; i < length; i++) {
        setPixel(x, y + i, color);
    }
}

/**
 * Draw a thick slanted line (for segment rendering)
 * Uses Bresenham's line algorithm with thickness
 */
void drawSlantedLine(int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t thickness, bool color) {
    int16_t dx = abs(x2 - x1);
    int16_t dy = abs(y2 - y1);
    int16_t sx = (x1 < x2) ? 1 : -1;
    int16_t sy = (y1 < y2) ? 1 : -1;
    int16_t err = dx - dy;
    
    int16_t x = x1;
    int16_t y = y1;
    
    while (true) {
        // Draw thickness around current point
        for (int8_t ty = -(thickness/2); ty <= (thickness/2); ty++) {
            for (int8_t tx = -(thickness/2); tx <= (thickness/2); tx++) {
                setPixel(x + tx, y + ty, color);
            }
        }
        
        if (x == x2 && y == y2) break;
        
        int16_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

// ============= SEGMENT RENDERING =============

/**
 * Render a single 7-segment segment with slant
 * 
 * Segment layout with slant:
 *       AAAA
 *      F    B
 *      F    B
 *       GGGG
 *      E    C
 *      E    C
 *       DDDD
 * 
 * All segments have ~10-12° italic slant
 */
void renderSegment(uint8_t* buffer, uint16_t bufferWidth, int16_t x, int16_t y, 
                   char segType, const SegmentPattern& pattern) {
    // Segment dimensions
    const uint8_t SEG_LENGTH = 28;     // Length of horizontal segments
    const uint8_t SEG_HEIGHT = 24;     // Height of vertical segments
    const uint8_t SEG_THICKNESS = 10;  // Thickness of segments
    const uint8_t SLANT = 6;           // Horizontal offset for slant
    
    bool drawSeg = false;
    
    switch (segType) {
        case 'A':  // Top horizontal
            if (pattern.segA) {
                // Slanted horizontal top segment
                for (int16_t i = 0; i < SEG_LENGTH; i++) {
                    for (int16_t t = 0; t < SEG_THICKNESS; t++) {
                        setPixel(x + SLANT + i, y + t, true);
                    }
                }
            }
            break;
            
        case 'B':  // Top right vertical (slanted)
            if (pattern.segB) {
                for (int16_t i = 0; i < SEG_HEIGHT; i++) {
                    int16_t xOffset = SLANT - (i * SLANT / SEG_HEIGHT);
                    for (int16_t t = 0; t < SEG_THICKNESS; t++) {
                        setPixel(x + SEG_LENGTH + xOffset + t, y + i, true);
                    }
                }
            }
            break;
            
        case 'C':  // Bottom right vertical (slanted)
            if (pattern.segC) {
                for (int16_t i = 0; i < SEG_HEIGHT; i++) {
                    int16_t xOffset = -(i * SLANT / SEG_HEIGHT);
                    for (int16_t t = 0; t < SEG_THICKNESS; t++) {
                        setPixel(x + SEG_LENGTH + xOffset + t, y + SEG_HEIGHT + SEG_THICKNESS + i, true);
                    }
                }
            }
            break;
            
        case 'D':  // Bottom horizontal
            if (pattern.segD) {
                for (int16_t i = 0; i < SEG_LENGTH; i++) {
                    for (int16_t t = 0; t < SEG_THICKNESS; t++) {
                        setPixel(x + i, y + 2 * SEG_HEIGHT + SEG_THICKNESS + t, true);
                    }
                }
            }
            break;
            
        case 'E':  // Bottom left vertical (slanted)
            if (pattern.segE) {
                for (int16_t i = 0; i < SEG_HEIGHT; i++) {
                    int16_t xOffset = -(i * SLANT / SEG_HEIGHT);
                    for (int16_t t = 0; t < SEG_THICKNESS; t++) {
                        setPixel(x + xOffset + t, y + SEG_HEIGHT + SEG_THICKNESS + i, true);
                    }
                }
            }
            break;
            
        case 'F':  // Top left vertical (slanted)
            if (pattern.segF) {
                for (int16_t i = 0; i < SEG_HEIGHT; i++) {
                    int16_t xOffset = SLANT - (i * SLANT / SEG_HEIGHT);
                    for (int16_t t = 0; t < SEG_THICKNESS; t++) {
                        setPixel(x + xOffset + t, y + i, true);
                    }
                }
            }
            break;
            
        case 'G':  // Middle horizontal
            if (pattern.segG) {
                for (int16_t i = 0; i < SEG_LENGTH; i++) {
                    for (int16_t t = 0; t < SEG_THICKNESS; t++) {
                        setPixel(x + SLANT/2 + i, y + SEG_HEIGHT + t, true);
                    }
                }
            }
            break;
    }
}

/**
 * Render a complete character at specified position using segment patterns
 */
void renderCharacter(uint8_t* buffer, uint16_t bufferWidth, int16_t x, int16_t y, 
                     uint8_t patternIndex) {
    if (patternIndex >= sizeof(CHAR_PATTERNS) / sizeof(CHAR_PATTERNS[0])) {
        return;  // Invalid pattern
    }
    
    const SegmentPattern& pattern = CHAR_PATTERNS[patternIndex];
    
    // Render all 7 segments
    renderSegment(buffer, bufferWidth, x, y, 'A', pattern);
    renderSegment(buffer, bufferWidth, x, y, 'B', pattern);
    renderSegment(buffer, bufferWidth, x, y, 'C', pattern);
    renderSegment(buffer, bufferWidth, x, y, 'D', pattern);
    renderSegment(buffer, bufferWidth, x, y, 'E', pattern);
    renderSegment(buffer, bufferWidth, x, y, 'F', pattern);
    renderSegment(buffer, bufferWidth, x, y, 'G', pattern);
    
    // TODO: Decimal point rendering if needed
}

// ============= CHARACTER RENDERING API =============

/**
 * Render a character at one of the 6 positions (0-5)
 */
void renderCharAtPosition(uint8_t charPos, char c) {
    if (charPos >= NUM_CHARS_PER_ROW) {
        return;
    }
    
    uint8_t patternIndex = charToPatternIndex(c);
    int16_t x = CHAR_X_POSITIONS[charPos];
    int16_t y = 2;  // Top margin
    
    renderCharacter(pixelBuffer, BUFFER_WIDTH, x, y, patternIndex);
}

/**
 * Render a digit (0-9) at position
 */
void renderDigit(uint8_t charPos, uint8_t digit) {
    if (digit > 9) {
        digit = 0;
    }
    renderCharAtPosition(charPos, '0' + digit);
}

/**
 * Render a 6-digit number across all positions
 */
void renderNumber(uint32_t number, bool leadingZeros) {
    if (number > MAX_SCORE) {
        number = MAX_SCORE;
    }
    
    // Extract digits from right to left
    for (int8_t i = NUM_CHARS_PER_ROW - 1; i >= 0; i--) {
        uint8_t digit = number % 10;
        
        if (number == 0 && i < NUM_CHARS_PER_ROW - 1 && !leadingZeros) {
            renderCharAtPosition(i, ' ');  // Blank leading zeros
        } else {
            renderDigit(i, digit);
        }
        
        number /= 10;
    }
}

/**
 * Render text string starting at position
 */
void renderText(const char* text, uint8_t startPos) {
    uint8_t len = strlen(text);
    for (uint8_t i = 0; i < len && (startPos + i) < NUM_CHARS_PER_ROW; i++) {
        renderCharAtPosition(startPos + i, text[i]);
    }
}

// ============= DISPLAY UPDATE =============

/**
 * Update a single physical display from its region of the pixel buffer
 */
void updateSingleDisplay(uint8_t displayNum) {
    if (displayNum >= NUM_DISPLAYS || displays[displayNum] == nullptr) {
        return;
    }
    
    // Select multiplexer channel
    if (!selectMuxChannel(displayNum)) {
        return;
    }
    
    DisplayMapping& map = displayMap[displayNum];
    
    // Clear display buffer
    displays[displayNum]->clearDisplay();
    
    // Copy pixels from virtual buffer to display buffer
    // SSD1306 buffer is 128x64, organized as 8 pages of 8 pixels each
    for (uint16_t x = 0; x < map.width; x++) {
        for (uint8_t y = 0; y < BUFFER_HEIGHT; y++) {
            bool pixel = getPixel(map.startX + x, y);
            if (pixel) {
                displays[displayNum]->drawPixel(x, y, SSD1306_WHITE);
            }
        }
    }
    
    // Update physical display
    displays[displayNum]->display();
    
    disableMuxChannels();
}

/**
 * Update all 5 physical displays from pixel buffer
 */
void updateAllDisplays() {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        updateSingleDisplay(i);
        delay(5);  // Small delay between displays
    }
    
    displayState.needsUpdate = false;
}

/**
 * Display a specific row (future: for row multiplexing)
 */
void displayRow(uint8_t row) {
    if (row >= TOTAL_ROWS) {
        return;
    }
    
    displayState.activeRow = row;
    // For now, just update all displays
    // Future: implement row multiplexing if needed
    updateAllDisplays();
}

// ============= SCORE MANAGEMENT =============

/**
 * Set player score and render it
 */
void setPlayerScore(uint8_t player, uint32_t score) {
    if (score > MAX_SCORE) {
        score = MAX_SCORE;
    }
    
    switch (player) {
        case 1: displayState.player1Score = score; break;
        case 2: displayState.player2Score = score; break;
        case 3: displayState.player3Score = score; break;
        case 4: displayState.player4Score = score; break;
        default: return;
    }
    
    displayState.needsUpdate = true;
}

/**
 * Add points to player score
 */
void addToPlayerScore(uint8_t player, uint32_t points) {
    uint32_t currentScore = 0;
    
    switch (player) {
        case 1: currentScore = displayState.player1Score; break;
        case 2: currentScore = displayState.player2Score; break;
        case 3: currentScore = displayState.player3Score; break;
        case 4: currentScore = displayState.player4Score; break;
        default: return;
    }
    
    setPlayerScore(player, currentScore + points);
}

/**
 * Display specific player's score
 */
void displayPlayerScore(uint8_t player) {
    clearPixelBuffer();
    
    uint32_t score = 0;
    switch (player) {
        case 1: score = displayState.player1Score; break;
        case 2: score = displayState.player2Score; break;
        case 3: score = displayState.player3Score; break;
        case 4: score = displayState.player4Score; break;
        default: return;
    }
    
    renderNumber(score, false);
    updateAllDisplays();
}

/**
 * Display all player scores (future: cycle through or multiplex)
 */
void displayAllScores() {
    // For now, display player 1
    // Future: implement cycling or multiplexing
    displayPlayerScore(displayState.currentPlayer);
}

// ============= STATUS DISPLAY =============

void setCurrentPlayer(uint8_t player) {
    if (player >= 1 && player <= 4) {
        displayState.currentPlayer = player;
        displayState.needsUpdate = true;
    }
}

void setBallNumber(uint8_t ball) {
    displayState.currentBall = ball;
    displayState.needsUpdate = true;
}

void setBonusValue(uint16_t bonus) {
    displayState.bonusValue = bonus;
    displayState.needsUpdate = true;
}

/**
 * Update status display (Player/Ball/Bonus)
 * Format: "P1 b3" or similar
 */
void updateStatusDisplay() {
    clearPixelBuffer();
    
    char statusText[7];
    sprintf(statusText, "P%d b%d", displayState.currentPlayer, displayState.currentBall);
    renderText(statusText, 0);
    
    updateAllDisplays();
}

// ============= DISPLAY CONTROL =============

void setDisplayBrightness(uint8_t brightness) {
    displayState.brightness = brightness;
    
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        if (displays[i] != nullptr) {
            selectMuxChannel(i);
            // SSD1306 doesn't have direct brightness, use contrast
            displays[i]->dim(brightness < 128);
            disableMuxChannels();
        }
    }
}

void setDisplayContrast(uint8_t contrast) {
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        if (displays[i] != nullptr) {
            selectMuxChannel(i);
            displays[i]->ssd1306_command(SSD1306_SETCONTRAST);
            displays[i]->ssd1306_command(contrast);
            disableMuxChannels();
        }
    }
}

void clearAllDisplays() {
    clearPixelBuffer();
    updateAllDisplays();
    
    displayState.player1Score = 0;
    displayState.player2Score = 0;
    displayState.player3Score = 0;
    displayState.player4Score = 0;
    displayState.currentPlayer = 1;
    displayState.currentBall = 1;
    displayState.bonusValue = 0;
}

void clearDisplayRow(uint8_t row) {
    if (row >= TOTAL_ROWS) {
        return;
    }
    
    clearPixelBuffer();
    updateAllDisplays();
}

// ============= DEBUG / TEST FUNCTIONS =============

void printDisplayMapping() {
    Serial.println("=== Display Mapping ===");
    for (uint8_t i = 0; i < NUM_DISPLAYS; i++) {
        Serial.print("Display ");
        Serial.print(i);
        Serial.print(": X=");
        Serial.print(displayMap[i].startX);
        Serial.print("-");
        Serial.print(displayMap[i].startX + displayMap[i].width - 1);
        Serial.print(" (");
        Serial.print(displayMap[i].width);
        Serial.println("px)");
    }
    Serial.println("=======================");
}

void testSegmentRendering() {
    Serial.println("Testing segment rendering...");
    
    clearPixelBuffer();
    
    // Test all segments of digit '8' at position 2 (center)
    renderDigit(2, 8);
    updateAllDisplays();
    
    delay(2000);
}

void testCharacterRendering() {
    Serial.println("Testing character rendering...");
    
    // Test digits 0-9
    for (uint8_t digit = 0; digit <= 9; digit++) {
        clearPixelBuffer();
        
        for (uint8_t pos = 0; pos < NUM_CHARS_PER_ROW; pos++) {
            renderDigit(pos, digit);
        }
        
        updateAllDisplays();
        delay(1000);
    }
    
    // Test letters
    const char* testChars = "AbCdEF";
    clearPixelBuffer();
    renderText(testChars, 0);
    updateAllDisplays();
    delay(2000);
}

void testDisplaySequence() {
    Serial.println("\n=== OLED Display Test Sequence ===");
    
    // Test 1: Scan I2C bus
    scanMuxChannels();
    
    // Test 2: Clear all displays
    Serial.println("Test: Clear all displays");
    clearAllDisplays();
    delay(1000);
    
    // Test 3: Render digits 0-9
    Serial.println("Test: Count 0-9");
    for (uint8_t i = 0; i <= 9; i++) {
        clearPixelBuffer();
        renderNumber(i, true);
        updateAllDisplays();
        delay(500);
    }
    
    // Test 4: Render large numbers
    Serial.println("Test: Large numbers");
    uint32_t testScores[] = {123456, 999999, 42, 1337, 0};
    for (uint8_t i = 0; i < 5; i++) {
        clearPixelBuffer();
        renderNumber(testScores[i], false);
        updateAllDisplays();
        delay(1500);
    }
    
    // Test 5: Test characters
    testCharacterRendering();
    
    Serial.println("=== Test Complete ===\n");
}

void dumpPixelBuffer() {
    Serial.println("=== Pixel Buffer Dump ===");
    Serial.print("Buffer size: ");
    Serial.print(BUFFER_SIZE_BYTES);
    Serial.println(" bytes");
    
    // Print summary - count lit pixels
    uint16_t litPixels = 0;
    for (uint16_t i = 0; i < BUFFER_SIZE_BYTES; i++) {
        litPixels += __builtin_popcount(pixelBuffer[i]);
    }
    
    Serial.print("Lit pixels: ");
    Serial.print(litPixels);
    Serial.print(" / ");
    Serial.println(BUFFER_WIDTH * BUFFER_HEIGHT);
    Serial.println("========================");
}
