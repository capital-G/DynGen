#pragma once

#include <string>
#include <iostream>

#include "eel2/ns-eel.h"


class EEL2Adapter {
public:
    EEL2Adapter() {
        static bool eel_inited = false;
        if (!eel_inited) {
            NSEEL_init();
            eel_inited = true;
        }

        // @todo is this right?
        eel_state_ = NSEEL_VM_alloc();

        const char* script = "out = in * 0.1;";

        code_ = NSEEL_code_compile(eel_state_, script, 0);
        if (!code_) {
            std::cout << "NSEEL_code_compile failed" << std::endl;
            return;
        }

        inVar_ = NSEEL_VM_getvar(eel_state_, "in");
        outVar_ = NSEEL_VM_getvar(eel_state_, "out");
        if (!inVar_ || !outVar_) {
            std::cout << "NSEEL_VM_getvar failed" << std::endl;
        }
    }

    ~EEL2Adapter() {
        if (code_) NSEEL_code_free(code_);
        if (eel_state_) NSEEL_VM_free(eel_state_);
    }

    inline void process(const float* inBuf, float* outBuf, int numSamples) {
        for (int i=0; i<numSamples; i++) {
            *inVar_ = static_cast<double>(inBuf[i]);
            NSEEL_code_execute(code_);
            outBuf[i] = static_cast<float>(*outVar_);
        }
    }

private:
    NSEEL_VMCTX eel_state_ = nullptr;
    NSEEL_CODEHANDLE code_ = nullptr;

    double* inVar_ = nullptr;
    double* outVar_ = nullptr;
};


// extern "C" {
// typedef struct EEL2_VM EEL2_VM;
//
// EEL2_VM* eel2_create_vm(int sample_rate, int max_block_size);
// void eel2_destroy_vm(EEL2_VM* vm);
//
// int eel2_compile_jsfx(EEL2_VM* vm, const char* source);
// const char* eel2_get_last_error(EEL2_VM* vm);
//
// void eel2_process_block(
//     EEL2_VM* vm,
//     const float** in_buffers,
//     float** out_buffers,
//     int num_channels,
//     int num_samples
// );
//
// }