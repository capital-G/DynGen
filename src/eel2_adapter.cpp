#define WDL_FFT_REALSIZE 8

#include "eel2_adapter.h"

#include "ns-eel-addfuncs.h"
#include "ns-eel-int.h"
#include "eel_fft.h"
#include "eel_mdct.h"

#include <SC_InlineBinaryOp.h>
#include <SC_InterfaceTable.h>
#include <SC_Unit.h>

// These can be empty because we do not execute code on the same VM in multiple threads.
extern "C" void NSEEL_HOSTSTUB_EnterMutex() {}
extern "C" void NSEEL_HOSTSTUB_LeaveMutex() {}

// fallback value for functions which return
// a ptr to an EEL_F such as in and out
EEL_F nullValue = double { 0.0 };

void EEL2Adapter::setup() {
    EEL_fft_register();
    EEL_mdct_register();

    NSEEL_addfunc_varparm("bufRead", 2, NSEEL_PProc_THIS, &eelBufRead);
    NSEEL_addfunc_varparm("bufReadL", 2, NSEEL_PProc_THIS, &eelBufReadL);
    NSEEL_addfunc_varparm("bufReadC", 2, NSEEL_PProc_THIS, &eelBufReadC);
    NSEEL_addfunc_varparm("bufWrite", 3, NSEEL_PProc_THIS, &eelBufWrite);

    // signal functions
    NSEEL_addfunc_varparm("clip", 2, NSEEL_PProc_THIS, &eelClip);
    NSEEL_addfunc_varparm("wrap", 2, NSEEL_PProc_THIS, &eelWrap);
    NSEEL_addfunc_varparm("fold", 2, NSEEL_PProc_THIS, &eelFold);
    NSEEL_addfunc_retval("mod", 2, NSEEL_PProc_THIS, &eelMod);
    NSEEL_addfunc_retval("lin", 3, NSEEL_PProc_THIS, &eelLininterp);
    NSEEL_addfunc_varparm("cubic", 5, NSEEL_PProc_THIS, &eelCubicinterp);

    // inputs and outputs
    NSEEL_addfunc_retptr("in", 1, NSEEL_PProc_THIS, &in);
    NSEEL_addfunc_retptr("out", 1, NSEEL_PProc_THIS, &out);
}

EEL2Adapter::EEL2Adapter(uint32 numInputChannels, uint32 numOutputChannels, int sampleRate, int blockSize, World* world,
                         Graph* parent):
    mNumInputChannels(numInputChannels),
    mNumOutputChannels(numOutputChannels),
    mSampleRate(sampleRate),
    mBlockSize(blockSize),
    mWorld(world),
    mParent(parent) {}

