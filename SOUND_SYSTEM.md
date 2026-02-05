# Sound System - LM384N Audio Amplifier

## Overview
Major upgrade from original Captain Fantastic sound system using modern ESP32 DAC output with LM384N power amplifier - a serious 5.5W amplifier for authentic arcade sound!

## Hardware Configuration

### ESP32 Audio Output
- **GPIO25** - DAC Channel 1 (8-bit DAC, 0-3.3V output)
- Built-in Digital-to-Analog Converter
- Sample rate: Up to 100kHz
- Resolution: 8-bit (0-255 values)

### LM384N Audio Amplifier
- **Part Number:** LM384N/NOPB (Texas Instruments)
- **Supply Voltage:** 12V to 28V (typically 18V for pinball)
- **Output Power:** 5W continuous, 5.5W typical @ 18V into 8Ω
- **Gain:** Fixed at 34dB (50×)
- **Quiescent Current:** 7mA typical
- **THD:** 0.2% typical @ 2.5W
- **Package:** TO-220 (5-pin)
- **Heatsink:** Required for >2W operation

## Circuit Design

### LM384N Pin Configuration (TO-220, 5-pin)
```
Front view (tab up, pins down):

Pin 1: Bypass      [1µF ceramic or tantalum to GND]
Pin 2: Input (-)   [Connect to GND, or to Pin 4 via voltage divider for gain control]
Pin 3: Input (+)   [Signal from ESP32 via 10µF coupling cap]
Pin 4: Output      [To speaker via 1000µF-2200µF cap, heatsink this tab!]
Pin 5: Vcc (+)     [+18V typical, 12V-28V range]

Note: Pin 4 (Output) is also connected to the metal tab - MUST heatsink!
```

### Recommended Circuit (Full Power)
```
Power Supply:
- Use pinball machine 18V rail (or regulated 18V)
- 1000µF electrolytic bulk capacitor near LM384N
- 0.1µF ceramic bypass on Vcc (Pin 5 to GND)

Input Stage:
- ESP32 GPIO25 (DAC) → 10kΩ volume pot → 10µF coupling cap (+) → Pin 3
- Pin 2 (inverting input) → GND for full gain (34dB)
- Pin 1 (bypass) → 1µF ceramic or tantalum → GND

Output Stage:
- Pin 4 (output) → 1000µF-2200µF electrolytic (+) → 8Ω speaker
- Speaker return → GND
- Zobel network (2.7Ω + 0.1µF) across speaker terminals for stability

Heatsinking:
- TO-220 heatsink rated for 5W @ 25°C ambient
- Thermal pad or compound between tab and heatsink
- Tab is connected to Pin 4 (output) - ISOLATE from GND if heatsink grounded!

Components List:
- 10µF electrolytic (input coupling, 25V+)
- 1µF ceramic or tantalum (bypass, Pin 1)
- 1000µF-2200µF electrolytic (output coupling, 25V+)
- 1000µF electrolytic (power supply bulk, 35V+)
- 0.1µF ceramic (Vcc bypass)
- 2.7Ω resistor (Zobel network, 1/2W)
- 0.1µF film or ceramic (Zobel network)
- 10kΩ linear potentiometer (volume control, optional)
- TO-220 heatsink

Speaker:
- 8Ω, 5W-10W rated (this amp has real power!)
- 4" or larger cone diameter for good bass
- Consider quality speaker for arcade sound
- Enclosure or baffle recommended for best bass response
```

### Pinout Diagram
```
        TO-220 Package (5-pin)
        
     +-----------------+
     |  LM384N         |
     |  [Heatsink Tab] |  ← Pin 4 (Output)
     +-----------------+
      | | | | |
      1 2 3 4 5
      
Pin 1: Bypass (1µF to GND)
Pin 2: Input (-) (to GND for full gain)
Pin 3: Input (+) (signal from ESP32)
Pin 4: Output (to speaker, HEATSINK!)
Pin 5: Vcc (+18V)
```

