// main_firmware.cpp - RTOS lamp and switch matrix scanner
// Based on Lamp MATRIX.docx scanning sequence
// MOSFET issue resolved - now using RTOS with proper timing

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_task_wdt.h"
#include "driver/dac.h"
#include "SPIFFS.h"
#include "AudioFileSourceSPIFFS.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputDAC.h"  // Custom DAC output - better quality
#include "displays.h"
#include "w25q64.h"  // External 8MB SPI flash for future use
#include "external_flash_ota.h"  // Two-stage OTA via external flash
#include "ota_http_server.h"     // Web UI for firmware uploads

// ========== WiFi CONFIGURATION ==========
// Change these to match your network
const char* wifi_ssid = "Forche main 2.4";  // <-- CHANGE THIS
const char* wifi_password = "gizmoa22";  // <-- CHANGE THIS
const char* ota_hostname = "captain-fantastic";
const char* ota_password = "pinball2026";

// Hardware definitions
#define NUM_LAMP_ROWS 8
#define NUM_LAMP_COLS 5  // Columns L3-L7 in shift register (bits 0-4)
#define NUM_SWITCH_ROWS 8
#define NUM_SWITCH_COLS 4  // Columns SW1-SW4
#define NUM_SOLENOIDS 5    // SOL0-SOL4

// Row pins (M1-M8) - Shared between lamps and switches
// From schematic pin mapping table
const uint8_t rowPins[NUM_LAMP_ROWS] = {
    15, // M1 - GPIO15
    16, // M2 - GPIO16
    17, // M3 - GPIO17
    5,  // M4 - GPIO5
    18, // M5 - GPIO18
    19, // M6 - GPIO19
    23, // M7 - GPIO23
    13, // M8 - GPIO13
};

// Shift register pins (74HC595)
#define SR_DATA   2   // GPIO2 - Serial data
#define SR_CLOCK  12  // GPIO12 - Shift clock
#define SR_LATCH  4   // GPIO4 - Storage register clock

// Switch input pins
const uint8_t switchColPins[NUM_SWITCH_COLS] = {
    35, // SW1 - GPIO35
    34, // SW2 - GPIO34
    39, // SW3 - GPIO39
    36, // SW4 - GPIO36
};

// Shift register bit mapping (16 bits total)
// First 595 (U9) - Bits 0-7:
#define SR_BIT_L3   0  // Bit 0: Lamp column L3
#define SR_BIT_L4   1  // Bit 1: Lamp column L4
#define SR_BIT_L5   2  // Bit 2: Lamp column L5
#define SR_BIT_L6   3  // Bit 3: Lamp column L6
#define SR_BIT_L7   4  // Bit 4: Lamp column L7
#define SR_BIT_SOL0 5  // Bit 5: Solenoid 0
#define SR_BIT_SOL1 6  // Bit 6: Solenoid 1
#define SR_BIT_SOL2 7  // Bit 7: Solenoid 2

// Second 595 (U1) - Bits 8-15:
#define SR_BIT_SOL3 8   // Bit 8: Solenoid 3
#define SR_BIT_SOL4 9   // Bit 9: Solenoid 4
#define SR_BIT_LED9 11  // Bit 11: Status LED 1 red
#define SR_BIT_LED10 12 // Bit 12: Status LED 2 green

// Global state
volatile uint16_t shiftRegisterState = 0;  // 16-bit shift register state
bool lampMatrix[NUM_LAMP_ROWS][NUM_LAMP_COLS] = {0};  // Lamp state array
bool solenoidState[5] = {0};  // Solenoid state (SOL0-SOL4)
bool statusLedState[2] = {0};  // Status LEDs

// Switch state arrays
bool switchRaw[NUM_SWITCH_ROWS][NUM_SWITCH_COLS] = {0};
bool switchDebounced[NUM_SWITCH_ROWS][NUM_SWITCH_COLS] = {0};
bool switchPrevious[NUM_SWITCH_ROWS][NUM_SWITCH_COLS] = {0};  // Track previous debounced state for edge detection
uint8_t switchDebounceCounter[NUM_SWITCH_ROWS][NUM_SWITCH_COLS] = {0};  // Debounce counter (0-3)

// Global score (for display - points to current player's score)
volatile uint32_t currentScore = 0;

// Diagnostic test mode
bool diagnosticMode = false;
uint8_t diagnosticStep = 0;

// Switch mapping mode - for identifying physical switch locations
bool switchMappingMode = false;

// ========== GAME STATE ==========
enum GameState {
    STATE_ATTRACT,
    STATE_GAME_START,
    STATE_BALL_IN_PLAY,
    STATE_BONUS_COUNTDOWN,
    STATE_BALL_OVER,
    STATE_GAME_OVER
};

GameState gameState = STATE_ATTRACT;

// Player data
struct PlayerData {
    uint32_t score;
    uint32_t bonus;
    uint8_t bonusMultiplier;  // 1, 2, or 3
    bool laneA, laneB, laneC, laneD;  // A-B-C-D lane completion
    bool target1, target2, target3;    // Target completion for bonus multiplier
    bool extraBallLit;                  // Extra ball available on return lanes
    uint8_t extraBallsAwarded;         // Track 100K, 200K, 300K awards
};

PlayerData players[4];
uint8_t numPlayers = 0;
uint8_t currentPlayer = 0;
uint8_t currentBall = 1;
bool ballInPlay = false;
uint32_t lastSwitchTime = 0;

// Solenoid timing - track when each solenoid was fired
uint32_t solenoidFireTime[5] = {0, 0, 0, 0, 0};
const uint32_t SOLENOID_PULSE_MS = 50;  // 50ms pulse duration

// ========== FORWARD DECLARATIONS ==========
void resetPlayer(uint8_t playerIndex);
void startNewGame(uint8_t players_count);
void handleSwitchScore(uint8_t row, uint8_t col, bool closed);
void fireSolenoid(uint8_t solenoidNum);

// ========== LAMP NAME LOOKUP ==========
// Returns the descriptive name for a lamp at given row/col
const char* getLampName(uint8_t row, uint8_t col) {
    // col: 0=L3, 1=L4, 2=L5, 3=L6, 4=L7
    // row: 0=M1, 1=M2, 2=M3, 3=M4, 4=M5, 5=M6, 6=M7, 7=M8
    
    if (row == 0) {
        if (col == 2) return "L12: 8K bonus";
        if (col == 3) return "L11: 9K bonus";
        if (col == 4) return "L22: Return Lane";
    }
    else if (row == 1) {
        if (col == 1) return "L1: A";
        if (col == 2) return "L14: 6K bonus";
        if (col == 3) return "L6: 1";
        if (col == 4) return "L19: 1000 bonus";
    }
    else if (row == 2) {
        if (col == 1) return "L2: B";
        if (col == 2) return "L15: 5K bonus";
        if (col == 3) return "L7: double bonus";
        if (col == 4) return "L21: Return Lane";
    }
    else if (row == 3) {
        if (col == 1) return "L3: C";
        if (col == 2) return "L13: 7K bonus";
        if (col == 3) return "L10: 10k bonus";
        if (col == 4) return "L18: 2k bonus";
    }
    else if (row == 4) {
        if (col == 1) return "L4: D";
        if (col == 2) return "L17: 3k bonus";
        if (col == 3) return "L9: 2";
        if (col == 4) return "L20: same player";
    }
    else if (row == 5) {
        if (col == 2) return "L16: 4k bonus";
        if (col == 3) return "L8: tripple bonus";
        if (col == 4) return "B5: Ball 5";
    }
    else if (row == 6) {
        if (col == 1) return "B1: Ball 1";
        if (col == 2) return "B2: Ball 2";
        if (col == 3) return "B3: Ball 3";
        if (col == 4) return "B4: Ball 4";
    }
    else if (row == 7) {
        if (col == 1) return "P1: Player 1";
        if (col == 2) return "P2: Player 2";
        if (col == 3) return "P3: Player 3";
        if (col == 4) return "P4: Player 4";
    }
    
    // Special case: game over at row 0, col 0
    if (row == 0 && col == 0) return "game over";
    
    return "Unknown";
}

// ========== SWITCH NAME LOOKUP ==========
// Returns the descriptive name for a switch at given row/col
// Based on Captain Fantastic switch matrix diagram
const char* getSwitchName(uint8_t row, uint8_t col) {
    // Static buffer for formatted name
    static char nameBuffer[40];
    
    // Map based on switch matrix diagram:
    // MX1 (row 0): SW0=S20/Ball Eject, SW1=Tilt2, SW2=S11/Spinner R, SW3=S22/Return Lane R
    // MX2 (row 1): SW0=S1/Lane A, SW1=Start2, SW2=S6/Target 1, SW3=S14/Slingshot L
    // MX3 (row 2): SW0=S4/Lane B, SW1=S7/Bumper L, SW2=S12/Side sw, SW3=S21/Return Lane L
    // MX4 (row 3): SW0=S3/Lane C, SW1=S8/Bumper R, SW2=S10/Spinner L, SW3=S18/Bonus lane L
    // MX5 (row 4): SW0=S4/Lane D, SW1=Easy, SW2=S9/Target 2, SW3=S16/Slingshot R
    // MX6 (row 5): SW0=S5/Target 3, SW1=Test2, SW2=S13/Side sw, SW3=S19/Bonus lane R
    
    if (row == 0) {  // MX1
        if (col == 0) return "S20 - Ball Eject";
        if (col == 1) return "Tilt2";
        if (col == 2) return "S11 - Spinner R";
        if (col == 3) return "S22 - Return Lane R";
    }
    else if (row == 1) {  // MX2
        if (col == 0) return "S1 - Lane A";
        if (col == 1) return "Start2 - Start Button";
        if (col == 2) return "S6 - Target 1";
        if (col == 3) return "S14 - Slingshot L";
    }
    else if (row == 2) {  // MX3
        if (col == 0) return "S4 - Lane B";
        if (col == 1) return "S7 - Bumper L";
        if (col == 2) return "S12 - Side Switch";
        if (col == 3) return "S21 - Return Lane L";
    }
    else if (row == 3) {  // MX4
        if (col == 0) return "S3 - Lane C";
        if (col == 1) return "S8 - Bumper R";
        if (col == 2) return "S10 - Spinner L";
        if (col == 3) return "S18 - Bonus Lane L";
    }
    else if (row == 4) {  // MX5
        if (col == 0) return "S4 - Lane D";
        if (col == 1) return "Easy - Easy Button";
        if (col == 2) return "S9 - Target 2";
        if (col == 3) return "S16 - Slingshot R";
    }
    else if (row == 5) {  // MX6
        if (col == 0) return "S5 - Target 3";
        if (col == 1) return "Test2 - Test Switch";
        if (col == 2) return "S13 - Side Switch";
        if (col == 3) return "S19 - Bonus Lane R";
    }
    else if (row == 6) {  // MX7
        sprintf(nameBuffer, "MX7/SW%d - Unmapped", col + 1);
        return nameBuffer;
    }
    else if (row == 7) {  // MX8
        sprintf(nameBuffer, "MX8/SW%d - Unmapped", col + 1);
        return nameBuffer;
    }
    
    sprintf(nameBuffer, "Unknown M%d/SW%d", row + 1, col + 1);
    return nameBuffer;
}

