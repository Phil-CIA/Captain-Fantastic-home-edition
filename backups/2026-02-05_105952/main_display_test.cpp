/*
 * Display System Test - Captain Fantastic Home Edition
 * 
 * VIRTUAL DISPLAY BUFFER DEMONSTRATION:
 * 
 * This test demonstrates the pixel/segment matrix approach:
 * 1. Creates a 30-digit virtual buffer (5 displays × 6 digits each)
 * 2. Game logic writes segment patterns to virtual buffer
 * 3. Display refresh slices buffer chunks to physical 4-digit displays
 * 4. Each display shows 4 of its 6 allocated virtual digits
 * 
 * BUFFER LAYOUT:
 * Digits  0-5:  Player 1 Score (shows rightmost 4 = positions 2-5)
 * Digits  6-11: Player 2 Score (shows rightmost 4 = positions 8-11)
 * Digits 12-17: Player 3 Score (shows rightmost 4 = positions 14-17)
 * Digits 18-23: Player 4 Score (shows rightmost 4 = positions 20-23)
 * Digits 24-29: Status Display (shows all 6 = positions 24-29)
 * 
 * EXAMPLE:
 * Score 123456 written to positions 0-5
 * Physical display shows positions 2-5 = "3456" (default right-align)
 * Can scroll to show positions 0-3 = "1234" (left portion)
 */

#include <Arduino.h>
#include <Wire.h>
#include "displayMux.h"
#include "displays.h"

// I2C pins (same as MCP23017 solenoid controller)
#define SDA_PIN 21
#define SCL_PIN 22

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n========================================");
    Serial.println(" Captain Fantastic - Virtual Display ");
    Serial.println("    Buffer System Test");
    Serial.println("========================================\n");
    
    Serial.println("ARCHITECTURE:");
    Serial.println("  30-digit virtual buffer");
    Serial.println("  5 physical displays × 4 digits each");
    Serial.println("  Each display allocated 6 virtual digits");
    Serial.println("  Shows 4 of 6 digits (scrollable)\n");
    
    // Initialize I2C bus
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000);  // 100kHz I2C speed
    
    Serial.println("I2C bus initialized:");
    Serial.print("  SDA: GPIO");
    Serial.println(SDA_PIN);
    Serial.print("  SCL: GPIO");
    Serial.println(SCL_PIN);
    Serial.println();
    
    // Initialize display system
    initDisplays();
    
    // Run test sequence
    testDisplaySequence();
    
    Serial.println("\n--- Setup Complete ---");
    Serial.println("Starting virtual buffer demo loop...\n");
    Serial.println("Watch the virtual buffer update and slice to physical displays!\n");
}

void loop() {
    static unsigned long lastUpdate = 0;
    static uint8_t demoStep = 0;
    
    // Update displays every 3 seconds
    if (millis() - lastUpdate >= 3000) {
        lastUpdate = millis();
        
        Serial.println("\n==========================================");
        Serial.print("=== Demo Step ");
        Serial.print(demoStep + 1);
        Serial.println(" ===");
        
        switch (demoStep) {
            case 0:
                Serial.println("ACTION: Clearing all displays");
                clearAllDisplays();
                break;
                
            case 1:
                Serial.println("ACTION: Player 1 scores 5000 points");
                addToPlayerScore(1, 5000);
                setCurrentPlayer(1);
                setBallNumber(1);
                break;
                
            case 2:
                Serial.println("ACTION: Player 1 scores up to 123456 (full 6 digits!)");
                setPlayerScore(1, 123456);
                setBonusValue(2000);
                break;
                
            case 3:
                Serial.println("ACTION: Player 2 joins with 50000");
                setCurrentPlayer(2);
                addToPlayerScore(2, 50000);
                break;
                
            case 4:
                Serial.println("ACTION: All 4 players have scores");
                setPlayerScore(1, 987654);  // 6 digits
                setPlayerScore(2, 234567);  // 6 digits
                setPlayerScore(3, 12345);   // 5 digits
                setPlayerScore(4, 6789);    // 4 digits
                setCurrentPlayer(3);
                setBallNumber(2);
                break;
                
            case 5:
                Serial.println("ACTION: High scores!");
                setPlayerScore(1, 999999);  // Maximum!
                setPlayerScore(2, 888888);
                setPlayerScore(3, 777777);
                setPlayerScore(4, 666666);
                setBonusValue(9999);
                setBallNumber(5);
                setCurrentPlayer(4);
                break;
                
            case 6:
                Serial.println("ACTION: Testing scroll offsets");
                Serial.println("Player 1 (999999) shown at different scroll positions:");
                
                // Show all 3 scroll positions
                for (uint8_t offset = 0; offset <= 2; offset++) {
                    Serial.print("\n  Scroll offset ");
                    Serial.print(offset);
                    Serial.print(" shows: ");
                    setScrollOffset(0, offset);
                    displayState.needsUpdate = true;
                    updateAllDisplays();
                    delay(1500);
                }
                
                // Reset to default
                setScrollOffset(0, 2);
                break;
                
            case 7:
                Serial.println("ACTION: Incremental scoring simulation");
                Serial.println("Watch Player 1 count up from 0 to 54321...");
                
                for (uint32_t score = 0; score <= 54321; score += 10000) {
                    setPlayerScore(1, score);
                    updateAllDisplays();
                    printVirtualBuffer();
                    delay(800);
                }
                break;
                
            default:
                // Loop back
                demoStep = -1;
                Serial.println("ACTION: Demo loop complete, restarting...");
                delay(2000);
                break;
        }
        
        // Update all displays from virtual buffer
        if (demoStep != 6) {  // Skip if we already updated in scroll demo
            updateAllDisplays();
        }
        
        // Print virtual buffer state
        if (demoStep != 7) {  // Skip during rapid counting
            printVirtualBuffer();
        }
        
        Serial.println("==========================================");
        
        demoStep++;
    }
    
    // Small delay to prevent CPU hogging
    delay(10);
}