## ESP32 DAC Programming

### Basic DAC Output (Tone Generation)
```cpp
#define AUDIO_PIN 25  // GPIO25 - DAC1

void setup() {
    // DAC doesn't need pinMode - handled by dacWrite()
}

void playTone(uint16_t frequency, uint16_t duration_ms) {
    uint32_t period_us = 1000000 / frequency;
    uint32_t half_period = period_us / 2;
    uint32_t cycles = (frequency * duration_ms) / 1000;
    
    for (uint32_t i = 0; i < cycles; i++) {
        dacWrite(AUDIO_PIN, 200);  // High (0-255)
        delayMicroseconds(half_period);
        dacWrite(AUDIO_PIN, 55);   // Low
        delayMicroseconds(half_period);
    }
    dacWrite(AUDIO_PIN, 128);  // Return to center (silence)
}
```

### Improved: Using LEDC PWM with RC Filter
```cpp
#define AUDIO_PIN 25
#define AUDIO_CHANNEL 0
#define AUDIO_RESOLUTION 8  // 8-bit resolution
#define AUDIO_FREQ 50000    // 50kHz carrier (will be filtered)

void setupAudio() {
    ledcSetup(AUDIO_CHANNEL, AUDIO_FREQ, AUDIO_RESOLUTION);
    ledcAttachPin(AUDIO_PIN, AUDIO_CHANNEL);
    ledcWrite(AUDIO_CHANNEL, 128);  // 50% duty = silence
}

void playTone(uint16_t frequency, uint16_t duration_ms) {
    // Change PWM frequency to audio frequency
    ledcSetup(AUDIO_CHANNEL, frequency, AUDIO_RESOLUTION);
    ledcWrite(AUDIO_CHANNEL, 128);  // 50% duty for square wave
    delay(duration_ms);
    ledcWrite(AUDIO_CHANNEL, 0);    // Silence
}
```

### Advanced: Using I2S DAC for Better Quality
```cpp
#include "driver/i2s.h"

#define I2S_NUM I2S_NUM_0
#define SAMPLE_RATE 22050  // 22.05kHz

void setupI2SAudio() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_DAC_BUILT_IN),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_STAND_MSB,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };
    
    i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM, NULL);  // Use internal DAC
    i2s_set_dac_mode(I2S_DAC_CHANNEL_RIGHT_EN);  // GPIO25
}

void playI2SSound(const uint8_t* audio_data, size_t length) {
    size_t bytes_written;
    i2s_write(I2S_NUM, audio_data, length, &bytes_written, portMAX_DELAY);
}
```

## Sound Effects Library

### Pinball Sound Effects
```cpp
// Classic pinball tones
#define TONE_BUMPER      880   // A5 - Bumper hit
#define TONE_SLINGSHOT   1047  // C6 - Slingshot
#define TONE_ROLLOVER    659   // E5 - Rollover switch
#define TONE_TARGET      523   // C5 - Target hit
#define TONE_DRAIN       220   // A3 - Ball drain (sad)
#define TONE_BONUS       1319  // E6 - Bonus score
#define TONE_SPECIAL     2093  // C7 - Special/Extra ball

void soundBumperHit() {
    playTone(TONE_BUMPER, 100);  // Short, high-pitched beep
}

void soundSlingshotHit() {
    playTone(TONE_SLINGSHOT, 80);
    delay(20);
    playTone(TONE_SLINGSHOT * 1.2, 60);  // Rising tone
}

void soundDrain() {
    // Descending tone (sad sound)
    for (int freq = 880; freq >= 220; freq -= 40) {
        playTone(freq, 30);
    }
}

void soundSpecial() {
    // Ascending fanfare
    playTone(523, 100);   // C
    playTone(659, 100);   // E
    playTone(784, 100);   // G
    playTone(1047, 200);  // C (octave up)
}

void soundStartup() {
    // Power-on jingle
    playTone(523, 150);   // C
    playTone(659, 150);   // E
    playTone(784, 300);   // G
}
```

