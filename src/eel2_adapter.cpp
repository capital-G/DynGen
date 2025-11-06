#include "eel2_adapter.h"

#include "ns-eel-addfuncs.h"
#include "ns-eel-int.h"

#include <SC_Unit.h>

// define symbols for jsfx
extern "C" void NSEEL_HOSTSTUB_EnterMutex() {}
extern "C" void NSEEL_HOSTSTUB_LeaveMutex() {}

// this is not RT safe
void EEL2Adapter::init(const std::string &script) {
  mInputs = new double *[mNumInputChannels]();
  mOutputs = new double *[mNumOutputChannels]();

  eel_state_ = static_cast<compileContext*>(NSEEL_VM_alloc());

  // obtain handles to input and output variables
  for (int i = 0; i < mNumInputChannels; i++) {
    std::string name = "in" + std::to_string(i);
    mInputs[i] = NSEEL_VM_regvar(eel_state_, name.c_str());
  }
  for (int i = 0; i < mNumOutputChannels; i++) {
    std::string name = "out" + std::to_string(i);
    mOutputs[i] = NSEEL_VM_regvar(eel_state_, name.c_str());
  }

  NSEEL_VM_SetCustomFuncThis(eel_state_, this);
  NSEEL_addfunc_varparm("bufRead", 2, NSEEL_PProc_THIS, &eelReadBuf);
  NSEEL_addfunc_varparm("bufReadL", 2, NSEEL_PProc_THIS, &eelReadBufL);
  NSEEL_addfunc_varparm("bufReadC", 2, NSEEL_PProc_THIS, &eelReadBufC);
  NSEEL_addfunc_varparm("bufWrite", 3, NSEEL_PProc_THIS, &eelWriteBuf);

  // define variables for the script context
  std::string header;
  header += "srate = " + std::to_string(mSampleRate) + ";\n";

  mScript = header + script;

  auto compileFlags =
    NSEEL_CODE_COMPILE_FLAG_COMMONFUNCS |
    NSEEL_CODE_COMPILE_FLAG_COMMONFUNCS_RESET |
    NSEEL_CODE_COMPILE_FLAG_NOFPSTATE;
  code_ = NSEEL_code_compile_ex(eel_state_, mScript.c_str(), 0, compileFlags);
  if (!code_) {
    std::cout << "NSEEL_code_compile failed: " << eel_state_->last_error_string << std::endl;
    return;
  }
  mReady.store(true);
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
  if (code_)
    // @todo delay this to NRT thread
    NSEEL_code_free(code_);
  if (eel_state_)
    // @todo delay this to NRT thread
    NSEEL_VM_free(eel_state_);
  delete[] mInputs;
  delete[] mOutputs;
}
