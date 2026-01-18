#include "dyngen.h"
#include "library.h"

#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <atomic>

InterfaceTable *ft;

// a global linked list which stores the code
// and its associated running DynGens.
CodeLibrary *gLibrary = nullptr;


DynGen::DynGen() : mPrevDynGen(nullptr), mNextDynGen(nullptr), mCodeLibrary(nullptr), mStub(nullptr) {
  mCodeID = static_cast<int>(in0(0));
  const bool useAudioThread = in0(1) > 0.5;
  mNumDynGenInputs = static_cast<int>(in0(2));
  mNumDynGenParameters = static_cast<int>(in0(3));

  set_calc_function<DynGen, &DynGen::next>();

  // necessary for `ClearUnitIfMemFailed` macro
  auto unit = this;
  mStub = static_cast<DynGenStub*>(RTAlloc(mWorld, sizeof(DynGenStub)));
  ClearUnitIfMemFailed(mStub);
  mStub->mObject = this;
  mStub->mRefCount = 1;

  // search for codeId within code library linked list
  auto codeNode = gLibrary;
  bool found = false;
  while (codeNode!=nullptr) {
    if (codeNode->id == mCodeID) {
      found = true;
      break;
    }
    codeNode = codeNode->next;
  }
  if (!found) {
    Print("ERROR: Could not find script with hash %i\n", mCodeID);
    next(1);
    return;
  }

  mNumParameters = codeNode->numParameters;
  mParameterIndices = static_cast<int*>(RTAlloc(mWorld, mNumParameters));
  if (!mParameterIndices) {
    Print("ERROR: Could not allocate memory for parameter pointers\n");
    next(1);
    return;
  }
  for (int i = 0; i < mNumDynGenParameters; i++) {
    // parameters come in groups, so only take each 2nd position
    auto paramIndex = static_cast<int>(*mInBuf[4 + mNumDynGenInputs + (2*i)]);
    if (paramIndex < 0 || paramIndex >= mNumParameters) {
      Print("ERROR: Parameter num %i out of range - falling back to param index 0\n", paramIndex);
      paramIndex = 0;
    }
    mParameterIndices[i] = paramIndex;
  }

  // insert ourselves into the linked list of DynGen nodes which are
  // using the same code such that we can receive code updates
  auto const head = codeNode->dynGen;
  if (head) {
    head->mPrevDynGen = this;
  }
  mNextDynGen = head;
  codeNode->dynGen = this;

  // we may have to re-adjust the entry point of our linked list if we
  // get cleared, so we store the handle to the library which can not be
  // deleted.
  mCodeLibrary = codeNode;

  if (useAudioThread) {
    // do init of VM in RT thread - this is dangerous and should not be done,
    // yet it get rids of one block size delay until the signal appears.
    // Since the VM init seems to be often fast enough we allow the user
    // to decide, yet this is not the default case.
    mVm = new EEL2Adapter(
      mNumDynGenInputs,
      mNumOutputs,
      mNumParameters,
      static_cast<int>(sampleRate()),
      mBufLength,
      mWorld,
      mParent
    );
    mVm->init(codeNode->code, codeNode->parameters);
  } else {
    // offload VM init to NRT thread
    ClearUnitIfMemFailed(updateCode(codeNode->code, codeNode->parameters));
  }
}

void DynGen::next(int numSamples) {
  if (mVm == nullptr) {
    for (int i = 0; i < mNumOutputs; i++) {
      Clear(numSamples, mOutBuf[i]);
    }
  } else {
    // skip first 4 channels since those are not signals
    mVm->process(mInBuf + 4, mOutBuf, mNumDynGenParameters, mParameterIndices, numSamples);
  }
}

bool DynGen::updateCode(const char* code, char** parameters) const {
  auto payload = static_cast<DynGenCallbackData*>(RTAlloc(mWorld, sizeof(DynGenCallbackData)));

  // guard in case allocation fails
  if (payload) {
    payload->dynGenStub = mStub;
    payload->numInputChannels = mNumDynGenInputs;
    payload->numOutputChannels = mNumOutputs;
    payload->numParameters = mNumParameters;
    payload->parameters = parameters;
    payload->sampleRate = static_cast<int>(sampleRate());
    payload->blockSize = mBufLength;
    payload->world = mWorld;
    payload->parent = mParent;
    payload->oldVm = nullptr;
    payload->code = code;

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
  // readjust the head of the linked list of the code library if necessary
  if (mCodeLibrary->dynGen == this) {
    mCodeLibrary->dynGen = mNextDynGen;
  }

  // remove ourselves from the linked list
  if (mPrevDynGen != nullptr) {
    mPrevDynGen->mNextDynGen = mNextDynGen;
  }
  if (mNextDynGen != nullptr) {
    mNextDynGen->mPrevDynGen = mPrevDynGen;
  }

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
    callbackData->numParameters,
    callbackData->sampleRate,
    callbackData->blockSize,
    callbackData->world,
    callbackData->parent
  );

  auto success = callbackData->vm->init(callbackData->code, callbackData->parameters);
  if (!success) {
    // if not successful, remove vm and do not attempt to replace
    // running vm.
    delete callbackData->vm;
    return false;
  }
  // continue to stage 3
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
