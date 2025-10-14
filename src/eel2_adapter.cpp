#include "eel2_adapter.h"

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

  // define variables for the script context
  std::string header;
  header += "srate = " + std::to_string(mSampleRate) + ";\n";

  mScript = header + script;

  code_ = NSEEL_code_compile(eel_state_, mScript.c_str(), 0);
  if (!code_) {
    std::cout << "NSEEL_code_compile failed" << std::endl;
    return;
  }
  mReady.store(true);
}

EEL2Adapter::~EEL2Adapter() {
  if (code_)
    NSEEL_code_free(code_);
  if (eel_state_)
    NSEEL_VM_free(eel_state_);
  delete[] mInputs;
  delete[] mOutputs;
}
