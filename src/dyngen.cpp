#include "dyngen.h"
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>
#include <atomic>

// a global linked list which stores the code
// and its associated running DynGens.
static CodeLibrary *gLibrary = nullptr;

// *****************************
// NRT callbacks for DynGen init
// *****************************

// stage 2 - NRT
bool createVmAndCompile(World* world, void *rawCallbackData) {
  auto callbackData = static_cast<DynGenCallbackData*>(rawCallbackData);

  callbackData->vm = new EEL2Adapter(
    callbackData->numInputChannels,
    callbackData->numOutputChannels,
    callbackData->sampleRate,
    callbackData->blockSize,
    callbackData->world,
    callbackData->parent
  );

  callbackData->vm->init(callbackData->code);
  // continue to stage 3
  return true;
}

// stage 3 - RT
bool swapVmPointers(World* world, void *rawCallbackData) {
  auto callbackData = static_cast<DynGenCallbackData*>(rawCallbackData);
  // only replace if DynGen instance is still existing
  if (callbackData->dynGenStub->mObject) {
    callbackData->oldVm = callbackData->dynGenStub->mObject->mVm;
    callbackData->dynGenStub->mObject->mVm = callbackData->vm;
  }
  return true;
}

// stage 4 - NRT
bool deleteOldVm(World* world, void *rawCallbackData) {
  auto callbackData = static_cast<DynGenCallbackData*>(rawCallbackData);
  delete callbackData->oldVm;
  return true;
}

// cleanup - RT
void dynGenInitCallbackCleanup(World *world, void *rawCallbackData) {
  auto callback = static_cast<DynGenCallbackData *>(rawCallbackData);
  callback->dynGenStub->mRefCount -= 1;
  // destroy if there are no references to the DynGen
  if (callback->dynGenStub->mRefCount == 0) {
    RTFree(world, callback->dynGenStub);
  }
  RTFree(world, callback);
}

// ~DynGen callback to destroy the vm in a NRT thread on stage 2
bool deleteVmOnSynthDestruction(World *world, void *rawCallbackData) {
  const auto vm = static_cast<EEL2Adapter*>(rawCallbackData);
  delete vm;
  // do not return to stage 3 - we are done
  return false;
}

// dummy task b/c we are already deleting the vm above which
// is the pointer we pass around
void doNothing(World *world, void *rawCallbackData) {}

// *********
// UGen code
// *********
DynGen::DynGen() : mPrevDynGen(nullptr), mNextDynGen(nullptr), mCodeLibrary(nullptr), mStub(nullptr) {
  mCodeID = static_cast<int>(in0(0));
  const bool useAudioThread = in0(1) > 0.5;
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
    Print("Could not find script with hash %i\n", mCodeID);
    next(1);
    return;
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
      mNumInputs-2,
      mNumOutputs,
      static_cast<int>(sampleRate()),
      mBufLength,
      mWorld,
      mParent
    );
    mVm->init(codeNode->code);
  } else {
    // offload VM init to NRT thread
    auto payload = static_cast<DynGenCallbackData*>(RTAlloc(mWorld, sizeof(DynGenCallbackData)));
    ClearUnitIfMemFailed(payload);

    fillCallbackData(payload, codeNode->code);
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

  next(1);
}

