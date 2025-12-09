#include "eel2_adapter.h"

#include "ns-eel-addfuncs.h"
#include "ns-eel-int.h"

#include <SC_InterfaceTable.h>
#include <SC_Unit.h>

static InterfaceTable *ft;

// define symbols for jsfx
extern "C" void NSEEL_HOSTSTUB_EnterMutex() {}
extern "C" void NSEEL_HOSTSTUB_LeaveMutex() {}

// this is not RT safe
bool EEL2Adapter::init(const std::string &script) {
  mScript = new DynGenScript(script);
  mInputs = new double *[mNumInputChannels]();
  mOutputs = new double *[mNumOutputChannels]();

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

  // eel2 functions
  NSEEL_VM_SetCustomFuncThis(mEelState, this);
  NSEEL_addfunc_varparm("bufRead", 2, NSEEL_PProc_THIS, &eelReadBuf);
  NSEEL_addfunc_varparm("bufReadL", 2, NSEEL_PProc_THIS, &eelReadBufL);
  NSEEL_addfunc_varparm("bufReadC", 2, NSEEL_PProc_THIS, &eelReadBufC);
  NSEEL_addfunc_varparm("bufWrite", 3, NSEEL_PProc_THIS, &eelWriteBuf);

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
    Print("ERROR: DynGen sample code is missing");
    return false;
  }

  if (!mScript->init.empty()) {
    mInitCode = NSEEL_code_compile_ex(mEelState, mScript->init.c_str(), 0, compileFlags);
    if (!mInitCode) {
      std::cout << "ERROR: DynGen init compile error: " << mEelState->last_error_string << std::endl;
      return false;
    }
  }

  if (!mScript->block.empty()) {
    mBlockCode = NSEEL_code_compile_ex(mEelState, mScript->block.c_str(), 0, compileFlags);
    if (!mBlockCode) {
      std::cout << "ERROR: DynGen block compile error " << mEelState->last_error_string << std::endl;
      return false;
    }
  }

  mSampleCode = NSEEL_code_compile_ex(mEelState, mScript->sample.c_str(), 0, compileFlags);
  if (!mSampleCode) {
    std::cout << "ERROR: DynGen sample compile error: " << mEelState->last_error_string << std::endl;
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
}
