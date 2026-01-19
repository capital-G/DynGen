#include "dyngen.h"
#include "library.h"

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <atomic>

InterfaceTable *ft;


DynGen::DynGen() {
  mCodeID = static_cast<int>(in0(0));
  const bool useAudioThread = in0(1) > 0.5;
  mNumDynGenInputs = static_cast<int>(in0(2));
  mNumDynGenParameters = static_cast<int>(in0(3));
  assert(mNumDynGenInputs + mNumDynGenParameters + 4 <= numInputs());

  set_calc_function<DynGen, &DynGen::next>();

  // necessary for `ClearUnitIfMemFailed` macro
  auto unit = this;
  mStub = static_cast<DynGenStub*>(RTAlloc(mWorld, sizeof(DynGenStub)));
  ClearUnitIfMemFailed(mStub);
  mStub->mObject = this;
  mStub->mRefCount = 1;

  // search for codeId within code library linked list
  auto codeNode = Library::findCode(mCodeID);
  if (codeNode == nullptr) {
    Print("ERROR: Could not find script with hash %i\n", mCodeID);
    next(1);
    return;
  }

  mParameterIndices = static_cast<int*>(RTAlloc(mWorld, sizeof(int) * mNumDynGenParameters));
  if (!mParameterIndices) {
    Print("ERROR: Could not allocate memory for parameter pointers\n");
    next(1);
    return;
  }
  for (int i = 0; i < mNumDynGenParameters; i++) {
    // parameters come in index-value pairs, so only take each 2nd position
    auto paramIndex = static_cast<int>(in0(4 + mNumDynGenInputs + (2*i)));
    mParameterIndices[i] = paramIndex;
  }

  // add ourselves to the code node so we can receive code updates.
  codeNode->addUnit(this);
  // we have to unregister ourself when the Unit is freed, see ~DynGen().
  mCodeLibrary = codeNode;

  if (useAudioThread) {
    // do init of VM in RT thread - this is dangerous and should not be done,
    // yet it get rids of one block size delay until the signal appears.
    // Since the VM init seems to be often fast enough we allow the user
    // to decide, yet this is not the default case.
    auto vm = new EEL2Adapter(
      mNumDynGenInputs,
      mNumOutputs,
      static_cast<int>(sampleRate()),
      mBufLength,
      mWorld,
      mParent
    );

    if (vm->init(*codeNode->mScript, mParameterIndices, mNumDynGenParameters)) {
        mVm = vm;
    } else {
        delete vm;
    }
  } else {
    // offload VM init to NRT thread
    if (!updateCode(codeNode->mScript)) {
      ClearUnitOnMemFailed
    }
  }
}

void DynGen::next(int numSamples) {
  if (mVm == nullptr) {
    for (int i = 0; i < mNumOutputs; i++) {
      Clear(numSamples, mOutBuf[i]);
    }
  } else {
    // skip first 4 channels since those are not signals
    mVm->process(mInBuf + 4, mOutBuf, mInBuf + 4 + mNumDynGenInputs, numSamples);
  }
}