### Background Music (Simple)
```cpp
struct Note {
    uint16_t frequency;
    uint16_t duration;
};

// Example: Coin insert music
const Note coinMusic[] = {
    {1047, 100},  // C6
    {1319, 100},  // E6
    {1568, 200},  // G6
    {0, 0}        // End marker
};

void playMelody(const Note* melody) {
    int i = 0;
    while (melody[i].frequency != 0) {
        if (melody[i].frequency > 0) {
            playTone(melody[i].frequency, melody[i].duration);
        } else {
            delay(melody[i].duration);  // Rest
        }
        i++;
    }
}
```

## Integration with Game Logic

### FreeRTOS Sound Task
```cpp
QueueHandle_t soundQueue;

struct SoundMessage {
    uint8_t soundId;
    uint8_t priority;  // 0=low, 255=high (interrupt current sound)
};

void soundTask(void* parameter) {
    SoundMessage msg;
    
    while (true) {
        if (xQueueReceive(soundQueue, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg.soundId) {
                case SND_BUMPER:
                    soundBumperHit();
                    break;
                case SND_SLINGSHOT:
                    soundSlingshotHit();
                    break;
                case SND_DRAIN:
                    soundDrain();
                    break;
                case SND_SPECIAL:
                    soundSpecial();
                    break;
                // ... more sounds
            }
        }
    }
}

void triggerSound(uint8_t soundId, uint8_t priority = 0) {
    SoundMessage msg = {soundId, priority};
    xQueueSend(soundQueue, &msg, 0);  // Non-blocking
}
```

### Switch Integration Example
```cpp
void IRAM_ATTR bumperSwitch_ISR() {
    // In interrupt - just trigger sound
    uint8_t soundId = SND_BUMPER;
    xQueueSendFromISR(soundQueue, &soundId, NULL);
}
```

## Power Supply Considerations

### LM384N Power Requirements
- **Voltage:** 12V minimum, 18V typical (pinball machine supply), 28V maximum
- **Current:** Up to 300mA @ 5W output (plus quiescent 7mA)
- **Ripple:** Keep <100mV for clean audio

### Power Supply Options
1. **18V Pinball Supply** (Recommended) - Use existing lamp/solenoid supply
2. **Dedicated 18V Regulated** - Best quality, use LM7818 or switching regulator
3. **12V Supply** - Will work but reduced output power (~2.5W)

### Power Supply Filtering (From Pinball 18V Rail)
```
+18V Raw → 1000µF electrolytic (35V) → 0.1µF ceramic → LM384N Pin 5

Additional filtering if noise present:
+18V Raw → 10Ω resistor (1W) → 1000µF electrolytic → LM384N Pin 5
(The 10Ω forms RC filter with capacitor)

Keep power wiring heavy gauge (22AWG or thicker) for low impedance
```

### Heat Dissipation
```
Power dissipation ≈ 2-3W at 5W output
Heatsink required: 10-20°C/W thermal resistance
TO-220 heatsink with fins, mounted vertically for airflow
Apply thermal compound between tab and heatsink
```

### Ground Loops Prevention
- Keep audio ground separate from digital ground if possible
- Single-point ground connection (star grounding)
- Twisted pair wiring for speaker connections
- Ferrite bead on speaker wires if noise present

## Volume Control

### Hardware Volume Control
```cpp
// 10kΩ potentiometer between GPIO25 and LM386 input
// Wiper goes to 10µF coupling capacitor
// Simple, no software needed
```

