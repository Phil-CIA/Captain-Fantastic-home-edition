#ifndef AUDIOOUTPUTDAC_H
#define AUDIOOUTPUTDAC_H

#include "AudioOutput.h"
#include <driver/dac.h>

// Custom audio output using ESP32 DAC directly (better quality than I2S-DAC mode)
// Outputs on GPIO25 using dacWrite() - same as sound effects
class AudioOutputDAC : public AudioOutput {
public:
    AudioOutputDAC(int pin = 25) : dacChannel(pin == 25 ? DAC_CHANNEL_1 : DAC_CHANNEL_2) {
        dac_output_enable(dacChannel);
        lastSampleTime = 0;
        samplePeriodUs = 1000000 / 22050;  // Default 22050 Hz = ~45 microseconds per sample
    }
    
    virtual ~AudioOutputDAC() {
        dac_output_disable(dacChannel);
    }
    
    virtual bool begin() override {
        SetGain(1.0);
        lastSampleTime = micros();
        return true;
    }
    
    virtual bool ConsumeSample(int16_t sample[2]) override {
        // Convert 16-bit signed sample to 8-bit unsigned DAC value
        // Mix stereo to mono: (left + right) / 2
        int32_t mono = ((int32_t)sample[LEFTCHANNEL] + (int32_t)sample[RIGHTCHANNEL]) / 2;
        
        // Apply gain (using base class gain value)
        int16_t ms[2] = {(int16_t)mono, (int16_t)mono};
        MakeSampleStereo16(ms);
        mono = ms[LEFTCHANNEL];
        
        // Clamp to 16-bit range
        if (mono > 32767) mono = 32767;
        if (mono < -32768) mono = -32768;
        
        // Convert to 8-bit unsigned (0-255) for DAC
        uint8_t dacValue = (mono + 32768) >> 8;  // Shift from -32768..32767 to 0..255
        
        // Write directly to DAC
        dac_output_voltage(dacChannel, dacValue);
        
        // Simple delay for sample rate pacing (Core 1 - no IDLE watchdog)
        delayMicroseconds(45);  // 22050 Hz
        
        return true;
    }
    
    virtual bool stop() override {
        // Center DAC at silence (128)
        dac_output_voltage(dacChannel, 128);
        return true;
    }
    
    virtual bool SetRate(int hz) override {
        samplePeriodUs = 1000000 / hz;  // Convert Hz to microseconds per sample
        return true;
    }

private:
    dac_channel_t dacChannel;
    uint32_t lastSampleTime;
    uint32_t samplePeriodUs;
};

#endif
