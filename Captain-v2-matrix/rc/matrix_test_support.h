#ifndef MATRIX_TEST_SUPPORT_H
#define MATRIX_TEST_SUPPORT_H

#include <Arduino.h>
#include <stddef.h>

struct CaptainMatrixRuntimeView {
    uint8_t* lampRowRam;
    size_t lampRowCount;
    uint8_t* switchStateBytes;
    size_t switchByteCount;
    uint8_t* lampPulseWidthLevel;
    bool* matrixSystemEnabled;
    bool* matrixOutputEnabled;
};

void matrixTestSetup();
void matrixTestLoop(const CaptainMatrixRuntimeView& runtime, uint32_t now);
uint8_t matrixTestStatusFlags();

#endif