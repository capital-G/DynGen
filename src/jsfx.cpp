#include "jsfx.h"
#include <thread>

struct SC_JSFX_Callback {
  SndBuf *scriptBuffer;
  EEL2Adapter *adapter;
};

void callbackCleanup(World *world, void *raw_callback) {
  auto callback = static_cast<SC_JSFX_Callback *>(raw_callback);
  RTFree(world, callback);
}

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
  auto callbackData = static_cast<SC_JSFX_Callback*>(rawCallbackData);

  std::thread([callbackData]() {
    auto script = extractScriptFromBuffer(callbackData->scriptBuffer);
    callbackData->adapter->init(script);
  }).detach();

  // do not continue to stage 3
  return false;
}

SC_JSFX::SC_JSFX() : vm(static_cast<int>(in0(2)), static_cast<int>(in0(0)), static_cast<int>(sampleRate())) {
  mScriptBuffer = mWorld->mSndBufs + static_cast<int>(in0(1));
  bool useAudioThread = in0(3) > 0.5;

  // vm = new EEL2Adapter(mNumInputs, mNumOutputs, static_cast<int>(sampleRate()));

  if (useAudioThread) {
    auto string = extractScriptFromBuffer(mScriptBuffer);
    vm.init(string);
  } else {
    auto payload = static_cast<SC_JSFX_Callback*>(RTAlloc(mWorld, sizeof(SC_JSFX_Callback)));
    payload->scriptBuffer = mScriptBuffer;
    payload->adapter = &vm;

    ft->fDoAsynchronousCommand(
        mWorld, nullptr, nullptr, static_cast<void*>(payload),
        jsfxCallback, nullptr,nullptr, callbackCleanup, 0, nullptr);
  }

  set_calc_function<SC_JSFX, &SC_JSFX::next>();
  next(1);
}

void SC_JSFX::next(int numSamples) {
  if (!vm.mReady.load()) {
    for (int i = 0; i < mNumOutputs; i++) {
      Clear(numSamples, mOutBuf[i]);
    }
  } else {
    // skip first 3 channels since those are not signals
    vm.process(mInBuf + 4, mOutBuf, numSamples);
  }
}

PluginLoad("SC_JSFX") {
  ft = inTable;

  NSEEL_init();

  registerUnit<SC_JSFX>(inTable, "JSFX", false);
  registerUnit<SC_JSFX>(inTable, "JSFXRT", false);
}
