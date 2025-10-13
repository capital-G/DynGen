#include "jsfx.h"
#include <thread>

// no cleanup necessary for us! ;)
void noOpCleanup(World *world, void *inUserData) {}

struct SC_JSFX_Callback {
  SndBuf *scriptBuffer;
  EEL2Adapter *adapter;
};

// this gets called deferred from the UGen init in a
// stage2 thread which is NRT - so it is safe to allocate
// memory and also spawn a thread to which we offload
// all the heavy lifting of initializing the VM.
void jsfxCallback(World *world, void *rawCallbackData) {
  auto callbackData = (SC_JSFX_Callback *)rawCallbackData;

  std::thread([callbackData]() {
    // load script from buffer
    std::string script;
    script.reserve(callbackData->scriptBuffer->samples);
    for (int i = 0; i < callbackData->scriptBuffer->samples; i++) {
      char c = static_cast<char>(
          static_cast<int>(callbackData->scriptBuffer->data[i]));
      script.push_back(c);
    }

    callbackData->adapter->init(script);
  }).detach();
}

SC_JSFX::SC_JSFX() {
  mNumOutputs = static_cast<int>(in0(0));
  mScriptBuffer = mWorld->mSndBufs + static_cast<int>(in0(1));
  mNumInputs = static_cast<int>(in0(2));

  vm = new EEL2Adapter(mNumInputs, mNumOutputs, static_cast<int>(sampleRate()));

  auto callbackData = new SC_JSFX_Callback();
  callbackData->scriptBuffer = mScriptBuffer;
  callbackData->adapter = vm;

  auto fakeReplyAddr = "foo\0";
  ft->fDoAsynchronousCommand(
      mWorld, (void *)fakeReplyAddr, "someFakeCmdName", (void *)callbackData,
      (AsyncStageFn)jsfxCallback, (AsyncStageFn)jsfxCallback,
      (AsyncStageFn)jsfxCallback, noOpCleanup, 0, nullptr);

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
    vm->process(mInBuf + 3, mOutBuf, numSamples);
  }
}

PluginLoad("SC_JSFX") {
  ft = inTable;

  registerUnit<SC_JSFX>(inTable, "JSFX", false);
}
