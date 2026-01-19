#define WDL_FFT_REALSIZE 8

#include "eel2_adapter.h"

#include "ns-eel-addfuncs.h"
#include "ns-eel-int.h"
#include "eel_fft.h"

#include <SC_InterfaceTable.h>
#include <SC_Unit.h>

extern InterfaceTable *ft;

// define symbols for jsfx
extern "C" void NSEEL_HOSTSTUB_EnterMutex() {}
extern "C" void NSEEL_HOSTSTUB_LeaveMutex() {}

// fallback value for functions which return
// a ptr to an EEL_F such as in and out
EEL_F nullValue = double{0.0};

// this is not RT safe
bool EEL2Adapter::init(const std::string &script, char** const parameters) {
  mScript = new DynGenScript(script);
  mInputs = new double *[mNumInputChannels]();
  mOutputs = new double *[mNumOutputChannels]();
  mParameters = new double *[mNumParameters]();

  mEelState = static_cast<compileContext*>(NSEEL_VM_alloc());

  // obtain handles to input and output variables
  for (int i = 0; i < mNumInputChannels; i++) {
    std::string name = "in" + std::to_string(i);
    mInputs[i] = NSEEL_VM_regvar(mEelState, name.c_str());
  }
  for (int i = 0; i < mNumOutputChannels; i++) {
    std::string name = "out" + std::to_string(i);
    mOutputs[i] = NSEEL_VM_regvar(mEelState, name.c_str());
  }
  // since parameters are append only, and we can only reference
  // existing parameters during the init of the synth
  // it is safe and sufficient to only add references to the number of
  // parameters that were available during init time.
  for (int i = 0; i < mNumParameters; i++) {
    mParameters[i] = NSEEL_VM_regvar(mEelState, parameters[i]);
  }

  // eel2 functions
  NSEEL_VM_SetCustomFuncThis(mEelState, this);
  EEL_fft_register();
  NSEEL_addfunc_varparm("bufRead", 2, NSEEL_PProc_THIS, &eelReadBuf);
  NSEEL_addfunc_varparm("bufReadL", 2, NSEEL_PProc_THIS, &eelReadBufL);
  NSEEL_addfunc_varparm("bufReadC", 2, NSEEL_PProc_THIS, &eelReadBufC);
  NSEEL_addfunc_varparm("bufWrite", 3, NSEEL_PProc_THIS, &eelWriteBuf);
  NSEEL_addfunc_retptr("in", 1, NSEEL_PProc_THIS, &in);
  NSEEL_addfunc_retptr("out", 1, NSEEL_PProc_THIS, &out);

  // eel2 variables
  auto eelSrate = NSEEL_VM_regvar(mEelState, "srate");
  *eelSrate = mSampleRate;
  auto eelBlockSize = NSEEL_VM_regvar(mEelState, "blockSize");
  *eelBlockSize = mBlockSize;

  auto compileFlags =
    NSEEL_CODE_COMPILE_FLAG_COMMONFUNCS |
    NSEEL_CODE_COMPILE_FLAG_COMMONFUNCS_RESET |
    NSEEL_CODE_COMPILE_FLAG_NOFPSTATE;

  if (mScript->sample.empty()) {
    Print("ERROR: DynGen sample code is missing\n");
    return false;
  }

  if (!mScript->init.empty()) {
    mInitCode = NSEEL_code_compile_ex(mEelState, mScript->init.c_str(), 0, compileFlags);
    if (!mInitCode) {
      Print("ERROR: DynGen init compile error: %s\n", mEelState->last_error_string);
      return false;
    }
  }

  if (!mScript->block.empty()) {
    mBlockCode = NSEEL_code_compile_ex(mEelState, mScript->block.c_str(), 0, compileFlags);
    if (!mBlockCode) {
      Print("ERROR: DynGen block compile error %s\n", mEelState->last_error_string);
      return false;
    }
  }

  mSampleCode = NSEEL_code_compile_ex(mEelState, mScript->sample.c_str(), 0, compileFlags);
  if (!mSampleCode) {
    Print("ERROR: DynGen sample compile error: %s\n", mEelState->last_error_string);
    return false;
  }

  if (mInitCode) {
    NSEEL_code_execute(mInitCode);
  }
  return true;
}

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelReadBuf(void* opaque, const INT_PTR numParams, EEL_F** params) {
  const auto eel2Adapter = static_cast<EEL2Adapter*>(opaque);
  const auto buf =  eel2Adapter->getBuffer(static_cast<int>(*params[0]));
  if (!buf) {
    return 0.0f;
  };
  int chanOffset = 0;
  if (numParams>=3) {
    chanOffset = getChannelOffset(buf, static_cast<int>(*params[2]));
  }
  LOCK_SNDBUF_SHARED(buf);
  return getSample(buf, chanOffset, static_cast<int>(*params[1]));
}

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelReadBufL(void* opaque, const INT_PTR numParams, EEL_F** params) {
  const auto eel2Adapter = static_cast<EEL2Adapter*>(opaque);
  const auto buf =  eel2Adapter->getBuffer(static_cast<int>(*params[0]));
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
  return lininterp(
    frac,
    getSample(buf, chanOffset, lowerIndex),
    getSample(buf, chanOffset, upperIndex)
  );
}

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelReadBufC(void* opaque, const INT_PTR numParams, EEL_F** params) {
  const auto eel2Adapter = static_cast<EEL2Adapter*>(opaque);
  const auto buf =  eel2Adapter->getBuffer(static_cast<int>(*params[0]));
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
  return cubicinterp(
    frac,
    getSample(buf, chanOffset, baseIndex-1),
    getSample(buf, chanOffset, baseIndex),
    getSample(buf, chanOffset, baseIndex+1),
    getSample(buf, chanOffset, baseIndex+2)
  );
}

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelWriteBuf(void* opaque, INT_PTR numParams, EEL_F** params) {
  const auto eel2Adapter = static_cast<EEL2Adapter*>(opaque);
  const auto buf =  eel2Adapter->getBuffer(static_cast<int>(*params[0]));
  if (buf == nullptr) {
    return 0.0f;
  };
  const int sampleNum = static_cast<int>(*params[1]);
  if (sampleNum >= buf->frames || sampleNum < 0) {
    return 0.0f;
  }

  int chanOffset;
  if (numParams >= 4) {
    chanOffset = static_cast<int>(*params[3]);
    if (chanOffset > buf->channels || chanOffset < 0) {
      chanOffset = 0;
    }
  } else {
    chanOffset = 0;
  }

  LOCK_SNDBUF(buf);
  buf->data[(sampleNum * buf->channels) + chanOffset] = static_cast<float>(*params[2]);
  // or should this return the old now overwritten value?
  return *params[2];
}