### Software Volume Control
```cpp
uint8_t volume = 128;  // 0-255 range

void setVolume(uint8_t vol) {
    volume = vol;
}

void playToneWithVolume(uint16_t frequency, uint16_t duration_ms) {
    uint32_t period_us = 1000000 / frequency;
    uint32_t half_period = period_us / 2;
    uint32_t cycles = (frequency * duration_ms) / 1000;
    
    uint8_t high_val = 128 + (volume / 2);  // Center + amplitude
    uint8_t low_val = 128 - (volume / 2);   // Center - amplitude
    
    for (uint32_t i = 0; i < cycles; i++) {
        dacWrite(AUDIO_PIN, high_val);
        delayMicroseconds(half_period);
        dacWrite(AUDIO_PIN, low_val);
        delayMicroseconds(half_period);
    }
    dacWrite(AUDIO_PIN, 128);  // Return to center
}
```

## Testing Procedure

### 1. Basic LM384N Test (No ESP32)
- Connect function generator or phone audio output to LM384N Pin 3 (via 10µF cap)
- Start with LOW volume input (<100mV)
- Play test tone (1kHz)
- Verify clean audio output from speaker
- Check for distortion, oscillation, or noise
- Monitor heatsink temperature (should stay <60°C)

### 2. ESP32 DAC Test
```cpp
void setup() {
    Serial.begin(115200);
    Serial.println("DAC Test - GPIO25");
    
    // Sweep through DAC range
    for (int i = 0; i < 256; i++) {
        dacWrite(25, i);
        delay(10);
    }
    
    // Play test tone
    playTone(1000, 1000);  // 1kHz for 1 second
}
```

### 3. Full System Test
```cpp
void setup() {
    setupAudio();
    
    Serial.println("Sound System Test");
    
    delay(1000);
    soundStartup();
    delay(1000);
    soundBumperHit();
    delay(500);
    soundSlingshotHit();
    delay(500);
    soundSpecial();
    delay(500);
    soundDrain();
}
```

### 4. Oscilloscope Verification
- **GPIO25 output:** Should see clean square wave or sine approximation
- **LM386 output:** Amplified version, check for clipping
- **Speaker waveform:** May show ringing (normal with inductive load)

## Troubleshooting

### No Sound
- Check LM384N power supply (Pin 5 = +18V, Pin 4 tab = output, heatsink isolated)
- Verify GPIO25 is outputting (use oscilloscope or LED test)
- Check coupling capacitors polarity (input: + to ESP32 side, output: + to LM384N side)
- Test speaker with multimeter (should read ~8Ω DC resistance)
- Verify Pin 1 bypass cap installed (1µF to GND)

### Distorted Sound / Clipping
- Reduce input level (lower DAC amplitude or adjust volume pot)
- Check power supply voltage (needs 18V for full 5W, only 2.5W @ 12V)
- Verify adequate heatsinking (thermal shutdown if overheated)
- Check for power supply sag under load (use 1000µF+ bulk cap)
- Reduce volume - LM384N has high fixed gain (34dB)

### Hum or Buzz (60Hz/120Hz)
- Add larger power supply filter capacitor (1000µF minimum)
- Add RC filter on power (10Ω + 1000µF) if using noisy pinball supply
- Check for ground loops (single-point ground, star topology)
- Move audio input wiring away from power/solenoid wiring
- Shield input cable if running >6" from ESP32

### Oscillation (High-Frequency Squeal or Motorboating)
- CRITICAL: Add Zobel network across speaker (2.7Ω + 0.1µF film cap)
- Shorten speaker wiring (<12 inches if possible)
- Verify 1µF bypass cap on Pin 1 to GND
- Add 0.1µF ceramic from Pin 5 (Vcc) to GND very close to IC
- Check output coupling cap value (use 1000µF minimum, not <470µF)

### Thermal Shutdown / Intermittent
- Heatsink inadequate - upgrade to larger heatsink (10°C/W or better)
- Apply thermal compound between tab and heatsink
- Verify airflow around heatsink (vertical mounting helps)
- Reduce drive level if running continuous high volume
- Check ambient temperature (<40°C recommended)

