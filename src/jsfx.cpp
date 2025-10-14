#include "jsfx.h"
#include <thread>

// no cleanup necessary for us! ;)
void noOpCleanup(World *world, void *inUserData) {}

struct SC_JSFX_Callback {
  SndBuf *scriptBuffer;
  EEL2Adapter *adapter;
};

std::string extractScriptFromBuffer(SndBuf* scriptBuffer) {
  std::string script;
  script.reserve(scriptBuffer->samples);
  for (int i = 0; i < scriptBuffer->samples; i++) {
    char c = static_cast<char>(
        static_cast<int>(scriptBuffer->data[i]));
    script.push_back(c);
  }
  return script;
}

// this gets called deferred from the UGen init in a
// stage2 thread which is NRT - so it is safe to allocate
// memory and also spawn a thread to which we offload
// all the heavy lifting of initializing the VM.
bool jsfxCallback(World *world, void *rawCallbackData) {
  auto callbackData = (SC_JSFX_Callback *)rawCallbackData;

  std::thread([callbackData]() {
    auto script = extractScriptFromBuffer(callbackData->scriptBuffer);
    callbackData->adapter->init(script);
  }).detach();

  // do not continue to stage 3
  return false;
}

SC_JSFX::SC_JSFX() {
  mNumOutputs = static_cast<int>(in0(0));
  mScriptBuffer = mWorld->mSndBufs + static_cast<int>(in0(1));
  mNumInputs = static_cast<int>(in0(2));
  bool useAudioThread = in0(3) > 0.5;

  vm = new EEL2Adapter(mNumInputs, mNumOutputs, static_cast<int>(sampleRate()));

  if (useAudioThread) {
    auto string = extractScriptFromBuffer(mScriptBuffer);
    vm->init(string);
  } else {
    auto callbackData = new SC_JSFX_Callback();
    callbackData->scriptBuffer = mScriptBuffer;
    callbackData->adapter = vm;

    ft->fDoAsynchronousCommand(
        mWorld, nullptr, nullptr, static_cast<void*>(callbackData),
        jsfxCallback, nullptr,nullptr, noOpCleanup, 0, nullptr);
  }

  set_calc_function<SC_JSFX, &SC_JSFX::next>();
  next(1);
}

SC_JSFX::~SC_JSFX() { delete vm; }

void SC_JSFX::next(int numSamples) {
  if (!vm || !vm->mReady.load()) {
    for (int i = 0; i < mNumOutputs; i++) {
      Clear(numSamples, mOutBuf[i]);
    }
  } else {
    // skip first 3 channels since those are not signals
    vm->process(mInBuf + 4, mOutBuf, numSamples);
  }
}

PluginLoad("SC_JSFX") {
  ft = inTable;

  NSEEL_init();

  registerUnit<SC_JSFX>(inTable, "JSFX", false);
  registerUnit<SC_JSFX>(inTable, "JSFXRT", false);
}
