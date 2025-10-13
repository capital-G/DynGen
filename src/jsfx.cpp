#include "jsfx.h"

SC_JSFX::SC_JSFX() {
  mNumOutputs = static_cast<int>(in0(0));
  mScriptBuffer = mWorld->mSndBufs + static_cast<int>(in0(1));
  mNumInputs = static_cast<int>(in0(2));

  std::string script;
  script.reserve(mScriptBuffer->samples);
  for (int i = 0; i < mScriptBuffer->samples; i++) {
    char c = static_cast<char>(static_cast<int>(mScriptBuffer->data[i]));
    script.push_back(c);
  }

  vm = new EEL2Adapter(script, mNumInputs, mNumOutputs, sampleRate());

  set_calc_function<SC_JSFX, &SC_JSFX::next>();
  next(1);
}

void SC_JSFX::next(int numSamples) {
  if (!vm || !vm->mCompiledSuccessfully) {
    for (int i = 0; i < mNumOutputs; i++) {
      Clear(numSamples, mOutBuf[i]);
    }
  } else {
    // skip first 3 channels since those are not signals
    vm->process(mInBuf + 3, mOutBuf, numSamples);
  }
}

PluginLoad("SC_JSFX") { registerUnit<SC_JSFX>(inTable, "JSFX", false); }