EEL_F_PTR NSEEL_CGEN_CALL EEL2Adapter::in(void* opaque, EEL_F *channel) {
  const auto eel2Adapter = static_cast<EEL2Adapter*>(opaque);
  if (eel2Adapter->mNumInputChannels <= 0) {
    return &nullValue;
  };
  auto numChannel = std::clamp(static_cast<int>(*channel), 0, eel2Adapter->mNumInputChannels-1);
  return eel2Adapter->mInputs[numChannel];
}

EEL_F_PTR NSEEL_CGEN_CALL EEL2Adapter::out(void* opaque, EEL_F *channel) {
  const auto eel2Adapter = static_cast<EEL2Adapter*>(opaque);
  if (eel2Adapter->mNumOutputChannels <= 0) {
    return &nullValue;
  };
  auto numChannel = static_cast<int>(*channel);
  numChannel = std::clamp(numChannel, 0, eel2Adapter->mNumOutputChannels-1);
  return eel2Adapter->mOutputs[numChannel];
}

EEL2Adapter::~EEL2Adapter() {
  delete mScript;
  if (mSampleCode)
    NSEEL_code_free(mSampleCode);
  if (mInitCode)
    NSEEL_code_free(mInitCode);
  if (mBlockCode)
    NSEEL_code_free(mBlockCode);
  if (mEelState)
    // @todo delay this to NRT thread
    NSEEL_VM_free(mEelState);
  delete[] mInputs;
  delete[] mOutputs;
  delete[] mParameters;
}