// ========== SOLENOID CONTROL ==========
// Fire a solenoid with automatic timeout
void fireSolenoid(uint8_t solenoidNum) {
    if (solenoidNum < 5) {
        solenoidState[solenoidNum] = true;
        solenoidFireTime[solenoidNum] = millis();
    }
}

// Update solenoid states - auto-clear after pulse duration
void updateSolenoids() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < 5; i++) {
        if (solenoidState[i] && (now - solenoidFireTime[i] >= SOLENOID_PULSE_MS)) {
            solenoidState[i] = false;
        }
    }
}

// Task handles
TaskHandle_t matrixTaskHandle = NULL;
TaskHandle_t displayTaskHandle = NULL;
TaskHandle_t ioServiceTaskHandle = NULL;
TaskHandle_t gameLogicTaskHandle = NULL;
TaskHandle_t audioMP3TaskHandle = NULL;
TaskHandle_t otaTaskHandle = NULL;  // Added task suspension for OTA updates

// ========== AUDIO SYSTEM DEFINITIONS ==========
// GPIO25 - DAC1 output to LM384N amplifier
#define AUDIO_PIN 25

// Sound effect IDs
enum SoundEffect {
    SND_NONE = 0,
    SND_BUMPER,          // Bumper hit - 700Hz
    SND_SLINGSHOT,       // Slingshot - 700Hz rising
    SND_TARGET,          // Target hit - 1300Hz
    SND_ROLLOVER,        // Rollover lane - 1300Hz short
    SND_DRAIN,           // Ball drain - descending tone
    SND_BONUS_COUNT,     // Bonus counting - 1000Hz quick
    SND_SPECIAL,         // Special/Extra ball - rising fanfare
    SND_STARTUP,         // Power-on jingle
    SND_COIN_INSERT,     // Coin inserted
    SND_GAME_START,      // Game start
    SND_GAME_OVER,       // Game over
    SND_SWITCH_CLOSE,    // Generic switch close
    SND_SWITCH_OPEN      // Generic switch open
};

// Sound message structure
struct SoundMessage {
    SoundEffect soundId;
    uint8_t priority;  // 0=low, 255=high (can interrupt current sound)
};

// Sound queue
QueueHandle_t soundQueue = NULL;
#define SOUND_QUEUE_LENGTH 16

// Audio volume (0-255) - Gets divided by 2 in playTone() for amplitude
// With LM384 gain of 50x, audioVolume=6 gives ~4V speaker output (safe for 8Ω)
uint8_t audioVolume = 6;

// ========== MP3 MUSIC SYSTEM ==========
// Music track IDs
enum MusicTrack {
    MUSIC_NONE = 0,
    MUSIC_ATTRACT,      // Attract mode - loops continuously
    MUSIC_START,        // Game start fanfare
    MUSIC_BONUS,        // Bonus countdown
    MUSIC_GAMEOVER,     // Game over
    MUSIC_HISCORE       // High score achievement
};

// MP3 player objects
AudioGeneratorMP3 *mp3 = NULL;
AudioFileSourceSPIFFS *file = NULL;
AudioOutput *out = NULL;  // Base class - can be I2S or I2SNoDAC

// W25Q64 external flash for future expansion
W25Q64 externalFlash;

// External flash OTA system
ExternalFlashOTA* extOTA = nullptr;
OTAHttpServer* otaServer = nullptr;

// Music state
MusicTrack currentMusic = MUSIC_NONE;
MusicTrack requestedMusic = MUSIC_NONE;
bool musicPlaying = false;
bool musicShouldLoop = false;
float musicVolume = 0.5;  // 50% - direct DAC output (same quality as tones)

// ========== FORWARD DECLARATIONS ==========
bool triggerSound(SoundEffect soundId, uint8_t priority = 0);
void triggerSoundFromISR(SoundEffect soundId, uint8_t priority = 0);
void requestMusic(MusicTrack track);

// Transfer data to shift register
void updateShiftRegister() {
    digitalWrite(SR_LATCH, LOW);
    delayMicroseconds(1);
    
    for (int8_t i = 15; i >= 0; i--) {
        digitalWrite(SR_CLOCK, LOW);
        digitalWrite(SR_DATA, (shiftRegisterState & (1 << i)) ? HIGH : LOW);
        delayMicroseconds(1);
        digitalWrite(SR_CLOCK, HIGH);
        delayMicroseconds(1);
    }
    
    digitalWrite(SR_LATCH, HIGH);
    delayMicroseconds(1);
}