// this is not RT safe
bool EEL2Adapter::init(const DynGenScript& script, const int* parameterIndices, int numParamIndices) {
    mEelState = NSEEL_VM_alloc();

    // obtain handles to input and output variables
    mInputs = std::make_unique<double*[]>(mNumInputChannels);
    for (int i = 0; i < mNumInputChannels; i++) {
        std::string name = "in" + std::to_string(i);
        mInputs[i] = NSEEL_VM_regvar(mEelState, name.c_str());
    }

    mOutputs = std::make_unique<double*[]>(mNumOutputChannels);
    for (int i = 0; i < mNumOutputChannels; i++) {
        std::string name = "out" + std::to_string(i);
        mOutputs[i] = NSEEL_VM_regvar(mEelState, name.c_str());
    }
    // since the parameter indices are fixed at synth creation time,
    // we only have to get pointers to the parameters at these indices.
    // note that parameter indices are stable because parameter names
    // are append-only.
    mParameters = std::make_unique<double*[]>(numParamIndices);
    mNumParameters = numParamIndices;
    auto& parameters = script.mParameters;
    for (int i = 0; i < numParamIndices; i++) {
        auto paramIndex = parameterIndices[i];
        if (paramIndex >= 0 && paramIndex < parameters.size()) {
            mParameters[i] = NSEEL_VM_regvar(mEelState, parameters[paramIndex].c_str());
        } else {
            // ignore out-of-range parameter indices
            Print("ERROR: Parameter index %d out of range\n");
            mParameters[i] = nullptr;
        }
    }

    // set 'this' pointer for custom functions
    NSEEL_VM_SetCustomFuncThis(mEelState, this);

    // set our script variables
    *NSEEL_VM_regvar(mEelState, "srate") = mSampleRate;
    *NSEEL_VM_regvar(mEelState, "blockSize") = mBlockSize;
    *NSEEL_VM_regvar(mEelState, "numIn") = mNumInputChannels;
    *NSEEL_VM_regvar(mEelState, "numOut") = mNumOutputChannels;

    auto compileFlags = NSEEL_CODE_COMPILE_FLAG_COMMONFUNCS | NSEEL_CODE_COMPILE_FLAG_COMMONFUNCS_RESET
        | NSEEL_CODE_COMPILE_FLAG_NOFPSTATE;

    if (script.mSample.empty()) {
        Print("ERROR: DynGen sample code is missing\n");
        return false;
    }

    if (!script.mInit.empty()) {
        mInitCode = NSEEL_code_compile_ex(mEelState, script.mInit.c_str(), 0, compileFlags);
        if (!mInitCode) {
            Print("ERROR: DynGen @init compile error: %s\n", NSEEL_code_getcodeerror(mEelState));
            return false;
        }
    }

    if (!script.mBlock.empty()) {
        mBlockCode = NSEEL_code_compile_ex(mEelState, script.mBlock.c_str(), 0, compileFlags);
        if (!mBlockCode) {
            Print("ERROR: DynGen @block compile error %s\n", NSEEL_code_getcodeerror(mEelState));
            return false;
        }
    }

    mSampleCode = NSEEL_code_compile_ex(mEelState, script.mSample.c_str(), 0, compileFlags);
    if (!mSampleCode) {
        Print("ERROR: DynGen @sample compile error: %s\n", NSEEL_code_getcodeerror(mEelState));
        return false;
    }

    if (mInitCode) {
        NSEEL_code_execute(mInitCode);
    }
    return true;
}

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelBufRead(void* opaque, const INT_PTR numParams, EEL_F** params) {
    const auto eel2Adapter = static_cast<EEL2Adapter*>(opaque);
    const auto buf = eel2Adapter->getBuffer(static_cast<int>(*params[0]));
    if (!buf) {
        return 0.0f;
    };
    int chanOffset = 0;
    if (numParams >= 3) {
        chanOffset = getChannelOffset(buf, static_cast<int>(*params[2]));
    }
    LOCK_SNDBUF_SHARED(buf);
    return getSample(buf, chanOffset, static_cast<int>(*params[1]));
}

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelBufReadL(void* opaque, const INT_PTR numParams, EEL_F** params) {
    const auto eel2Adapter = static_cast<EEL2Adapter*>(opaque);
    const auto buf = eel2Adapter->getBuffer(static_cast<int>(*params[0]));
    if (buf == nullptr) {
        return 0.0f;
    };
    int chanOffset = 0;
    if (numParams >= 3) {
        chanOffset = getChannelOffset(buf, static_cast<int>(*params[2]));
    }

    const auto lowerIndex = static_cast<int>(*params[1]);
    const auto upperIndex = lowerIndex + 1;
    const float frac = static_cast<float>(*params[1]) - static_cast<float>(lowerIndex);

    LOCK_SNDBUF_SHARED(buf);
    return lininterp(frac, getSample(buf, chanOffset, lowerIndex), getSample(buf, chanOffset, upperIndex));
}

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelBufReadC(void* opaque, const INT_PTR numParams, EEL_F** params) {
    const auto eel2Adapter = static_cast<EEL2Adapter*>(opaque);
    const auto buf = eel2Adapter->getBuffer(static_cast<int>(*params[0]));
    if (buf == nullptr) {
        return 0.0f;
    };
    int chanOffset = 0;
    if (numParams >= 3) {
        chanOffset = getChannelOffset(buf, static_cast<int>(*params[2]));
    }

    const auto baseIndex = static_cast<int>(*params[1]);
    const float frac = static_cast<float>(*params[1]) - static_cast<float>(baseIndex);

    LOCK_SNDBUF_SHARED(buf);
    return cubicinterp(frac, getSample(buf, chanOffset, baseIndex - 1), getSample(buf, chanOffset, baseIndex),
                       getSample(buf, chanOffset, baseIndex + 1), getSample(buf, chanOffset, baseIndex + 2));
}

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelBufWrite(void* opaque, INT_PTR numParams, EEL_F** params) {
    const auto eel2Adapter = static_cast<EEL2Adapter*>(opaque);
    const auto buf = eel2Adapter->getBuffer(static_cast<int>(*params[0]));
    if (buf == nullptr) {
        return 0.0f;
    };
    const int sampleNum = static_cast<int>(*params[1]);
    if (sampleNum < 0 || sampleNum >= buf->frames) {
        return 0.0f;
    }

    int chanOffset;
    if (numParams >= 4) {
        chanOffset = static_cast<int>(*params[3]);
        if (chanOffset < 0 || chanOffset >= buf->channels) {
            return 0.0f;
        }
    } else {
        chanOffset = 0;
    }

    LOCK_SNDBUF(buf);
    buf->data[(sampleNum * buf->channels) + chanOffset] = static_cast<float>(*params[2]);
    // or should this return the old now overwritten value?
    return *params[2];
}

