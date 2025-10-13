#pragma once

#include <string>
#include <iostream>

#include "eel2/ns-eel.h"


class EEL2Adapter {
public:
    EEL2Adapter(const std::string& script, int numInputChannels, int numOutputChannels, double sampleRate) : mNumInputChannels(numInputChannels), mNumOutputChannels(numOutputChannels), mSampleRate(sampleRate)  {
        static bool eel_inited = false;
        if (!eel_inited) {
            NSEEL_init();
            eel_inited = true;
        }

        mInputs = new double*[mNumInputChannels]();
        mOutputs = new double*[mNumOutputChannels]();

        eel_state_ = NSEEL_VM_alloc();

        // obtain handles to input and output variables
        for (int i = 0; i < mNumInputChannels; i++) {
            std::string name = "in" + std::to_string(i);
            mInputs[i] = NSEEL_VM_regvar(eel_state_, name.c_str());
        }
        for (int i = 0; i < mNumOutputChannels; i++) {
            std::string name = "out" + std::to_string(i);
            mOutputs[i] = NSEEL_VM_regvar(eel_state_, name.c_str());
        }

        // define variables for the script context
        std::string header;
        header += "srate = "+std::to_string(mSampleRate) + ";\n";

        mScript = header + script;

        code_ = NSEEL_code_compile(eel_state_, mScript.c_str(), 0);
        if (!code_) {
            std::cout << "NSEEL_code_compile failed" << std::endl;
            return;
        }
        mCompiledSuccesfully = true;
    }

    ~EEL2Adapter() {
        if (code_) NSEEL_code_free(code_);
        if (eel_state_) NSEEL_VM_free(eel_state_);
        if (mInputs) delete[] mInputs;
        if (mOutputs) delete[] mOutputs;
    }

    inline void process(float** inBuf, float** outBuf, int numSamples) {
        for (int i=0; i<numSamples; i++) {
            // copy input buffer to vm - cast to double!
            for (int inChannel = 0; inChannel < mNumInputChannels; inChannel++) {
                *mInputs[inChannel] = static_cast<double>(inBuf[inChannel][i]);
            }

            NSEEL_code_execute(code_);

            // read output buffer from vm
            for (int outChannel = 0; outChannel < mNumOutputChannels; outChannel++) {
                outBuf[outChannel][i] = static_cast<float>(*mOutputs[outChannel]);
            }
        }
    }

    bool mCompiledSuccesfully = false;

private:
    NSEEL_VMCTX eel_state_ = nullptr;
    NSEEL_CODEHANDLE code_ = nullptr;

    int mNumInputChannels = 0;
    int mNumOutputChannels = 0;
    double mSampleRate = 0;

    double** mInputs;
    double** mOutputs;

    std::string mScript;
};
