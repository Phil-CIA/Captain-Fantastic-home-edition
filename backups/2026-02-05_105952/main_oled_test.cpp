/**
 * main_oled_test.cpp
 * 
 * Test program for SSD1306 OLED display system
 * Tests vintage 7-segment style rendering on 5 x 128x64 OLED displays
 */

#include <Arduino.h>
#include "displays.h"
#include "displayMux.h"

void setup() {
    // Initialize serial
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n========================================");
    Serial.println("Captain Fantastic - OLED Display Test");
    Serial.println("5 x SSD1306 128x64 Displays");
    Serial.println("Vintage 7-Segment Style Rendering");
    Serial.println("========================================\n");
    
    // Initialize display system
    initDisplays();
    
    delay(1000);
    
    // Run comprehensive test sequence
    testDisplaySequence();
}

void loop() {
    static unsigned long lastUpdate = 0;
    static uint32_t score = 0;
    static uint8_t currentPlayer = 1;
    
    // Update display every 2 seconds
    if (millis() - lastUpdate > 2000) {
        lastUpdate = millis();
        
        // Increment score
        score += random(100, 10000);
        if (score > MAX_SCORE) {
            score = 0;
            currentPlayer++;
            if (currentPlayer > 4) {
                currentPlayer = 1;
            }
        }
        
        // Display current player's score
        Serial.print("Player ");
        Serial.print(currentPlayer);
        Serial.print(" score: ");
        Serial.println(score);
        
        setPlayerScore(currentPlayer, score);
        displayPlayerScore(currentPlayer);
    }
    
    // Check for serial commands
    if (Serial.available()) {
        char cmd = Serial.read();
        
        switch (cmd) {
            case '0' ... '9':
                // Display a specific digit across all positions
                clearPixelBuffer();
                for (uint8_t i = 0; i < NUM_CHARS_PER_ROW; i++) {
                    renderDigit(i, cmd - '0');
                }
                updateAllDisplays();
                Serial.print("Displaying digit: ");
                Serial.println(cmd - '0');
                break;
                
            case 't':
                // Run test sequence
                Serial.println("Running test sequence...");
                testDisplaySequence();
                break;
                
            case 's':
                // Test segment rendering
                testSegmentRendering();
                break;
                
            case 'c':
                // Test character rendering
                testCharacterRendering();
                break;
                
            case 'x':
                // Clear all
                Serial.println("Clearing displays...");
                clearAllDisplays();
                score = 0;
                break;
                
            case 'b':
                // Test brightness
                Serial.println("Testing brightness levels...");
                for (uint8_t b = 0; b <= 255; b += 64) {
                    setDisplayContrast(b);
                    Serial.print("Contrast: ");
                    Serial.println(b);
                    delay(1000);
                }
                break;
                
            case 'p':
                // Print display mapping
                printDisplayMapping();
                break;
                
            case 'd':
                // Dump pixel buffer
                dumpPixelBuffer();
                break;
                
            case 'm':
                // Test multiplexer
                Serial.println("Scanning multiplexer channels...");
                scanMuxChannels();
                break;
                
            case '?':
            case 'h':
                // Help
                Serial.println("\n=== Commands ===");
                Serial.println("0-9: Display digit");
                Serial.println("t:   Run test sequence");
                Serial.println("s:   Test segments");
                Serial.println("c:   Test characters");
                Serial.println("x:   Clear displays");
                Serial.println("b:   Test brightness");
                Serial.println("p:   Print mapping");
                Serial.println("d:   Dump pixel buffer");
                Serial.println("m:   Scan multiplexer");
                Serial.println("?/h: This help");
                Serial.println("================\n");
                break;
        }
    }
}