EEL_F_PTR NSEEL_CGEN_CALL EEL2Adapter::in(void* opaque, EEL_F* channel) {
    const auto eel2Adapter = static_cast<EEL2Adapter*>(opaque);
    if (eel2Adapter->mNumInputChannels <= 0) {
        return &nullValue;
    };
    auto numChannel = std::clamp(static_cast<int>(*channel), 0, eel2Adapter->mNumInputChannels - 1);
    return eel2Adapter->mInputs[numChannel];
}

EEL_F_PTR NSEEL_CGEN_CALL EEL2Adapter::out(void* opaque, EEL_F* channel) {
    const auto eel2Adapter = static_cast<EEL2Adapter*>(opaque);
    if (eel2Adapter->mNumOutputChannels <= 0) {
        return &nullValue;
    };
    auto numChannel = static_cast<int>(*channel);
    numChannel = std::clamp(numChannel, 0, eel2Adapter->mNumOutputChannels - 1);
    return eel2Adapter->mOutputs[numChannel];
}

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelClip(void*, const INT_PTR numParams, EEL_F** params) {
    if (numParams == 2) {
        return sc_clip2(*params[0], *params[1]);
    }
    return sc_clip(*params[0], *params[1], *params[2]);
}

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelWrap(void*, const INT_PTR numParams, EEL_F** params) {
    if (numParams == 2) {
        return sc_wrap2(*params[0], *params[1]);
    }
    return sc_wrap(*params[0], *params[1], *params[2]);
}

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelFold(void*, const INT_PTR numParams, EEL_F** params) {
    if (numParams == 2) {
        return sc_fold2(*params[0], *params[1]);
    }
    return sc_fold(*params[0], *params[1], *params[2]);
}

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelMod(void*, EEL_F* in, EEL_F* hi) { return sc_mod(*in, *hi); }

// from SC_SndBuf but adjusted for doubles - added eel_ prefix to avoid clashes from SC_SndBuf
EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelLininterp(void*, EEL_F* x, EEL_F* a, EEL_F* b) { return *a + *x * (*b - *a); }

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelCubicinterp(void*, const INT_PTR numParams, EEL_F** params) {
    // 4-point, 3rd-order Hermite (x-form)
    EEL_F c0 = *params[2];
    EEL_F c1 = 0.5f * (*params[3] - *params[1]);
    EEL_F c2 = *params[1] - 2.5f * *params[2] + 2.f * *params[3] - 0.5f * *params[4];
    EEL_F c3 = 0.5f * (*params[4] - *params[1]) + 1.5f * (*params[2] - *params[3]);

    return ((c3 * *params[0] + c2) * *params[0] + c1) * *params[0] + c0;
}


EEL2Adapter::~EEL2Adapter() {
    if (mSampleCode)
        NSEEL_code_free(mSampleCode);
    if (mInitCode)
        NSEEL_code_free(mInitCode);
    if (mBlockCode)
        NSEEL_code_free(mBlockCode);
    if (mEelState)
        NSEEL_VM_free(mEelState);
}