void DynGen::fillCallbackData(DynGenCallbackData* payload, char* code) const {
  payload->dynGenStub = mStub;
  payload->numInputChannels = mNumInputs;
  payload->numOutputChannels = mNumOutputs;
  payload->sampleRate = static_cast<int>(sampleRate());
  payload->blockSize = mBufLength;
  payload->world = mWorld;
  payload->parent = mParent;
  payload->oldVm = nullptr;
  payload->code = code;
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

void DynGen::next(int numSamples) {
  if (mVm == nullptr) {
    for (int i = 0; i < mNumOutputs; i++) {
      Clear(numSamples, mOutBuf[i]);
    }
  } else {
    // skip first 2 channels since those are not signals
    mVm->process(mInBuf + 2, mOutBuf, numSamples);
  }
}

// ************
// Library code
// ************

// this runs in stage 2 (NRT) and loads the content of the file
// which gets passed to stage 3 (RT)
bool loadFileToDynGenLibrary(World *world, void *rawCallbackData) {
  auto entry = static_cast<NewDynGenLibraryEntry*>(rawCallbackData);

  auto codeFile = std::ifstream(entry->codePath, std::ios::binary);
  if (!codeFile.is_open()) {
    std::cerr << "Could not open DynGen file at " << entry->codePath << std::endl;
    return false;
  }

  std::stringstream codeStream;

  codeFile.seekg(0, std::ios::end);
  const std::streamsize codeSize = codeFile.tellg();
  codeFile.seekg(0);

  // add /0
  auto* codeBuffer = new char[codeSize + 1];
  codeFile.read(codeBuffer, codeSize);
  codeBuffer[codeSize] = '\0';

  entry->code = codeBuffer;

  // continue to next stage
  return true;
}

// runs in stage 3 (RT-thread)
// The code string gets entered into the library
// by traversing it as a linked list.
// If the hash ID already exists the code gets updated and all running
// instances should be updated.
bool swapCode(World* world, void *rawCallbackData) {
  auto entry = static_cast<NewDynGenLibraryEntry*>(rawCallbackData);

  CodeLibrary* node = gLibrary;
  while (node && node->id != entry->hash) {
    node = node->next;
  }

  if (!node) {
    auto* newNode = static_cast<CodeLibrary*>(RTAlloc(world, sizeof(CodeLibrary)));
    newNode->next = gLibrary;
    newNode->id = entry->hash;
    newNode->dynGen = nullptr;
    newNode->code = entry->code;
    gLibrary = newNode;
  } else {
    // swap code
    entry->oldCode = node->code;
    node->code = entry->code;
    auto dynGen = node->dynGen;
    while (dynGen != nullptr) {
      auto* callbackData = static_cast<DynGenCallbackData*>(RTAlloc(world, sizeof(DynGenCallbackData)));
      // make sure allocation worked
      if (callbackData) {
        // although the code can be updated, the referenced code
        // lives long enough b/c in worst case there is already
        // a new code in the pipeline at stage2 where the old code
        // would be destroyed in its stage4.
        // Yet we only need to access the code in stage 2 in our callback,
        // where it could not have been destroyed yet.
        // See https://github.com/capital-G/DynGen/pull/40#discussion_r2599579920

/*
     ┌─────────┐             ┌──────────┐           ┌─────────┐          ┌──────────┐
     │STAGE1_RT│             │STAGE2_NRT│           │STAGE3_RT│          │STAGE4_NRT│
     └────┬────┘             └─────┬────┘           └────┬────┘          └─────┬────┘
          │loadFileToDynGenLibrary │                     │                     │
          │───────────────────────>│                     │                     │
          │                        │                     │                     │
          │                        │      swapCode       │                     │
          │                        │────────────────────>│                     │
          │                        │                     │                     │
          │loadFileToDynGenLibrary │                     │                     │
          │ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ >│                     │                     │
          │                        │                     │                     │
          │     ╔════════════════╗ │createVmAndCompileA  │                     │
          │     ║accessing code ░║ │<────────────────────│                     │
          │     ╚════════════════╝ │                     │                     │
          │                        │createVmAndCompileB  │                     │
          │                        │<────────────────────│                     │
          │                        │                     │                     │
          │    ╔═════════════════╗ │      swapCode       │                     │
          │    ║code -> oldCode ░║ │ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─>│                     │
          │    ╚═════════════════╝ │                     │                     │
          │                        │                     │   deleteOldCode     │
          │                        │                     │────────────────────>│
          │                        │                     │                     │
          │                        │  swapVmPointersA    │                     │
          │                        │────────────────────>│                     │
          │                        │                     │                     │
          │                        │  swapVmPointersB    │                     │
          │                        │────────────────────>│                     │
          │                        │                     │                     │
          │                 ╔══════╧═══════════════════╗ │   deleteOldCode     │
          │                 ║deleting code as oldCode ░║ │ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─>│
          │                 ╚══════╤═══════════════════╝ │                     │
          │                        │                     │    deleteOldVm      │
          │                        │                     │────────────────────>│
     ┌────┴────┐             ┌─────┴────┐           ┌────┴────┐          ┌─────┴────┐
     │STAGE1_RT│             │STAGE2_NRT│           │STAGE3_RT│          │STAGE4_NRT│
     └─────────┘             └──────────┘           └─────────┘          └──────────┘

source code:
```plantuml
@startuml
STAGE1_RT -> STAGE2_NRT : loadFileToDynGenLibrary
STAGE2_NRT -> STAGE3_RT : swapCode
STAGE1_RT --> STAGE2_NRT : loadFileToDynGenLibrary
STAGE3_RT -> STAGE2_NRT : createVmAndCompileA
note left: accessing code
STAGE3_RT -> STAGE2_NRT : createVmAndCompileB
STAGE2_NRT --> STAGE3_RT: swapCode
note left: code -> oldCode
STAGE3_RT -> STAGE4_NRT : deleteOldCode
STAGE2_NRT -> STAGE3_RT: swapVmPointersA
STAGE2_NRT -> STAGE3_RT: swapVmPointersB
STAGE3_RT --> STAGE4_NRT : deleteOldCode
note left: deleting code as oldCode
STAGE3_RT -> STAGE4_NRT : deleteOldVm
@enduml
```
*/
        dynGen->fillCallbackData(callbackData, entry->code);
        dynGen->mStub->mRefCount += 1;
        ft->fDoAsynchronousCommand(
          world,
          nullptr,
          nullptr,
          static_cast<void*>(callbackData),
          createVmAndCompile,
          swapVmPointers,
          deleteOldVm,
          dynGenInitCallbackCleanup,
          0,
          nullptr
        );
      }
      dynGen = dynGen->mNextDynGen;
    }
  }

  return true;
}

// runs in stage 4 (non-RT-thread)
bool deleteOldCode(World *world, void *rawCallbackData) {
  auto entry = static_cast<NewDynGenLibraryEntry*>(rawCallbackData);
  delete[] entry->oldCode;
  return true;
}

// frees the created struct. Uses RTFree since the callback data has been
// allocated within RT thread
void pluginCmdCallbackCleanup(World *world, void *rawCallbackData) {
  auto callBackData = static_cast<NewDynGenLibraryEntry*>(rawCallbackData);
  RTFree(world, callBackData->codePath);
  RTFree(world, callBackData);
}


// runs in stage  1 (RT thread)
// responds to an osc message on the RT thread - we therefore have to
// copy the OSC data to a new struct which then gets passed to another
// callback which runs in stage 2 (non-RT thread).
// We have to free the created struct afterward via `pluginCmdCallbackCleanup`.
void pluginCmdCallback(World* inWorld, void* inUserData, struct sc_msg_iter* args, void* replyAddr) {
  auto newLibraryEntry = static_cast<NewDynGenLibraryEntry*>(RTAlloc(inWorld, sizeof(NewDynGenLibraryEntry)));
  newLibraryEntry->hash = args->geti();
  if (const char* codePath = args->gets()) {
    newLibraryEntry->codePath = static_cast<char*>(RTAlloc(inWorld, strlen(codePath) + 1));
    if (!newLibraryEntry->codePath) {
      Print("Failed to allocate memory for DynGen code library");
      return;
    }
    strcpy(newLibraryEntry->codePath, codePath);
  } else {
    Print("Invalid dyngenadd message\n");
    return;
  }
  newLibraryEntry->oldCode = nullptr;

  ft->fDoAsynchronousCommand(
    inWorld, nullptr, nullptr, static_cast<void*>(newLibraryEntry),
    loadFileToDynGenLibrary, swapCode,deleteOldCode, pluginCmdCallbackCleanup, 0, nullptr);
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
    "dyngenadd",
    pluginCmdCallback,
    nullptr
  );
}