### Weak Output / Low Volume
- Check power supply voltage (needs 18V for full power)
- Verify Pin 2 connected to GND (not floating)
- Check speaker impedance (8Ω recommended, 4Ω will draw more current)
- Increase input signal level from ESP32 DAC
- Verify output coupling cap large enough (1000µF+, not 100µF)

### One Channel Dead (If Stereo)
- LM384N is mono - need two ICs for stereo
- Each IC needs own heatsink and power supply filter caps
- Use GPIO25 (DAC1) and GPIO26 (DAC2) for stereo sources

## Future Enhancements

1. **WAV File Playback** - Store sound effects in SPIFFS, play via I2S
2. **MP3 Decoder** - Add DFPlayer Mini or VS1053 for music tracks
3. **Multi-Channel** - Use second DAC (GPIO26) for stereo or effects layering
4. **Dynamic Volume** - Adjust volume based on game state (attract mode quiet)
5. **Speech Synthesis** - Add Talkie library for voice callouts
6. **MIDI Playback** - Vintage pinball music using ESP32 MIDI library

## Parts List

### Essential Components:
- 1× **LM384N/NOPB** (Texas Instruments, TO-220 5-pin package)
- 1× **TO-220 Heatsink** (10-20°C/W rating, required!)
- 1× **Thermal compound** or thermal pad
- 1× **10µF electrolytic** capacitor, 25V+ (input coupling)
- 1× **1µF ceramic or tantalum** capacitor (Pin 1 bypass)
- 1× **1000µF-2200µF electrolytic**, 25V+ (output coupling)
- 1× **1000µF electrolytic**, 35V+ (power supply bulk filter)
- 1× **0.1µF ceramic** capacitor (Vcc bypass)
- 1× **8Ω speaker, 5W-10W** rated (4" diameter or larger)
- Jumper wire from GPIO25

### Recommended (Stability & Quality):
- 1× **2.7Ω resistor**, 1/2W (Zobel network)
- 1× **0.1µF film or ceramic** capacitor (Zobel network)
- 1× **10kΩ linear potentiometer** (volume control)
- 1× **10Ω resistor**, 1W (power supply RC filter, if needed)
- Additional **0.1µF ceramic** caps for decoupling

### Power Supply:
- **18V from pinball machine supply** (existing lamp/solenoid rail), OR
- **LM7818 regulator** + heatsink + 100µF caps (if making dedicated 18V), OR
- **12V minimum** (reduced power output ~2.5W)

### Hardware:
- Insulating washer for TO-220 if heatsink is grounded (output is on tab!)
- PCB or perfboard for assembly
- Heavy gauge wire for power (22AWG or thicker)
- Shielded cable for audio input (optional, reduces noise)

## Pin Assignment Summary

| Component | ESP32 Pin | Notes |
|-----------|-----------|-------|
| Audio DAC | GPIO25 | DAC1 output, 0-3.3V |
| (Future) | GPIO26 | DAC2 for stereo/dual channel |

## Original Captain Fantastic Sound System

### Vintage Circuit (LM3900 + TIP110)
The original used a two-wire system:
- **"STRIKE" pin** - Trigger/gate signal (enables sound output)
- **"TONE" pin** - Frequency select (LOW = low pitch, HIGH = high pitch)

**Circuit Flow:**
```
STRIKE ──┐
         ├─→ LM3900 Norton Op-Amps (oscillators/mixers) → TIP110 (power driver) → Speaker
TONE ────┘
```

**Sound Characteristics:**
- **Low Tone** (~600-800Hz) - Bumpers, slingshots (TONE=LOW, STRIKE pulses)
- **High Tone** (~1200-1500Hz) - Targets, rollover lanes (TONE=HIGH, STRIKE pulses)
- **Rising Pitch** - Bonus counting or special events (TONE transitions LOW→HIGH during STRIKE)
- **Duration** - Typically 80-150ms per sound

### Modern ESP32 Replication

Two approaches to replicate the original sound:

#### Approach 1: Backward Compatible (Use Original STRIKE/TONE Signals)
```cpp
// Connect original sound board outputs to ESP32 inputs
#define STRIKE_INPUT  34  // GPIO34 (input-only, ideal for sensing)
#define TONE_INPUT    35  // GPIO35 (input-only)
#define AUDIO_OUTPUT  25  // GPIO25 (DAC to LM384N)

// Frequencies tuned to match original
#define FREQ_LOW   700   // Bumper/slingshot tone
#define FREQ_HIGH  1300  // Target/rollover tone

void soundTask(void* parameter) {
    while (true) {
        if (digitalRead(STRIKE_INPUT) == HIGH) {  // Active high
            uint16_t freq = digitalRead(TONE_INPUT) ? FREQ_HIGH : FREQ_LOW;
            playTone(freq, 100);  // 100ms pulse
            
            // Wait for strike to end
            while (digitalRead(STRIKE_INPUT) == HIGH) {
                vTaskDelay(pdMS_TO_TICKS(1));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
```

#### Approach 2: Full Modern Control (Generate from Game Logic)
```cpp
// Sound effect IDs
enum SoundEffect {
    SND_BUMPER = 0,
    SND_SLINGSHOT,
    SND_TARGET,
    SND_ROLLOVER,
    SND_DRAIN,
    SND_BONUS_COUNT,
    SND_SPECIAL
};

void playCaptainFantasticSound(SoundEffect effect) {
    switch (effect) {
        case SND_BUMPER:
        case SND_SLINGSHOT:
            playTone(700, 100);  // Low buzz
            break;
            
        case SND_TARGET:
        case SND_ROLLOVER:
            playTone(1300, 80);  // High beep
            break;
            
        case SND_BONUS_COUNT:
            playTone(1000, 50);  // Quick mid-range beep
            break;
            
        case SND_SPECIAL:
            // Rising pitch fanfare
            for (int f = 700; f <= 1500; f += 50) {
                playTone(f, 40);
            }
            break;
            
        case SND_DRAIN:
            // Descending tone (sad)
            for (int f = 1200; f >= 600; f -= 50) {
                playTone(f, 30);
            }
            break;
    }
}

// Trigger from switch ISR
void IRAM_ATTR bumperSwitch_ISR() {
    SoundEffect snd = SND_BUMPER;
    xQueueSendFromISR(soundQueue, &snd, NULL);
}
```

## Advantages of LM384N for Pinball

### Why LM384N vs LM386?
1. **Power Output:** 5.5W vs 0.7W - authentic arcade volume!
2. **Uses 18V Rail:** Can run directly from pinball power supply
3. **High Gain:** Fixed 34dB gain matches low-level DAC output
4. **Robust:** TO-220 package handles heat, designed for harsh environments
5. **Clean Audio:** 0.2% THD, much better than vintage amps

### Pinball-Specific Benefits:
- Loud enough to hear over solenoids and lamp noise
- Runs on existing 18V power supply (no extra regulator needed)
- Can drive larger speaker for bass response
- Thermal protection prevents damage during long game sessions
- High current capability for dynamic punch (bumper hits, special effects)

## References

- **LM384N/NOPB Datasheet:** Texas Instruments (SNVS062)
- **ESP32 DAC:** 8-bit resolution, 0-3.3V output range
- **LAMP_VOLTAGE_NOTES.md:** PWM techniques (applicable to audio synthesis)
- **SOLENOID_DRIVER_PCHANNEL.md:** 18V power supply sharing considerations
- **combined_test_rtos.cpp:** FreeRTOS task structure for integration

---
**Last Updated:** December 5, 2025
**Status:** Documentation complete, ready for hardware implementation
**Amplifier:** LM384N/NOPB (5.5W @ 18V)
**GPIO Assignment:** GPIO25 (DAC1)
**Power:** 18V from pinball supply (shared with lamps/solenoids)
