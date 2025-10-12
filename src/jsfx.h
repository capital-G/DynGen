#pragma once

#include <SC_PlugIn.hpp>
#include "eel2_adapter.h"

static InterfaceTable* ft;

class SC_JSFX : public SCUnit {
public:
    SC_JSFX()  {
        vm = new EEL2Adapter();
        mNumChannels = 0.0f;
        mMaxBlock = bufferSize();

        // vm = eel2_create_vm(static_cast<int>(sampleRate()), mMaxBlock);

        set_calc_function<SC_JSFX, &SC_JSFX::next>();
        next(1);
    };

private:
    EEL2Adapter* vm;
    int mNumChannels;
    int mMaxBlock;

    void next(int numSamples) {
        auto inBuf = in(0);
        auto outBuf = out(0);

        vm->process(inBuf, outBuf, numSamples);
    }
};

PluginLoad("SC_JSFX") {
    registerUnit<SC_JSFX>(inTable, "JSFX", false);
}
