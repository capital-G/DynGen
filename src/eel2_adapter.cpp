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
  NSEEL_addfunc_retval("bufRd", 3, NSEEL_PProc_THIS, &eelReadBuf);
  NSEEL_addfunc_retval("bufWr", 4, NSEEL_PProc_THIS, &eelWriteBuf);

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

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelReadBuf(void* opaque, const EEL_F *bufNumArg, const EEL_F *sampleNumArg, const EEL_F *chanArg) {
  const auto eel2Adapter = static_cast<EEL2Adapter*>(opaque);
  const auto optBuf =  eel2Adapter->GetBuffer(static_cast<int>(*bufNumArg));
  if (!optBuf) {
    return 0.0f;
  };
  const auto buf = *optBuf;
  const int sampleNum = static_cast<int>(*sampleNumArg);
  if (sampleNum >= buf->frames || sampleNum < 0) {
    return 0.0f;
  }

  int chanOffset = static_cast<int>(*chanArg);
  if (chanOffset > buf->channels || chanOffset < 0) {
    chanOffset = 0;
  }

  LOCK_SNDBUF_SHARED(buf);
  return buf->data[(sampleNum*buf->channels) + chanOffset];
}

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelWriteBuf(void* opaque, const EEL_F *bufNumArg, const EEL_F *sampleNumArg, const EEL_F *chanArg, const EEL_F *bufValueArg) {
  const auto eel2Adapter = static_cast<EEL2Adapter*>(opaque);
  const auto optBuf =  eel2Adapter->GetBuffer(static_cast<int>(*bufNumArg));
  if (!optBuf) {
    return 0.0f;
  };
  const auto buf = *optBuf;
  const int sampleNum = static_cast<int>(*sampleNumArg);
  if (sampleNum >= buf->frames || sampleNum < 0) {
    return 0.0f;
  }

  int chanOffset = static_cast<int>(*chanArg);
  if (chanOffset > buf->channels || chanOffset < 0) {
    chanOffset = 0;
  }

  LOCK_SNDBUF(buf);
  buf->data[(sampleNum * buf->channels) + chanOffset] = static_cast<float>(*bufValueArg);
  // or should this return the old now overwritten value?
  return *bufValueArg;
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