// ========== RTOS DISPLAY UPDATE TASK ==========
// Updates the 7-segment displays independently
// Runs on Core 0 to keep it separate from matrix scanning
void displayTask(void* parameter) {
    // Run flashy startup test once
    delay(500);  // Brief delay before starting
    displayStartupTest();
    
    // Now continuously update score display
    const TickType_t xFrequency = pdMS_TO_TICKS(100); // Update every 100ms
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while (true) {
        updateLEDScore(currentScore);
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// ========== I/O SERVICE TASK ==========
// Handles switch debouncing, solenoid control, and I/O monitoring
// Runs at 20ms intervals for debouncing and solenoid timing
void ioServiceTask(void* parameter) {
    const TickType_t xFrequency = pdMS_TO_TICKS(20); // Run every 20ms
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t heartbeatCounter = 0;  // Counter for heartbeat timing
    
    while (true) {
        // Heartbeat for LED10 (green) - blink at ~1Hz (500ms on, 500ms off)
        heartbeatCounter++;
        if (heartbeatCounter >= 25) {  // 25 * 20ms = 500ms
            statusLedState[1] = !statusLedState[1];  // Toggle LED10
            heartbeatCounter = 0;
        }
        
        // ========== SWITCH DEBOUNCING ==========
        // Simple counter-based debounce: requires 3 consecutive stable readings
        for (uint8_t row = 0; row < NUM_SWITCH_ROWS; row++) {
            for (uint8_t col = 0; col < NUM_SWITCH_COLS; col++) {
                // Check if raw state matches current debounced state
                if (switchRaw[row][col] == switchDebounced[row][col]) {
                    // Stable - reset counter
                    switchDebounceCounter[row][col] = 0;
                } else {
                    // Raw state differs - increment counter
                    switchDebounceCounter[row][col]++;
                    
                    // If stable for 3 consecutive reads (60ms), accept the change
                    if (switchDebounceCounter[row][col] >= 3) {
                        // Detect edge BEFORE updating debounced state
                        if (switchRaw[row][col] != switchDebounced[row][col]) {
                            bool closed = switchRaw[row][col];
                            
                            // Check for Start button press (row 1, col 1)
                            if (row == 1 && col == 1 && closed) {
                                Serial.println("\n[SWITCH] === START BUTTON PRESSED ===");
                                
                                if (diagnosticMode) {
                                    // In diagnostic mode: advance to solenoid test
                                    diagnosticStep = 3;
                                    Serial.println("[DIAG] Jumping to solenoid test...");
                                } else if (gameState == STATE_ATTRACT) {
                                    // Start new 1-player game
                                    startNewGame(1);
                                } else if (gameState == STATE_GAME_START || gameState == STATE_BALL_IN_PLAY) {
                                    // Add player during game (max 4)
                                    if (numPlayers < 4) {
                                        numPlayers++;
                                        resetPlayer(numPlayers - 1);
                                        Serial.printf("[GAME] Player %d added!\n", numPlayers);
                                    }
                                }
                            }
                            
                            // Check for Test button (MX6 SW1 = row 5, col 1)
                            if (row == 5 && col == 1 && closed) {
                                Serial.println("\n[SWITCH] === TEST BUTTON PRESSED ===");
                                diagnosticMode = !diagnosticMode;
                                if (diagnosticMode) {
                                    diagnosticStep = 0;
                                    Serial.println("[DIAG] Entering diagnostic mode...");
                                } else {
                                    Serial.println("[DIAG] Exiting diagnostic mode...");
                                }
                            }
                            
                            // Handle switch scoring during gameplay
                            handleSwitchScore(row, col, closed);
                            
                            // State changed and is now stable - report it
                            if (switchMappingMode) {
                                // Verbose output for switch mapping
                                Serial.println("\n====================================");
                                Serial.print("SWITCH ACTIVATED: Row=");
                                Serial.print(row);
                                Serial.print(" (M");
                                Serial.print(row + 1);
                                Serial.print("), Column=");
                                Serial.print(col);
                                Serial.print(" (SW");
                                Serial.print(col + 1);
                                Serial.print(")\n");
                                Serial.print("Name: ");
                                Serial.println(getSwitchName(row, col));
                                Serial.print("State: ");
                                Serial.println(switchRaw[row][col] ? "CLOSED" : "OPEN");
                                Serial.print("Array index: switchMatrix[");
                                Serial.print(row);
                                Serial.print("][");
                                Serial.print(col);
                                Serial.println("]");
                                Serial.println("====================================");
                            } else {
                                // Normal compact output with name
                                Serial.print("[SWITCH] M");
                                Serial.print(row + 1);
                                Serial.print(", SW");
                                Serial.print(col + 1);
                                Serial.print(": ");
                                Serial.print(switchRaw[row][col] ? "CLOSED" : "OPEN");
                                Serial.print(" - ");
                                Serial.println(getSwitchName(row, col));
                            }
                        }
                        
                        switchDebounced[row][col] = switchRaw[row][col];
                        switchPrevious[row][col] = switchRaw[row][col];
                        switchDebounceCounter[row][col] = 0;
                    }
                }
            }
        }
        
        // Update solenoid states (auto-clear after pulse duration)
        updateSolenoids();
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// ========== GAME LOGIC TASK ==========

// Helper function to clear all lamps
void clearAllLamps() {
    for (uint8_t r = 0; r < NUM_LAMP_ROWS; r++) {
        for (uint8_t c = 0; c < NUM_LAMP_COLS; c++) {
            lampMatrix[r][c] = false;
        }
    }
}

// Reset player data
void resetPlayer(uint8_t playerIndex) {
    players[playerIndex].score = 0;
    players[playerIndex].bonus = 1000;  // Bonus starts at 1000
    players[playerIndex].bonusMultiplier = 1;
    players[playerIndex].laneA = false;
    players[playerIndex].laneB = false;
    players[playerIndex].laneC = false;
    players[playerIndex].laneD = false;
    players[playerIndex].target1 = false;
    players[playerIndex].target2 = false;
    players[playerIndex].target3 = false;
    players[playerIndex].extraBallLit = false;
    players[playerIndex].extraBallsAwarded = 0;
}

// Start new game
void startNewGame(uint8_t players_count) {
    numPlayers = players_count;
    currentPlayer = 0;
    currentBall = 1;
    
    for (uint8_t i = 0; i < numPlayers; i++) {
        resetPlayer(i);
    }
    
    currentScore = 0;
    gameState = STATE_GAME_START;
    
    Serial.println("\n========================================");
    Serial.printf("NEW GAME STARTED - %d PLAYER(S)\n", numPlayers);
    Serial.println("========================================\n");
}

// Add score and check for extra balls
void addScore(uint32_t points) {
    PlayerData &p = players[currentPlayer];
    uint32_t oldScore = p.score;
    p.score += points;
    currentScore = p.score;
    
    // Check for extra ball awards at 100K, 200K, 300K
    if (p.extraBallsAwarded == 0 && p.score >= 100000 && oldScore < 100000) {
        p.extraBallsAwarded = 1;
        Serial.println("[GAME] *** EXTRA BALL AWARDED - 100,000 POINTS! ***");
        // TODO: Award extra ball
    }
    else if (p.extraBallsAwarded == 1 && p.score >= 200000 && oldScore < 200000) {
        p.extraBallsAwarded = 2;
        Serial.println("[GAME] *** EXTRA BALL AWARDED - 200,000 POINTS! ***");
        // TODO: Award extra ball
    }
    else if (p.extraBallsAwarded == 2 && p.score >= 300000 && oldScore < 300000) {
        p.extraBallsAwarded = 3;
        Serial.println("[GAME] *** EXTRA BALL AWARDED - 300,000 POINTS! ***");
        // TODO: Award extra ball
    }
}

// Add to bonus counter
void addBonus(uint32_t amount) {
    players[currentPlayer].bonus += amount;
}

// Check and update bonus multiplier based on targets
void updateBonusMultiplier() {
    PlayerData &p = players[currentPlayer];
    
    if (p.target1 && p.target2 && p.target3) {
        if (p.bonusMultiplier < 3) {
            p.bonusMultiplier = 3;
            Serial.println("[GAME] *** TRIPLE BONUS! ***");
        }
    }
    else if (p.target1 && p.target2) {
        if (p.bonusMultiplier < 2) {
            p.bonusMultiplier = 2;
            Serial.println("[GAME] *** DOUBLE BONUS! ***");
        }
    }
}

// Check if A-B-C-D lanes complete
void checkLaneCompletion() {
    PlayerData &p = players[currentPlayer];
    
    if (p.laneA && p.laneB && p.laneC && p.laneD && !p.extraBallLit) {
        p.extraBallLit = true;
        Serial.println("[GAME] *** EXTRA BALL LIT ON RETURN LANES! ***");
    }
}

// Handle switch scoring
void handleSwitchScore(uint8_t row, uint8_t col, bool closed) {
    if (!ballInPlay || !closed) return;  // Only score on switch close during ball in play
    
    PlayerData &p = players[currentPlayer];
    lastSwitchTime = millis();
    
    // Map row/col to switch number and score
    // MX1 (row 0): SW0=S20, SW1=Tilt, SW2=S11, SW3=S22
    // MX2 (row 1): SW0=S1, SW1=Start, SW2=S6, SW3=S14
    // MX3 (row 2): SW0=S2, SW1=S7, SW2=S12, SW3=S21
    // MX4 (row 3): SW0=S3, SW1=S8, SW2=S10, SW3=S18
    // MX5 (row 4): SW0=S4, SW1=Easy, SW2=S9, SW3=S16
    // MX6 (row 5): SW0=S5, SW1=Test, SW2=S13, SW3=S19
    
    if (row == 0 && col == 2) {  // S11 - Spinner R
        addScore(100);
        Serial.println("[SCORE] Spinner R: +100");
    }
    else if (row == 0 && col == 3) {  // S22 - Return Lane R
        addScore(500);
        addBonus(500);
        if (p.extraBallLit) {
            Serial.println("[SCORE] Return Lane R: +500 + EXTRA BALL!");
            p.extraBallLit = false;
            // TODO: Award extra ball
        } else {
            Serial.println("[SCORE] Return Lane R: +500 + Bonus");
        }
    }
    else if (row == 1 && col == 0) {  // S1 - Lane A
        addScore(1000);
        addBonus(1000);
        p.laneA = true;
        checkLaneCompletion();
        Serial.println("[SCORE] Lane A: +1000 + Bonus (A lit)");
    }
    else if (row == 1 && col == 2) {  // S6 - Target 1
        addScore(50);
        addBonus(2000);  // Add 2000 to bonus each hit
        p.target1 = true;
        updateBonusMultiplier();
        triggerSound(SND_TARGET, 2);
        Serial.println("[SCORE] Target 1: +50, Bonus +2000");
    }
    else if (row == 1 && col == 3) {  // S14 - Slingshot L (and S15)
        addScore(100);
        fireSolenoid(1);  // Fire left slingshot
        triggerSound(SND_SLINGSHOT, 2);
        Serial.println("[SCORE] Slingshot L: +100");
    }
    else if (row == 2 && col == 0) {  // S2 - Lane B
        addScore(1000);
        addBonus(1000);
        p.laneB = true;
        checkLaneCompletion();
        Serial.println("[SCORE] Lane B: +1000 + Bonus (B lit)");
    }
    else if (row == 2 && col == 1) {  // S7 - Bumper L
        addScore(100);
        fireSolenoid(3);  // Fire left thumper-bumper
        triggerSound(SND_BUMPER, 2);
        Serial.println("[SCORE] Bumper L: +100");
    }
    else if (row == 2 && col == 2) {  // S12 - Side Switch
        addScore(50);
        addBonus(50);
        Serial.println("[SCORE] Side Switch: +50 + Bonus");
    }
    else if (row == 2 && col == 3) {  // S21 - Return Lane L
        addScore(500);
        addBonus(500);
        if (p.extraBallLit) {
            Serial.println("[SCORE] Return Lane L: +500 + EXTRA BALL!");
            p.extraBallLit = false;
            // TODO: Award extra ball
        } else {
            Serial.println("[SCORE] Return Lane L: +500 + Bonus");
        }
    }
    else if (row == 3 && col == 0) {  // S3 - Lane C
        addScore(1000);
        addBonus(1000);
        p.laneC = true;
        checkLaneCompletion();
        Serial.println("[SCORE] Lane C: +1000 + Bonus (C lit)");
    }
    else if (row == 3 && col == 1) {  // S8 - Bumper R
        addScore(100);
        fireSolenoid(4);  // Fire right thumper-bumper
        triggerSound(SND_BUMPER, 2);
        Serial.println("[SCORE] Bumper R: +100");
    }
    else if (row == 3 && col == 2) {  // S10 - Spinner L
        addScore(100);
        Serial.println("[SCORE] Spinner L: +100");
    }
    else if (row == 3 && col == 3) {  // S18 - Bonus Lane L
        addScore(500);
        addBonus(500);
        Serial.println("[SCORE] Bonus Lane L: +500 + Bonus");
    }
    else if (row == 4 && col == 0) {  // S4 - Lane D
        addScore(1000);
        addBonus(1000);
        p.laneD = true;
        checkLaneCompletion();
        Serial.println("[SCORE] Lane D: +1000 + Bonus (D lit)");
    }
    else if (row == 4 && col == 2) {  // S9 - Target 2
        addScore(50);
        addBonus(2000);  // Add 2000 to bonus each hit
        p.target2 = true;
        updateBonusMultiplier();
        triggerSound(SND_TARGET, 2);
        Serial.println("[SCORE] Target 2: +50, Bonus +2000");
    }
    else if (row == 4 && col == 3) {  // S16 - Slingshot R (and S17)
        addScore(100);
        fireSolenoid(2);  // Fire right slingshot
        triggerSound(SND_SLINGSHOT, 2);
        Serial.println("[SCORE] Slingshot R: +100");
    }
    else if (row == 5 && col == 0) {  // S5 - Target 3
        addScore(50);
        addBonus(2000);  // Add 2000 to bonus each hit
        p.target3 = true;
        updateBonusMultiplier();
        triggerSound(SND_TARGET, 2);
        Serial.println("[SCORE] Target 3: +50, Bonus +2000");
    }
    else if (row == 5 && col == 2) {  // S13 - Side Switch
        addScore(50);
        addBonus(50);
        Serial.println("[SCORE] Side Switch: +50 + Bonus");
    }
    else if (row == 5 && col == 3) {  // S19 - Bonus Lane R
        addScore(500);
        addBonus(500);
        Serial.println("[SCORE] Bonus Lane R: +500 + Bonus");
    }
    
    // Check for outhole (S20) - ball drain
    if (row == 0 && col == 0 && closed) {  // S20 - Outhole/Ball Eject
        Serial.println("[GAME] Ball drained (outhole)");
        ballInPlay = false;
        gameState = STATE_BONUS_COUNTDOWN;
        triggerSound(SND_DRAIN, 3);
    }
    
    // Tilt at row 0, col 1 - handle separately
    // Start at row 1, col 1 - handled in debounce task
}

// Update lamps based on game state
void updateGameLamps() {
    clearAllLamps();
    
    PlayerData &p = players[currentPlayer];
    
    // Show player lamps (P1-P4) at row 7, cols 1-4
    for (uint8_t i = 0; i < numPlayers; i++) {
        if (i == currentPlayer) {
            lampMatrix[7][i + 1] = true;  // Current player lit
        }
    }
    
    // Show ball lamps (B1-B5) at row 6, cols 1-4 and row 5, col 4
    if (currentBall <= 4) {
        lampMatrix[6][currentBall] = true;
    } else if (currentBall == 5) {
        lampMatrix[5][4] = true;
    }
    
    // Show A-B-C-D lane lamps (L1-L4) at row 1-4, col 1
    if (p.laneA) lampMatrix[1][1] = true;  // L1: A
    if (p.laneB) lampMatrix[2][1] = true;  // L2: B
    if (p.laneC) lampMatrix[3][1] = true;  // L3: C
    if (p.laneD) lampMatrix[4][1] = true;  // L4: D
    
    // Show target completion lamps (1, 2, 3)
    if (p.target1) lampMatrix[1][3] = true;  // L6: 1
    if (p.target2) lampMatrix[4][3] = true;  // L9: 2
    if (p.target3) lampMatrix[5][1] = true;  // L5: 3 (needs verification)
    
    // Show bonus multiplier
    if (p.bonusMultiplier >= 2) {
        lampMatrix[2][3] = true;  // L7: double bonus
    }
    if (p.bonusMultiplier >= 3) {
        lampMatrix[5][3] = true;  // L8: triple bonus
    }
    
    // Show bonus value on lamp pole (1K-19K) - cumulative lighting
    // Bonus pole: L19(1K), L18(2K), L17(3K), L16(4K), L15(5K), L14(6K), L13(7K), L12(8K), L11(9K), L10(10K)
    // For 11K+, light 10K plus additional thousands (11K = 1K+10K, 12K = 2K+10K, 13K = 3K+10K, etc.)
    uint32_t bonusThousands = p.bonus / 1000;
    
    if (bonusThousands >= 10) {
        lampMatrix[3][3] = true;  // L10: 10K always lit when >= 10K
        // Add additional thousands: 11K=1K+10K, 12K=2K+10K, etc.
        uint32_t extraThousands = bonusThousands - 10;
        if (extraThousands >= 1) lampMatrix[1][4] = true;  // L19: +1K (11K total)
        if (extraThousands >= 2) lampMatrix[3][4] = true;  // L18: +2K (12K total)
        if (extraThousands >= 3) lampMatrix[4][2] = true;  // L17: +3K (13K total)
        if (extraThousands >= 4) lampMatrix[5][2] = true;  // L16: +4K (14K total)
        if (extraThousands >= 5) lampMatrix[2][2] = true;  // L15: +5K (15K total)
        if (extraThousands >= 6) lampMatrix[1][2] = true;  // L14: +6K (16K total)
        if (extraThousands >= 7) lampMatrix[3][2] = true;  // L13: +7K (17K total)
        if (extraThousands >= 8) lampMatrix[0][2] = true;  // L12: +8K (18K total)
        if (extraThousands >= 9) lampMatrix[0][3] = true;  // L11: +9K (19K total)
    } else {
        // Below 10K: light lamps sequentially
        if (bonusThousands >= 1) lampMatrix[1][4] = true;  // L19: 1K
        if (bonusThousands >= 2) lampMatrix[3][4] = true;  // L18: 2K
        if (bonusThousands >= 3) lampMatrix[4][2] = true;  // L17: 3K
        if (bonusThousands >= 4) lampMatrix[5][2] = true;  // L16: 4K
        if (bonusThousands >= 5) lampMatrix[2][2] = true;  // L15: 5K
        if (bonusThousands >= 6) lampMatrix[1][2] = true;  // L14: 6K
        if (bonusThousands >= 7) lampMatrix[3][2] = true;  // L13: 7K
        if (bonusThousands >= 8) lampMatrix[0][2] = true;  // L12: 8K
        if (bonusThousands >= 9) lampMatrix[0][3] = true;  // L11: 9K
    }
    
    // Show extra ball lit
    if (p.extraBallLit) {
        // Flash return lane lamps
        if ((millis() / 250) % 2 == 0) {
            lampMatrix[2][4] = true;  // L21: Return Lane L
            lampMatrix[0][4] = true;  // L22: Return Lane R
        }
    }
}

// Helper function to set lamps in diagnostic GROUP 1
void setLampGroup1() {
    // GROUP 1: P-1, P-2, B-1, B-2, L-1, L-2, L-4, L-5, L-10, L-11, L-14, L-15, L-16, L-17, L-18, L-22
    lampMatrix[1][1] = true;  // L1: M2, L4
    lampMatrix[2][1] = true;  // L2: M3, L4
    lampMatrix[0][1] = true;  // L4: M1, L4
    lampMatrix[4][1] = true;  // L5: M5, L4
    lampMatrix[3][3] = true;  // L10: M4, L6
    lampMatrix[0][3] = true;  // L11: M1, L6
    lampMatrix[1][2] = true;  // L14: M2, L5
    lampMatrix[2][2] = true;  // L15: M3, L5
    lampMatrix[5][2] = true;  // L16: M6, L5
    lampMatrix[4][2] = true;  // L17: M5, L5
    lampMatrix[3][4] = true;  // L18: M4, L7
    lampMatrix[0][4] = true;  // L22: M1, L7
    // Note: P-1, P-2, B-1, B-2 are not in our current lamp matrix (player/bumper lamps)
}

// Helper function to set lamps in diagnostic GROUP 2
void setLampGroup2() {
    // GROUP 2: P-3, P-4, B-3, B-4, B-5, L-3, L-6, L-7, L-8, L-9, L-12, L-13, L-18, L-19, L-20, L-21
    lampMatrix[3][1] = true;  // L3: M4, L4
    lampMatrix[1][3] = true;  // L6: M2, L6
    lampMatrix[2][3] = true;  // L7: M3, L6
    lampMatrix[5][3] = true;  // L8: M6, L6
    lampMatrix[4][3] = true;  // L9: M5, L6
    lampMatrix[0][2] = true;  // L12: M1, L5
    lampMatrix[3][2] = true;  // L13: M4, L5
    lampMatrix[3][4] = true;  // L18: M4, L7 (in both groups per manual)
    lampMatrix[1][4] = true;  // L19: M2, L7
    lampMatrix[4][4] = true;  // L20: M5, L7
    lampMatrix[2][4] = true;  // L21: M3, L7
    // Note: P-3, P-4, B-3, B-4, B-5, Game Over are not in our current lamp matrix
}

// Handles game rules, scoring, state machine, and gameplay logic
// Runs at 50ms intervals
void gameLogicTask(void* parameter) {
    const TickType_t xFrequency = pdMS_TO_TICKS(50); // Run every 50ms
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    uint8_t attractStep = 0;
    uint16_t stepCounter = 0;
    uint32_t diagnosticCounter = 0;
    uint32_t bonusCountdownTimer = 0;
    
    Serial.println("[GAME] Game Logic task started");
    
    // Clear all lamps first
    clearAllLamps();
    
    while (true) {
        stepCounter++;
        
        if (diagnosticMode) {
            // ===== BALLY SERIES II DIAGNOSTIC TEST MODE =====
            
            switch (diagnosticStep) {
                case 0:  // Logic and Program Test - Display "600d"
                    if (stepCounter == 1) {
                        Serial.println("[DIAG] Step 0: Logic and Program Test - Display 600d");
                        currentScore = 6000;  // Will display as "  600d" with trailing zeros
                    }
                    if (stepCounter >= 60) {  // Hold for 3 seconds
                        diagnosticStep = 1;
                        stepCounter = 0;
                        diagnosticCounter = 0;
                        Serial.println("[DIAG] Step 1: Score Display Scan starting");
                    }
                    break;
                    
                case 1:  // Score Display Scan - 000000 to 999999
                    if (stepCounter >= 2) {  // Update every 100ms
                        stepCounter = 0;
                        currentScore = diagnosticCounter;
                        diagnosticCounter += 111;  // Increment to test all segments
                        if (diagnosticCounter > 999999) {
                            diagnosticStep = 2;
                            diagnosticCounter = 0;
                            Serial.println("[DIAG] Step 2: Lamp Test - Alternating Groups");
                        }
                    }
                    break;
                    
                case 2:  // Lamp Test - GROUP 1 and GROUP 2 alternating
                    if (stepCounter >= 20) {  // Switch every 1 second
                        stepCounter = 0;
                        clearAllLamps();
                        if (diagnosticCounter % 2 == 0) {
                            setLampGroup1();
                            Serial.println("[DIAG] Lamp GROUP 1");
                        } else {
                            setLampGroup2();
                            Serial.println("[DIAG] Lamp GROUP 2");
                        }
                        diagnosticCounter++;
                        if (diagnosticCounter >= 10) {  // 5 cycles of each group
                            diagnosticStep = 3;
                            diagnosticCounter = 0;
                            clearAllLamps();
                            Serial.println("[DIAG] Step 3: Solenoid Test Sequence");
                        }
                    }
                    break;
                    
                case 3:  // Solenoid Test Sequence
                    if (stepCounter >= 20) {  // Each solenoid fires for 1 second
                        stepCounter = 0;
                        // Clear previous solenoid
                        solenoidState[0] = false;  // Ball Return
                        solenoidState[1] = false;  // Left Slingshot
                        solenoidState[2] = false;  // Right Slingshot
                        solenoidState[3] = false;  // Left Thumper-Bumper
                        solenoidState[4] = false;  // Right Thumper-Bumper
                        
                        switch (diagnosticCounter) {
                            case 0:
                                Serial.println("[DIAG] Solenoid A: Ball Return");
                                solenoidState[0] = true;
                                break;
                            case 1:
                                Serial.println("[DIAG] Solenoid B: Left Slingshot");
                                solenoidState[1] = true;
                                break;
                            case 2:
                                Serial.println("[DIAG] Solenoid C: Right Slingshot");
                                solenoidState[2] = true;
                                break;
                            case 3:
                                Serial.println("[DIAG] Solenoid D: Left Thumper-Bumper");
                                solenoidState[3] = true;
                                break;
                            case 4:
                                Serial.println("[DIAG] Solenoid E: Right Thumper-Bumper");
                                solenoidState[4] = true;
                                break;
                        }
                        
                        diagnosticCounter++;
                        if (diagnosticCounter > 4) {
                            diagnosticCounter = 0;
                        }
                    }
                    break;
                    
                case 4:  // Switch Test
                    // Display shows switch number when each switch closes
                    // Implementation handled in ioServiceTask by printing switch info
                    if (stepCounter == 1) {
                        Serial.println("[DIAG] Step 4: Switch Test - Close switches to test");
                        currentScore = 0;
                    }
                    break;
            }
        } else {
            // ===== NORMAL GAME LOGIC =====
            
            switch (gameState) {
                case STATE_ATTRACT:
                    // Attract mode lamp sequence
                    if (stepCounter >= 6) {
                        stepCounter = 0;
                        clearAllLamps();
                        
                        switch(attractStep) {
                            case 0:  lampMatrix[4][4] = true; lampMatrix[1][4] = true; lampMatrix[3][4] = true; break;
                            case 1:  lampMatrix[4][2] = true; break;
                            case 2:  lampMatrix[2][4] = true; lampMatrix[0][4] = true; lampMatrix[5][2] = true; break;
                            case 3:  lampMatrix[2][2] = true; break;
                            case 4:  lampMatrix[1][2] = true; break;
                            case 5:  lampMatrix[3][2] = true; break;
                            case 6:  lampMatrix[0][2] = true; break;
                            case 7:  lampMatrix[1][3] = true; lampMatrix[0][3] = true; lampMatrix[4][3] = true; break;
                            case 8:  lampMatrix[2][3] = true; lampMatrix[3][3] = true; lampMatrix[5][3] = true; break;
                            case 9:  lampMatrix[4][1] = true; break;
                            case 10: lampMatrix[1][1] = true; lampMatrix[2][1] = true; lampMatrix[3][1] = true; lampMatrix[0][1] = true; break;
                            case 11: for(uint8_t r=0; r<NUM_LAMP_ROWS; r++) for(uint8_t c=0; c<NUM_LAMP_COLS; c++) lampMatrix[r][c]=true; break;
                        }
                        
                        attractStep++;
                        if (attractStep > 11) attractStep = 0;
                    }
                    currentScore = 0;  // Display shows 0 in attract
                    break;
                    
                case STATE_GAME_START:
                    Serial.printf("[GAME] Starting Ball %d, Player %d\n", currentBall, currentPlayer + 1);
                    currentScore = players[currentPlayer].score;
                    ballInPlay = true;
                    gameState = STATE_BALL_IN_PLAY;
                    lastSwitchTime = millis();
                    updateGameLamps();
                    
                    // Fire ball eject solenoid
                    fireSolenoid(0);
                    Serial.println("[GAME] Ball ejected");
                    break;
                    
                case STATE_BALL_IN_PLAY:
                    // Update lamps based on current game state
                    updateGameLamps();
                    break;
                    
                case STATE_BONUS_COUNTDOWN:
                    // Count down bonus at 200ms per 1000 points, show on lamps
                    if (bonusCountdownTimer == 0) {
                        Serial.printf("[GAME] Bonus countdown: %d x %dX = %d\n", 
                                     players[currentPlayer].bonus, 
                                     players[currentPlayer].bonusMultiplier,
                                     players[currentPlayer].bonus * players[currentPlayer].bonusMultiplier);
                        bonusCountdownTimer = 1;
                    }
                    
                    // Update lamps to show current bonus value during countdown
                    updateGameLamps();
                    
                    if (stepCounter % 4 == 0 && players[currentPlayer].bonus > 0) {
                        // Count down 1000 points every 200ms (slower for better visibility)
                        if (players[currentPlayer].bonus >= 1000) {
                            addScore(1000 * players[currentPlayer].bonusMultiplier);
                            players[currentPlayer].bonus -= 1000;
                            triggerSound(SND_BONUS_COUNT, 1);
                        } else {
                            // Last partial bonus
                            addScore(players[currentPlayer].bonus * players[currentPlayer].bonusMultiplier);
                            players[currentPlayer].bonus = 0;
                        }
                    }
                    
                    if (players[currentPlayer].bonus == 0) {
                        gameState = STATE_BALL_OVER;
                        bonusCountdownTimer = 0;
                    }
                    break;
                    
                case STATE_BALL_OVER:
                    // Move to next player or next ball
                    currentPlayer++;
                    if (currentPlayer >= numPlayers) {
                        currentPlayer = 0;
                        currentBall++;
                        
                        if (currentBall > 5) {
                            gameState = STATE_GAME_OVER;
                        } else {
                            Serial.printf("[GAME] === BALL %d ===\n", currentBall);
                            gameState = STATE_GAME_START;
                        }
                    } else {
                        gameState = STATE_GAME_START;
                    }
                    
                    // Reset player features for new ball (keep score)
                    if (gameState == STATE_GAME_START) {
                        players[currentPlayer].bonus = 1000;
                        players[currentPlayer].bonusMultiplier = 1;
                        players[currentPlayer].laneA = false;
                        players[currentPlayer].laneB = false;
                        players[currentPlayer].laneC = false;
                        players[currentPlayer].laneD = false;
                        players[currentPlayer].target1 = false;
                        players[currentPlayer].target2 = false;
                        players[currentPlayer].target3 = false;
                        players[currentPlayer].extraBallLit = false;
                    }
                    break;
                    
                case STATE_GAME_OVER:
                    Serial.println("\n========================================");
                    Serial.println("           GAME OVER");
                    Serial.println("========================================");
                    for (uint8_t i = 0; i < numPlayers; i++) {
                        Serial.printf("Player %d: %d points\n", i + 1, players[i].score);
                    }
                    Serial.println("========================================\n");
                    
                    gameState = STATE_ATTRACT;
                    numPlayers = 0;
                    currentPlayer = 0;
                    currentBall = 1;
                    currentScore = 0;
                    break;
            }
        }
        
        // Handle OTA updates from this task (loop() doesn't run frequently enough with FreeRTOS)
        ArduinoOTA.handle();
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

// ========== AUDIO HELPER FUNCTIONS ==========

// Play a tone using DAC output
// frequency: Hz (20-20000)
// duration: milliseconds
void playTone(uint16_t frequency, uint16_t duration_ms) {
    if (frequency == 0 || audioVolume == 0) {
        // Silence
        dac_output_voltage(DAC_CHANNEL_1, 128);  // Center position
        delay(duration_ms);
        return;
    }
    
    uint32_t period_us = 1000000 / frequency;
    uint32_t half_period = period_us / 2;
    uint32_t cycles = (uint32_t)frequency * duration_ms / 1000;
    
    // Calculate amplitude based on volume (0-255)
    // DAC center is 128, amplitude is volume/2
    uint8_t amplitude = audioVolume / 2;
    uint8_t high_val = 128 + amplitude;
    uint8_t low_val = 128 - amplitude;
    
    // Generate square wave
    for (uint32_t i = 0; i < cycles; i++) {
        dac_output_voltage(DAC_CHANNEL_1, high_val);
        delayMicroseconds(half_period);
        dac_output_voltage(DAC_CHANNEL_1, low_val);
        delayMicroseconds(half_period);
    }
    
    // Return to center (silence)
    dac_output_voltage(DAC_CHANNEL_1, 128);
}

// Play rising tone sweep
void playRisingTone(uint16_t startFreq, uint16_t endFreq, uint16_t totalDuration_ms) {
    const int steps = 20;
    int stepDuration = totalDuration_ms / steps;
    int freqStep = (endFreq - startFreq) / steps;
    
    for (int i = 0; i < steps; i++) {
        uint16_t freq = startFreq + (freqStep * i);
        playTone(freq, stepDuration);
    }
}

// Play descending tone sweep
void playDescendingTone(uint16_t startFreq, uint16_t endFreq, uint16_t totalDuration_ms) {
    const int steps = 20;
    int stepDuration = totalDuration_ms / steps;
    int freqStep = (startFreq - endFreq) / steps;
    
    for (int i = 0; i < steps; i++) {
        uint16_t freq = startFreq - (freqStep * i);
        playTone(freq, stepDuration);
    }
}

// ========== SOUND EFFECT IMPLEMENTATIONS ==========

void soundBumperHit() {
    playTone(700, 100);  // Low buzz
}

void soundSlingshotHit() {
    playTone(700, 60);
    delay(10);
    playTone(850, 50);  // Rising tone
}

void soundTargetHit() {
    playTone(1300, 80);  // High beep
}

void soundRolloverHit() {
    playTone(1300, 60);  // Shorter high beep
}

void soundDrain() {
    // Descending tone (sad sound)
    playDescendingTone(1200, 400, 400);
}

void soundBonusCount() {
    playTone(1000, 50);  // Quick mid-range beep
}

void soundSpecial() {
    // Rising pitch fanfare
    playTone(523, 100);   // C
    playTone(659, 100);   // E
    playTone(784, 100);   // G
    playTone(1047, 200);  // C (octave up)
}

void soundStartup() {
    // USB insertion sound (gentle, won't scare dog)
    playTone(800, 80);    // Low beep
    delay(120);
    playTone(1000, 80);   // Slightly higher confirmation beep
}

void soundCoinInsert() {
    // Ascending chime
    playTone(1047, 80);   // C6
    delay(20);
    playTone(1319, 80);   // E6
    delay(20);
    playTone(1568, 150);  // G6
}

void soundGameStart() {
    // Exciting fanfare
    playTone(784, 100);   // G
    playTone(1047, 100);  // C
    playTone(1319, 200);  // E
}

void soundGameOver() {
    // Descending cadence
    playTone(880, 200);   // A
    delay(50);
    playTone(659, 200);   // E
    delay(50);
    playTone(523, 400);   // C
}

void soundSwitchClose() {
    playTone(1200, 30);  // Quick high beep
}

void soundSwitchOpen() {
    playTone(800, 30);   // Quick lower beep
}

// ========== MP3 MUSIC FUNCTIONS ==========

// Get filename for music track (only 2 files fit in SPIFFS)
const char* getMusicFilename(MusicTrack track) {
    switch (track) {
        case MUSIC_ATTRACT:  return "/rocket_man.mp3";
        case MUSIC_START:    return "/crocodile_rock.mp3";
        case MUSIC_BONUS:    return "/crocodile_rock.mp3";
        case MUSIC_GAMEOVER: return "/rocket_man.mp3";
        case MUSIC_HISCORE:  return "/rocket_man.mp3";
        default:             return NULL;
    }
}

// Stop current music playback
void stopMusic() {
    if (mp3 && mp3->isRunning()) {
        mp3->stop();
    }
    if (file) {
        file->close();
        delete file;
        file = NULL;
    }
    musicPlaying = false;
    currentMusic = MUSIC_NONE;
}

// Start playing a music track
bool playMusic(MusicTrack track, bool loop = false) {
    const char* filename = getMusicFilename(track);
    if (!filename) {
        Serial.println("[MUSIC] Invalid track ID");
        return false;
    }
    
    Serial.print("[MUSIC] Attempting to play: ");
    Serial.println(filename);
    
    // Stop any currently playing music
    stopMusic();
    
    // Create new file source
    file = new AudioFileSourceSPIFFS(filename);
    if (!file) {
        Serial.println("[MUSIC] Failed to create file source");
        return false;
    }
    
    Serial.println("[MUSIC] File source created, starting playback...");
    
    // Start MP3 playback
    if (mp3->begin(file, out)) {
        // Update gain for this track
        if (out) {
            out->SetGain(musicVolume);
            Serial.print("[MUSIC] Output gain set to: ");
            Serial.println(musicVolume);
        }
        
        currentMusic = track;
        musicPlaying = true;
        musicShouldLoop = loop;
        
        Serial.print("[MUSIC] Decoder started! Playing: ");
        Serial.print(filename);
        if (loop) Serial.print(" (loop)");
        Serial.println();
        Serial.print("[MUSIC] isRunning: ");
        Serial.println(mp3->isRunning());
        
        return true;
    } else {
        Serial.println("[MUSIC] Failed to start MP3 playback");
        delete file;
        file = NULL;
        return false;
    }
}

// Update music player (call regularly from audio task)
void updateMusicPlayer() {
    static uint32_t loopCount = 0;
    if (mp3 && musicPlaying) {
        if (mp3->isRunning()) {
            // Feed the MP3 decoder - call repeatedly to decode audio
            if (mp3->loop()) {
                // Successfully decoded a chunk
                loopCount++;
                if (loopCount % 1000 == 0) {  // Print every 1000 loops (~1 second)
                    Serial.print("[MUSIC] Decoding... loops: ");
                    Serial.println(loopCount);
                }
            } else {
                // mp3->loop() returns false when track ends or error occurs
                Serial.println("[MUSIC] *** mp3->loop() returned FALSE ***");
                Serial.print("[MUSIC] isRunning: ");
                Serial.println(mp3->isRunning());
                Serial.print("[MUSIC] Total successful loops: ");
                Serial.println(loopCount);
                loopCount = 0;
                musicPlaying = false;
                
                // Auto-loop if enabled
                if (musicShouldLoop && currentMusic != MUSIC_NONE) {
                    Serial.println("[MUSIC] Looping track...");
                    MusicTrack loopTrack = currentMusic;
                    stopMusic();
                    delay(100);  // Brief delay before restarting
                    playMusic(loopTrack, true);
                }
            }
        } else {
            // Decoder stopped running
            musicPlaying = false;
        }
    }
    
    // Check for requested music change
    if (requestedMusic != MUSIC_NONE && requestedMusic != currentMusic) {
        MusicTrack track = requestedMusic;
        requestedMusic = MUSIC_NONE;
        
        // Attract mode loops, game events play once
        bool loop = (track == MUSIC_ATTRACT);
        if (loop) {
            Serial.println("[MUSIC] Starting looping attract music");
        } else {
            Serial.println("[MUSIC] Playing game event music (one-shot)");
        }
        playMusic(track, loop);
    }
}

// Request music change (safe to call from any task)
void requestMusic(MusicTrack track) {
    requestedMusic = track;
}

// ========== SOUND TRIGGER FUNCTIONS ==========

// Trigger a sound effect from any task (non-blocking)
// Returns true if queued successfully, false if queue full
bool triggerSound(SoundEffect soundId, uint8_t priority) {
    SoundMessage msg = {soundId, priority};
    return xQueueSend(soundQueue, &msg, 0) == pdTRUE;  // Non-blocking
}

// Trigger sound from ISR (for use in interrupt handlers)
void triggerSoundFromISR(SoundEffect soundId, uint8_t priority) {
    SoundMessage msg = {soundId, priority};
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xQueueSendFromISR(soundQueue, &msg, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// ========== AUDIO MP3 TASK ==========
// Handles MP3 playback and sound effects
// Queue-based sound effect system
void audioMP3Task(void* parameter) {
    SoundMessage msg;
    
    // Initialize DAC for sound effects
    dac_output_enable(DAC_CHANNEL_1);  // GPIO25
    dac_output_voltage(DAC_CHANNEL_1, 128);  // Center position (silence)
    
    Serial.println("[AUDIO] Audio system initialized on GPIO25");
    Serial.print("[AUDIO] Sound effects volume: ");
    Serial.println(audioVolume);
    Serial.println("[AUDIO] MP3 playback disabled - sound effects only");
    
    // Play startup sound
    delay(500);
    soundStartup();
    
    while (true) {
        // Check for sound effect requests
        if (xQueueReceive(soundQueue, &msg, pdMS_TO_TICKS(10)) == pdTRUE) {
            // Pause music briefly for sound effect (optional)
            // For now, sound effects play over music
            
            // Process sound effect based on ID
            switch (msg.soundId) {
                case SND_BUMPER:
                    soundBumperHit();
                    break;
                    
                case SND_SLINGSHOT:
                    soundSlingshotHit();
                    break;
                    
                case SND_TARGET:
                    soundTargetHit();
                    break;
                    
                case SND_ROLLOVER:
                    soundRolloverHit();
                    break;
                    
                case SND_DRAIN:
                    soundDrain();
                    break;
                    
                case SND_BONUS_COUNT:
                    soundBonusCount();
                    break;
                    
                case SND_SPECIAL:
                    soundSpecial();
                    break;
                    
                case SND_STARTUP:
                    soundStartup();
                    break;
                    
                case SND_COIN_INSERT:
                    soundCoinInsert();
                    break;
                    
                case SND_GAME_START:
                    soundGameStart();
                    break;
                    
                case SND_GAME_OVER:
                    soundGameOver();
                    break;
                    
                case SND_SWITCH_CLOSE:
                    soundSwitchClose();
                    break;
                    
                case SND_SWITCH_OPEN:
                    soundSwitchOpen();
                    break;
                    
                default:
                    // Unknown sound ID
                    break;
            }
        }
    }
}

// ========== RTOS MATRIX SCANNING TASK ==========
// Combined lamp output and switch input scanning in single row pulse
// Timing parameters:
//   - P-channel turn-off: 150µs (settling before next row)
//   - P-channel turn-on: 150µs (settling after row activation)
//   - Lamp illumination: 400µs (total row active time ~600µs)
//   - Switch read: during final portion of row pulse (no extra time)
//   - Total scan period: 5ms (200Hz) for 8 rows
void matrixTask(void* parameter) {
    const TickType_t xFrequency = pdMS_TO_TICKS(5); // 5ms per scan (200Hz)
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while (true) {
        // Scan all 8 rows - each row drives both lamps AND reads switches
        for (uint8_t row = 0; row < NUM_LAMP_ROWS; row++) {
            // 1. Activate current row (HIGH for P-channel MOSFET)
            digitalWrite(rowPins[row], HIGH);
            delayMicroseconds(100);  // Reduced: P-channel turn-on + settling (improved gate circuit)

            // 2. Build shift register state for this row
            // N-channel: SR bit LOW = MOSFET OFF = column HIGH (idle)
            //            SR bit HIGH = MOSFET ON = column LOW (lamp on)
            uint16_t sr = 0; // Start with all bits LOW → N-ch OFF → columns HIGH
            
            // Set bits HIGH for lamps that should light → N-ch ON → columns LOW
            for (uint8_t col = 0; col < NUM_LAMP_COLS; col++) {
                if (lampMatrix[row][col]) {
                    sr |= (1 << col);  // Set bit HIGH → N-ch ON → column LOW
                }
            }
            
            // Solenoid states (bits 5-9) - Active HIGH
            if (solenoidState[0]) sr |= (1 << SR_BIT_SOL0);
            if (solenoidState[1]) sr |= (1 << SR_BIT_SOL1);
            if (solenoidState[2]) sr |= (1 << SR_BIT_SOL2);
            if (solenoidState[3]) sr |= (1 << SR_BIT_SOL3);
            if (solenoidState[4]) sr |= (1 << SR_BIT_SOL4);
            
            // Status LED states (bits 11-12) - Active HIGH
            if (statusLedState[0]) sr |= (1 << SR_BIT_LED9);
            if (statusLedState[1]) sr |= (1 << SR_BIT_LED10);
            
            // Update shift register
            if (shiftRegisterState != sr) {
                shiftRegisterState = sr;
                updateShiftRegister();
            }

            // 3. Hold row active for lamp illumination (500µs for brighter lamps)
            delayMicroseconds(500);

            // 4. Set all lamp columns back to idle (HIGH) before reading switches
            shiftRegisterState &= ~0x1F; // Clear bits → N-ch OFF → columns HIGH
            updateShiftRegister();
            
            // Allow switch lines to settle after MOSFET turn-off
            // MOSFET switching creates ~15µs spikes (up to 2.5V with all lamps on)
            delayMicroseconds(30);  // Wait for ringing to decay below threshold
            
            // 5. Read all switch columns for this row (while row still active)
            // GPIO34-39 have external pull-downs (or floating low)
            // Switch closed + row HIGH = column HIGH = true (CLOSED)
            // Switch open or row LOW = column LOW = false (OPEN)
            for (uint8_t col = 0; col < NUM_SWITCH_COLS; col++) {
                switchRaw[row][col] = digitalRead(switchColPins[col]);  // HIGH = closed, LOW = open
            }
            
            // 6. Deactivate current row
            digitalWrite(rowPins[row], LOW);
            delayMicroseconds(100);  // Reduced: P-channel turn-off (improved gate circuit)
        }
        
        vTaskDelayUntil(&xLastWakeTime, xFrequency); // Yield to RTOS until next scan
    }
}

// Helper function to set lamp state
void setLamp(uint8_t row, uint8_t col, bool state) {
    if (row >= NUM_LAMP_ROWS || col >= NUM_LAMP_COLS) return;
    lampMatrix[row][col] = state;
}

// ========================================
// OTA TASK - Priority 4 (above game, below WiFi)
// ========================================
void ota_task(void *pvParameter) {
    Serial.printf("[OTA] OTA task started on Core %d with priority %d\n", xPortGetCoreID(), uxTaskPriorityGet(NULL));
    Serial.println("[OTA] This is BELOW WiFi stack (priority 23) to prevent starvation");
    
    // Wait for WiFi to be fully stable
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    
    // Configure OTA from within the task
    ArduinoOTA.setHostname(ota_hostname);
    ArduinoOTA.setPassword(ota_password);
    
    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("\n========================================");
        Serial.println("[OTA] *** START CALLBACK TRIGGERED ***");
        Serial.println("========================================");
        Serial.println("[OTA] Update type: " + type);
        
        // Show initial heap status
        Serial.printf("[OTA] Initial free heap: %d bytes\n", ESP.getFreeHeap());
        Serial.printf("[OTA] Largest free block: %d bytes\n", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        Serial.printf("[OTA] Total free heap: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
        
        // CRITICAL: DELETE all tasks to free heap and stop flash access
        Serial.println("\n[OTA] Deleting all game tasks to free memory...");
        
        if (matrixTaskHandle != NULL) {
            Serial.println("[OTA]   Deleting matrixTask...");
            vTaskDelete(matrixTaskHandle);
            matrixTaskHandle = NULL;
            Serial.printf("[OTA]   After matrix: %d bytes free\n", ESP.getFreeHeap());
        }
        
        if (displayTaskHandle != NULL) {
            Serial.println("[OTA]   Deleting displayTask...");
            vTaskDelete(displayTaskHandle);
            displayTaskHandle = NULL;
            Serial.printf("[OTA]   After display: %d bytes free\n", ESP.getFreeHeap());
        }
        
        if (ioServiceTaskHandle != NULL) {
            Serial.println("[OTA]   Deleting ioServiceTask...");
            vTaskDelete(ioServiceTaskHandle);
            ioServiceTaskHandle = NULL;
            Serial.printf("[OTA]   After ioService: %d bytes free\n", ESP.getFreeHeap());
        }
        
        if (gameLogicTaskHandle != NULL) {
            Serial.println("[OTA]   Deleting gameLogicTask...");
            vTaskDelete(gameLogicTaskHandle);
            gameLogicTaskHandle = NULL;
            Serial.printf("[OTA]   After gameLogic: %d bytes free\n", ESP.getFreeHeap());
        }
        
        Serial.println("\n[OTA] Waiting for RTOS cleanup...");
        vTaskDelay(100 / portTICK_PERIOD_MS);
        
        Serial.println("\n[OTA] FINAL HEAP STATUS BEFORE Update.begin():");
        Serial.printf("[OTA]   Free heap: %d bytes\n", ESP.getFreeHeap());
        Serial.printf("[OTA]   Largest block: %d bytes\n", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        Serial.printf("[OTA]   Total free: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
        Serial.printf("[OTA]   Min free ever: %d bytes\n", ESP.getMinFreeHeap());
        
        Serial.println("\n[OTA] Tasks deleted. Proceeding to Update.begin()...");
        Serial.flush();  // Make sure all output is sent before OTA begins
    });
    
    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA] *** END CALLBACK TRIGGERED ***");
        Serial.println("[OTA] Update complete!");
    });
    
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static uint8_t lastPercent = 0;
        uint8_t percent = (progress / (total / 100));
        if (percent != lastPercent && percent % 10 == 0) {
            Serial.printf("[OTA] Progress: %u%%\n", percent);
            lastPercent = percent;
        }
    });
    
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.println("\n========================================");
        Serial.println("[OTA] *** ERROR CALLBACK TRIGGERED ***");
        Serial.println("========================================");
        Serial.printf("[OTA] Error code: %u\n", error);
        Serial.printf("[OTA] Error name: ");
        if (error == OTA_AUTH_ERROR) Serial.println("OTA_AUTH_ERROR (Auth Failed)");
        else if (error == OTA_BEGIN_ERROR) {
            Serial.println("OTA_BEGIN_ERROR (Update.begin() Failed)");
            Serial.println("\n[OTA] BEGIN ERROR DIAGNOSTICS:");
            Serial.printf("[OTA]   Current free heap: %d bytes\n", ESP.getFreeHeap());
            Serial.printf("[OTA]   Largest free block: %d bytes\n", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
            Serial.printf("[OTA]   Total free heap: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
            Serial.printf("[OTA]   Required for ~800KB update: ~1MB contiguous space\n");
            Serial.println("\n[OTA] NOTE: Update.begin() needs large contiguous heap block!");
            Serial.println("[OTA]       Heap fragmentation may be the issue.");
        }
        else if (error == OTA_CONNECT_ERROR) Serial.println("OTA_CONNECT_ERROR (Connect Failed)");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("OTA_RECEIVE_ERROR (Receive Failed)");
        else if (error == OTA_END_ERROR) Serial.println("OTA_END_ERROR (End Failed)");
        else Serial.printf("Unknown error: %u\n", error);
        
        Serial.println("\n[OTA] System will continue running without update.");
        Serial.println("========================================\n");
        Serial.flush();
    });
    
    // CRITICAL: Increase timeout for RTOS-heavy workloads
    ArduinoOTA.setTimeout(10000);  // 10 seconds per packet
    
    // Start OTA service from within this task
    ArduinoOTA.begin();
    Serial.println("[OTA] ArduinoOTA.begin() called from OTA task");
    Serial.println("[OTA] UDP server should be listening on port 3232");
    Serial.println("[OTA] Entering handle() loop with 10ms delays...");
    
    while (true) {
        // Handle OTA - WiFi stack gets priority when needed
        ArduinoOTA.handle();
        vTaskDelay(10 / portTICK_PERIOD_MS);  // 10ms delay to prevent starving WiFi
    }
}

void setup() {
    // ========== FIRMWARE VERSION INDICATOR ==========
    Serial.begin(115200);
    delay(100);
    Serial.println("\n\n");
    Serial.println("========================================");
    Serial.println("  FIRMWARE BUILD: 2026-02-04-DEBUG-V2");
    Serial.println("  WITH EXTERNAL FLASH OTA & DEBUG");
    Serial.println("========================================\n");
    
    // ========== CRITICAL: DISABLE SOLENOIDS IMMEDIATELY ==========
    // Initialize shift register pins FIRST to prevent solenoid firing during boot
    pinMode(SR_DATA, OUTPUT);
    pinMode(SR_CLOCK, OUTPUT);
    pinMode(SR_LATCH, OUTPUT);
    digitalWrite(SR_DATA, LOW);
    digitalWrite(SR_CLOCK, LOW);
    digitalWrite(SR_LATCH, LOW);
    
    // Immediately clear shift register to all zeros (solenoids and lamps OFF)
    shiftRegisterState = 0;
    updateShiftRegister();
    
    // Initialize all row pins LOW (deactivated) to prevent matrix activation
    for (uint8_t i = 0; i < NUM_LAMP_ROWS; i++) {
        pinMode(rowPins[i], OUTPUT);
        digitalWrite(rowPins[i], LOW);
    }
    
    // Now safe to start serial and continue initialization
    Serial.begin(115200);
    delay(1000);
    
    // ========== WiFi & OTA SETUP ==========
    Serial.println("\n========================================");
    Serial.println("  WiFi & OTA Initialization");
    Serial.println("========================================");
    
    // Connect to WiFi
    Serial.print("Connecting to WiFi: ");
    Serial.println(wifi_ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid, wifi_password);
    
    // Wait up to 10 seconds for connection
    uint8_t wifi_attempts = 0;
    while (WiFi.status() != WL_CONNECTED && wifi_attempts < 20) {
        delay(500);
        Serial.print(".");
        wifi_attempts++;
    }
    Serial.println();
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("WiFi connected! IP: ");
        Serial.println(WiFi.localIP());
        
        // Setup ArduinoOTA (will be handled in loop())
        ArduinoOTA.setHostname(ota_hostname);
        ArduinoOTA.setPassword(ota_password);
        ArduinoOTA.setTimeout(10000);
        
        // OTA callbacks remain the same
        ArduinoOTA.onStart([]() {
            Serial.println("\n[OTA] Update starting...");
        });
        
        ArduinoOTA.onEnd([]() {
            Serial.println("\n[OTA] Update complete!");
        });
        
        ArduinoOTA.onError([](ota_error_t error) {
            Serial.printf("\n[OTA] Error[%u]: ", error);
        });
        
        ArduinoOTA.begin();
        Serial.print("[OTA] ArduinoOTA initialized. Hostname: ");
        Serial.print(ota_hostname);
        Serial.println(".local");
    } else {
        Serial.println("[WiFi] Failed to connect - OTA disabled");
        Serial.println("[WiFi] Will continue with USB upload only");
    }
    Serial.println("========================================\n");
    
    // Disable task watchdog to prevent audio playback interruptions
    disableCore0WDT();
    disableCore1WDT();
    
    Serial.println("\n========================================");
    Serial.println("  Captain Fantastic - RTOS Firmware");
    Serial.println("  Matrix Scanner with MOSFET Fix");
    Serial.println("========================================\n");
    Serial.println("NOTE: Shift register cleared immediately to prevent solenoid firing");
    Serial.println("      (Rev 1 will use OE pin for proper output disable during boot)\n");
    
    Serial.println("Initializing row pins (M1-M8)... [DONE]");
    Serial.println("Initializing shift register... [DONE]");
    
    // Initialize switch input pins
    // NOTE: GPIO34-39 are INPUT-ONLY and do NOT support internal pull-ups!
    // These pins likely have external pull-down resistors
    Serial.println("Initializing switch inputs (INPUT-ONLY pins, external pull-downs)...");
    for (uint8_t i = 0; i < NUM_SWITCH_COLS; i++) {
        pinMode(switchColPins[i], INPUT);  // INPUT only - these pins don't support pullups!
    }
    
    // Initialize lamp matrix - all off
    clearAllLamps();
    
    // Test W25Q64 external flash (CS=32, CLK=33, MOSI=26, MISO=27)
    Serial.println("\nTesting W25Q64 external 8MB flash...");
    externalFlash.begin();
    Serial.println("[W25Q64] External flash initialized");
    
    // Quick verification test
    uint8_t testPattern[] = {0x5A, 0xA5, 0x55, 0xAA};
    uint8_t readBack[4];
    externalFlash.eraseSector(0x000000);
    externalFlash.writeBytes(0x000000, testPattern, 4);
    externalFlash.readBytes(0x000000, readBack, 4);
    
    bool flashOK = (readBack[0] == 0x5A && readBack[1] == 0xA5 && 
                    readBack[2] == 0x55 && readBack[3] == 0xAA);
    
    if (flashOK) {
      Serial.println("[W25Q64] Hardware test PASSED");
    } else {
      Serial.println("[W25Q64] ERROR: Hardware test FAILED - OTA will not work!");
    }
    
    // ========== EXTERNAL FLASH OTA SETUP ==========
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n========================================");
        Serial.println("  External Flash OTA Initialization");
        Serial.println("========================================");
        
        // Initialize two-stage OTA system
        extOTA = new ExternalFlashOTA(&externalFlash);
        extOTA->begin();
        Serial.println("[OTA] ExternalFlashOTA initialized successfully");
        
        // Check for pending firmware
        if (extOTA->hasPendingFirmware()) {
            Serial.println("[OTA] WARNING: Pending firmware detected!");
            Serial.println("[OTA] Visit web UI to flash or use serial command 'F'");
        }
        
        // Start HTTP server on port 80
        otaServer = new OTAHttpServer(extOTA, 80);
        otaServer->begin();
        Serial.print("[OTA] Web UI available at: http://");
        Serial.println(WiFi.localIP());
        Serial.println("[OTA] Upload firmware via web browser");
        
        Serial.println("========================================\n");
    } else {
        Serial.println("[OTA] WiFi not connected - External Flash OTA disabled\n");
    }
    
    // Initialize HT16K33 7-segment display
    Serial.println("\nInitializing display system...");
    Wire.begin();  // Initialize I2C bus first
    delay(100);
    initDisplay();
    Serial.println("Display init complete");
    
    currentScore = 123456;  // Test score
    Serial.println("Score set to 123456");
    
    // Lamps will be cycled by Game Logic task for verification
    Serial.println("\nLamp verification will start after tasks are created");
    
    // Ensure all solenoids are OFF
    for (uint8_t i = 0; i < 5; i++) {
        solenoidState[i] = false;
    }
    
    // Status LEDs - LED2 (green) will heartbeat in I/O Service Task
    Serial.println("\nStatus LEDs:");
    Serial.println("  LED9 (red) OFF");
    Serial.println("  LED10 (green) will heartbeat");
    statusLedState[0] = false;  // LED9 red OFF
    statusLedState[1] = false;  // LED10 green - controlled by heartbeat
    
    // Create sound queue
    Serial.println("\nCreating sound queue...");
    soundQueue = xQueueCreate(SOUND_QUEUE_LENGTH, sizeof(SoundMessage));
    if (soundQueue == NULL) {
        Serial.println("ERROR: Failed to create sound queue!");
    } else {
        Serial.println("Sound queue created successfully");
    }
    
    // Create display update task on Core 0
    Serial.println("\nCreating display task on Core 0...");
    BaseType_t displayTaskCreated = xTaskCreatePinnedToCore(
        displayTask,          // Task function
        "DisplayTask",        // Task name
        2048,                 // Stack size
        NULL,                 // Parameters
        1,                    // Priority (lowest)
        &displayTaskHandle,   // Task handle
        0                     // Core 0
    );
    if (displayTaskCreated == pdPASS) {
        Serial.println("Display task created successfully");
    } else {
        Serial.println("ERROR: Failed to create display task!");
    }
    
    // Create I/O Service task on Core 0
    Serial.println("Creating I/O Service task on Core 0...");
    BaseType_t ioTaskCreated = xTaskCreatePinnedToCore(
        ioServiceTask,        // Task function
        "IOServiceTask",      // Task name
        2048,                 // Stack size
        NULL,                 // Parameters
        2,                    // Priority (medium-high)
        &ioServiceTaskHandle, // Task handle
        0                     // Core 0
    );
    if (ioTaskCreated == pdPASS) {
        Serial.println("I/O Service task created successfully");
    } else {
        Serial.println("ERROR: Failed to create I/O Service task!");
    }
    
    // Create Game Logic task on Core 0
    Serial.println("Creating Game Logic task on Core 0...");
    BaseType_t gameTaskCreated = xTaskCreatePinnedToCore(
        gameLogicTask,        // Task function
        "GameLogicTask",      // Task name
        4096,                 // Stack size (larger for game logic)
        NULL,                 // Parameters
        2,                    // Priority (medium)
        &gameLogicTaskHandle, // Task handle
        0                     // Core 0
    );
    if (gameTaskCreated == pdPASS) {
        Serial.println("Game Logic task created successfully");
    } else {
        Serial.println("ERROR: Failed to create Game Logic task!");
    }
    
    // Create audio task on Core 0 - lower priority
    BaseType_t audioTaskCreated = xTaskCreatePinnedToCore(
        audioMP3Task,            // Task function
        "AudioTask",          // Task name
        4096,                 // Stack size (larger for MP3 decoding)
        NULL,                 // Parameters
        1,                    // Priority (lower than game logic)
        &audioMP3TaskHandle,     // Task handle
        0                     // Core 0
    );
    if (audioTaskCreated == pdPASS) {
        Serial.println("Audio task created successfully");
    } else {
        Serial.println("ERROR: Failed to create Audio task!");
    }
    
    // Create matrix scanning task on Core 1
    BaseType_t matrixTaskCreated = xTaskCreatePinnedToCore(
        matrixTask,           // Task function
        "MatrixTask",         // Task name
        2048,                 // Stack size
        NULL,                 // Parameters
        3,                    // Priority (highest)
        &matrixTaskHandle,    // Task handle
        1                     // Core 1
    );
    if (matrixTaskCreated == pdPASS) {
        Serial.println("Matrix task created successfully");
    } else {
        Serial.println("ERROR: Failed to create matrix task!");
    }
    
    Serial.println("\nAll RTOS tasks started!");
    Serial.println("========================================");
    
    // Switch mapping mode disabled by default - use 'M' command to enable
    switchMappingMode = false;
    
    Serial.println("[DEBUG] *** setup() complete - loop() should start now ***");
    Serial.flush();
}

void loop() {
    static bool firstRun = true;
    if (firstRun) {
        Serial.println("\n[LOOP] *** loop() is now running! ***");
        firstRun = false;
    }
    
    static uint32_t lastHeartbeat = 0;
    static bool menuPrinted = false;
    
    // Handle OTA updates
    ArduinoOTA.handle();
    
    // Handle external flash OTA web server
    if (otaServer != nullptr) {
        otaServer->handleClient();
    }
    
    // Print heartbeat every 5 seconds to verify loop is running
    if (millis() - lastHeartbeat > 5000) {
        Serial.print("[LOOP] Heartbeat - millis: ");
        Serial.print(millis());
        Serial.print(", Serial.available: ");
        Serial.println(Serial.available());
        lastHeartbeat = millis();
        
        // Also print menu reminder
        if (!menuPrinted || millis() - lastHeartbeat > 30000) {
            Serial.println("\n>> Send M=SwitchMap, D=Diagnostic, ?=Help <<\n");
        }
    }
    
    // Print menu once 3 seconds after boot
    if (!menuPrinted && millis() > 3000) {
        Serial.println("\n========================================");
        Serial.println("  Captain Fantastic - RTOS Firmware");
        Serial.println("========================================");
        Serial.println("\nSERIAL COMMANDS:");
        Serial.println("  M - Switch Mapping Mode (identify switches)");
        Serial.println("  D - Enter Diagnostic Test Mode");
        Serial.println("  X - Exit Diagnostic Test Mode");
        Serial.println("  ? - Show command menu");
        Serial.println("========================================\n");
        menuPrinted = true;
    }
    
    // Check for serial commands
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        
        Serial.print("[DEBUG] Raw char received: 0x");
        Serial.print(cmd, HEX);
        Serial.print(" ('");
        Serial.print(cmd);
        Serial.println("')");
        
        // Flush any trailing characters (like \n or \r)
        while (Serial.available()) {
            Serial.read();
        }
        
        // Ignore line endings and whitespace
        if (cmd == '\n' || cmd == '\r' || cmd == ' ' || cmd == '\t') {
            delay(100);
            return;
        }
        
        Serial.print(">>> Processing command: ");
        Serial.println(cmd);
        
        switch (cmd) {
            case 'M':  // Enter switch mapping mode
            case 'm':
                switchMappingMode = !switchMappingMode;
                if (switchMappingMode) {
                    Serial.println("\n===== SWITCH MAPPING MODE ENABLED =====");
                    Serial.println("Activate each switch to see its row/column");
                    Serial.println("Send 'M' again to disable");
                } else {
                    Serial.println("\n===== SWITCH MAPPING MODE DISABLED =====");
                }
                break;
                
            case 'D':  // Enter diagnostic mode
            case 'd':
                if (!diagnosticMode) {
                    diagnosticMode = true;
                    diagnosticStep = 0;
                    Serial.println("\n===== ENTERING DIAGNOSTIC MODE =====");
                    Serial.println("Bally Series II Test Procedure");
                    Serial.println("Send 'X' to exit diagnostic mode");
                } else {
                    Serial.println("Already in diagnostic mode");
                }
                break;
                
            case 'X':  // Exit diagnostic mode
            case 'x':
                if (diagnosticMode) {
                    diagnosticMode = false;
                    diagnosticStep = 0;
                    Serial.println("\n===== EXITING DIAGNOSTIC MODE =====");
                    Serial.println("Returning to attract mode");
                    // Clear all solenoids
                    for (int i = 0; i < NUM_SOLENOIDS; i++) {
                        solenoidState[i] = false;
                    }
                }
                break;
                
            case '?':  // Help menu
            case 'h':
            case 'H':
                Serial.println("\n===== COMMAND MENU =====");
                Serial.println("M - Toggle Switch Mapping Mode");
                Serial.println("D - Enter Diagnostic Test Mode");
                Serial.println("X - Exit Diagnostic Test Mode");
                Serial.println("F - Flash pending firmware from external flash");
                Serial.println("? - Show this menu");
                Serial.println("========================");
                break;
                
            case 'F':  // Flash pending firmware
            case 'f':
                if (extOTA != nullptr && extOTA->hasPendingFirmware()) {
                    Serial.println("\n===== FLASHING FIRMWARE FROM EXTERNAL FLASH =====");
                    Serial.println("WARNING: Device will reboot after flashing!");
                    delay(1000);
                    
                    if (extOTA->flashFromExternalFlash()) {
                        Serial.println("[OTA] Flash successful! Rebooting...");
                        delay(500);
                        ESP.restart();
                    } else {
                        Serial.println("[OTA] Flash failed - check serial output");
                    }
                } else if (extOTA == nullptr) {
                    Serial.println("[OTA] External Flash OTA not initialized");
                } else {
                    Serial.println("[OTA] No pending firmware to flash");
                    Serial.println("[OTA] Upload firmware via web UI first");
                }
                break;
                
            default:
                Serial.println("Unknown command - send '?' for help");
                break;
        }
    }
    
    // Main loop just yields to scheduler
    delay(100);
}