bool DynGen::updateCode(const DynGenScript* script) const {
  // allocate extra space for parameter indices, see DynGenCallbackData.
  auto payloadSize = sizeof(DynGenCallbackData) + sizeof(int) * mNumDynGenParameters;
  auto payload = static_cast<DynGenCallbackData*>(RTAlloc(mWorld, payloadSize));

  // guard in case allocation fails
  if (payload) {
    payload->dynGenStub = mStub;
    payload->numInputChannels = mNumDynGenInputs;
    payload->numOutputChannels = mNumOutputs;
    payload->numParameters = mNumDynGenParameters;
    payload->sampleRate = static_cast<int>(sampleRate());
    payload->blockSize = mBufLength;
    payload->world = mWorld;
    payload->parent = mParent;
    payload->oldVm = nullptr;
    payload->script = script;

    for (int i = 0; i < mNumDynGenParameters; ++i) {
      payload->parameterIndices[i] = mParameterIndices[i];
    }

    // increment ref counter before we start the async command
    mStub->mRefCount += 1;

    ft->fDoAsynchronousCommand(
      mWorld,
      nullptr,
      nullptr,
      static_cast<void*>(payload),
      createVmAndCompile,
      swapVmPointers,
      deleteOldVm,
      dynGenInitCallbackCleanup,
      0,
      nullptr
    );
  }

  return payload != nullptr;
}

 DynGen::~DynGen() {
  RTFree(mWorld, mParameterIndices);

  mStub->mObject = nullptr;
  mStub->mRefCount -= 1;
  if (mStub->mRefCount==0) {
    RTFree(mWorld, mStub);
  }

  // in case we have not found a matching code the vm was never initialized
  // so we also do not have access to a code library or a vm we would have
  // to clean up, so we bail out early.
  if (mCodeLibrary == nullptr) {
    return;
  }

  // remove ourselves from the code library
  mCodeLibrary->removeUnit(this);

  // free the vm in RT context through async command
  ft->fDoAsynchronousCommand(
    mWorld,
    nullptr,
    nullptr,
    static_cast<void*>(mVm),
    deleteVmOnSynthDestruction,
    nullptr,
    nullptr,
    doNothing,
    0,
    nullptr
  );
}

bool DynGen::createVmAndCompile(World* world, void *rawCallbackData) {
  auto callbackData = static_cast<DynGenCallbackData*>(rawCallbackData);

  callbackData->vm = new EEL2Adapter(
    callbackData->numInputChannels,
    callbackData->numOutputChannels,
    callbackData->sampleRate,
    callbackData->blockSize,
    callbackData->world,
    callbackData->parent
  );

  auto success = callbackData->vm->init(*callbackData->script,
                                        callbackData->parameterIndices,
                                        callbackData->numParameters);
  if (!success) {
    // if not successful, remove vm and do not attempt to replace
    // running vm.
    delete callbackData->vm;
    return false;
  }
  // continue with stage 3
  return true;
}

bool DynGen::swapVmPointers(World* world, void *rawCallbackData) {
  auto callbackData = static_cast<DynGenCallbackData*>(rawCallbackData);
  // only replace if DynGen instance is still existing
  if (callbackData->dynGenStub->mObject) {
    callbackData->oldVm = callbackData->dynGenStub->mObject->mVm;
    callbackData->dynGenStub->mObject->mVm = callbackData->vm;
  } else {
    // mark the vm we just created ready for deletion since the DynGen
    // it was created for does not exist anymore.
    callbackData->oldVm = callbackData->vm;
  }
  return true;
}

bool DynGen::deleteOldVm(World* world, void *rawCallbackData) {
  auto callbackData = static_cast<DynGenCallbackData*>(rawCallbackData);
  delete callbackData->oldVm;
  return true;
}

void DynGen::dynGenInitCallbackCleanup(World *world, void *rawCallbackData) {
  auto callback = static_cast<DynGenCallbackData *>(rawCallbackData);
  callback->dynGenStub->mRefCount -= 1;
  // destroy if there are no references to the DynGen
  if (callback->dynGenStub->mRefCount == 0) {
    RTFree(world, callback->dynGenStub);
  }
  RTFree(world, callback);
}

bool DynGen::deleteVmOnSynthDestruction(World *world, void *rawCallbackData) {
  const auto vm = static_cast<EEL2Adapter*>(rawCallbackData);
  delete vm;
  // do not return to stage 3 - we are done
  return false;
}

// ********************
// Plugin bootstrapping
// ********************

PluginLoad("DynGen") {
  ft = inTable;

  NSEEL_init();

  registerUnit<DynGen>(inTable, "DynGen", false);
  registerUnit<DynGen>(inTable, "DynGenRT", false);

  ft->fDefinePlugInCmd(
    "dyngenfile",
    Library::dyngenAddFileCallback,
    nullptr
  );

  ft->fDefinePlugInCmd(
  "dyngenscript",
  Library::addScriptCallback,
  nullptr
);
}
