#pragma once

#include <fstream>
#include <SC_PlugIn.hpp>
#include "eel2_adapter.h"

static InterfaceTable* ft;

class SC_JSFX : public SCUnit {
public:
    SC_JSFX()  {
        mNumInputs = static_cast<int>(in0(0));
        mNumOutputs = static_cast<int>(in0(1));

        // @todo make this dynamic
        std::string scriptPath = "/Users/scheiba/github/SC_JSFX/filter.jsfx";

        // @todo don't do I/O access in the audio thread^^
        std::ifstream file(scriptPath);
        std::string script((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        vm = new EEL2Adapter(script, mNumInputs, mNumOutputs, sampleRate());
        // mNumChannels = 0.0f;
        // mMaxBlock = bufferSize();

        // vm = eel2_create_vm(static_cast<int>(sampleRate()), mMaxBlock);

        set_calc_function<SC_JSFX, &SC_JSFX::next>();
        next(1);
    };

private:
    EEL2Adapter* vm;
    int mNumInputs;
    int mNumOutputs;

    void next(int numSamples) {
        // skip first 2 channels
        float **vmInBuf = mInBuf + 2;
        vm->process(vmInBuf, mOutBuf, numSamples);
    }
};

PluginLoad("SC_JSFX") {
    registerUnit<SC_JSFX>(inTable, "JSFX", false);
}
