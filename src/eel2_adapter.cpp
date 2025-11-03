#include "eel2_adapter.h"

#include "ns-eel-addfuncs.h"

// define symbols for jsfx
extern "C" void NSEEL_HOSTSTUB_EnterMutex() {}
extern "C" void NSEEL_HOSTSTUB_LeaveMutex() {}

// this is not RT safe
void EEL2Adapter::init(const std::string &script) {
  mInputs = new double *[mNumInputChannels]();
  mOutputs = new double *[mNumOutputChannels]();

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

  NSEEL_VM_SetCustomFuncThis(eel_state_, this);
  NSEEL_addfunc_retval("bufRd", 2, NSEEL_PProc_THIS, &eelReadBuf);

  // define variables for the script context
  std::string header;
  header += "srate = " + std::to_string(mSampleRate) + ";\n";

  mScript = header + script;

  auto compileFlags = \
    NSEEL_CODE_COMPILE_FLAG_COMMONFUNCS | \
    NSEEL_CODE_COMPILE_FLAG_COMMONFUNCS | \
    NSEEL_CODE_COMPILE_FLAG_COMMONFUNCS_RESET | \
    NSEEL_CODE_COMPILE_FLAG_NOFPSTATE;
  code_ = NSEEL_code_compile_ex(eel_state_, mScript.c_str(), 0, compileFlags);
  if (!code_) {
    std::cout << "NSEEL_code_compile failed" << std::endl;
    return;
  }
  mReady.store(true);
}

EEL_F NSEEL_CGEN_CALL EEL2Adapter::eelReadBuf(void* opaque, const EEL_F *bufNumArg, const EEL_F *sampleNumArg) {
  auto world = static_cast<EEL2Adapter*>(opaque)->mWorld;
  int bufNum = static_cast<int>(*bufNumArg);
  if (bufNum >= world->mNumSndBufs) {
    return 0.0f;
  };
  auto buf = world->mSndBufs[bufNum];
  int sampleNum = static_cast<int>(*sampleNumArg);
  if (sampleNum >= buf.frames) {
    return 0.0f;
  }
  return buf.data[sampleNum];
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
