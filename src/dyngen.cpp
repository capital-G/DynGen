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

  callbackData->vm->init(callbackData->codeNode->code);
  // continue to stage 3
  return true;
}

// stage 3 - RT
bool swapVmPointers(World* world, void *rawCallbackData) {
  auto callbackData = static_cast<DynGenCallbackData*>(rawCallbackData);
  callbackData->oldVm = callbackData->dynGenNode->dynGenUnit->mVm;
  callbackData->dynGenNode->dynGenUnit->mVm = callbackData->vm;
  return true;
}

// stage 4 - NRT
bool deleteOldVm(World* world, void *rawCallbackData) {
  auto callbackData = static_cast<DynGenCallbackData*>(rawCallbackData);
  delete callbackData->oldVm;
  return true;
}

// cleanup
void dynGenInitCallbackCleanup(World *world, void *rawCallbackData) {
  auto callback = static_cast<DynGenCallbackData *>(rawCallbackData);
  RTFree(world, callback);
}

// ~DynGen callback to destroy the vm in a NRT thread on stage 2
bool deleteVmOnSynthDestruction(World *world, void *rawCallbackData) {
  if (rawCallbackData != nullptr) {
    auto vm = static_cast<EEL2Adapter*>(rawCallbackData);
    delete vm;
  }
  // do not return to stage 3 - we are done
  return false;
}

// dummy task b/c we are already deleting the vm above which
// is the pointer we pass around
void doNothing(World *world, void *rawCallbackData) {}

// *********
// UGen code
// *********
DynGen::DynGen() {
  mCodeID = static_cast<int>(in0(0));
  bool useAudioThread = in0(1) > 0.5;
  set_calc_function<DynGen, &DynGen::next>();

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

  // insert ourselves into the linked list such that we can
  // receive code updates
  auto const dynGenNode = static_cast<CodeLibrary::DynGenNode*>(RTAlloc(mWorld, sizeof(CodeLibrary::DynGenNode)));
  dynGenNode->dynGenUnit = this;
  dynGenNode->next = codeNode->dynGenNodes;
  codeNode->dynGenNodes = dynGenNode;

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

    auto unit = this; // the macro needs a reference to unit
    ClearUnitIfMemFailed(payload);

    payload->dynGenNode = dynGenNode;
    payload->codeNode = codeNode;

    payload->numInputChannels = mNumInputs;
    payload->numOutputChannels = mNumOutputs;
    payload->sampleRate = static_cast<int>(sampleRate());
    payload->blockSize = mBufLength;
    payload->world = mWorld;
    payload->parent = mParent;
    payload->oldVm = nullptr;

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

 DynGen::~DynGen() {
  // delete ourselves from the linked list of units in the associated library.
  auto node = gLibrary;
  while (node != nullptr) {
    if (node->id == mCodeID) {
      auto unit = node->dynGenNodes;
      CodeLibrary::DynGenNode *previousUnit = nullptr;

      // @todo this can probably be written cleaner and more optimized?
      while (unit!=nullptr) {
        if (unit->dynGenUnit == this) {
          if (previousUnit != nullptr) {
            if (unit->next != nullptr) {
              previousUnit->next = unit->next;
            } else {
              previousUnit->next = nullptr;
            }
          } else {
            node->dynGenNodes = nullptr;
          }
          ft->fRTFree(mWorld, static_cast<void*>(unit));
          break;
        }
        previousUnit = unit;
        unit = unit->next;
      }
    };
    node = node->next;
  }

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

// this runs in stage 2 (NRT) and enters the content of the file to
// the library by traversing the library which is a linked list.
// If the hash ID already exists the code gets updated and all running
// instances should be updated.
bool loadFileToDynGenLibrary(World *world, void *raw_callback) {
  auto entry = static_cast<NewDynGenLibraryEntry*>(raw_callback);

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
bool swapCode(World* world, void *raw_callback) {
  auto entry = static_cast<NewDynGenLibraryEntry*>(raw_callback);

  CodeLibrary* node = gLibrary;
  CodeLibrary* prevNode = nullptr;
  while (node && node->id != entry->hash) {
    prevNode = node;
    node = node->next;
  }

  if (!node) {
    auto* newNode = new CodeLibrary {
      gLibrary,
      entry->hash,
      nullptr,
      entry->code,
    };
    gLibrary = newNode;
  } else {
    // swap code
    entry->oldCode = node->code;
    node->code = entry->code;
    for (auto* unit = node->dynGenNodes; unit; unit = unit->next) {
      // protecting in case the vm already got removed b/c the synth got removed
      if (unit->dynGenUnit->mVm != nullptr) {
        unit->dynGenUnit->mVm->init(entry->code);
      } else {
        return false;
      }
    }
  }

  return true;
}

// runs in stage 4 (non-RT-thread)
bool deleteOldCode(World *world, void *raw_callback) {
  auto entry = static_cast<NewDynGenLibraryEntry*>(raw_callback);
  delete entry->oldCode;
  return true;
}

// frees the created struct. Uses RTFree since this has been managed
// by the RT thread.
void pluginCmdCallbackCleanup(World *world, void *raw_callback) {
  auto callBackData = static_cast<NewDynGenLibraryEntry*>(raw_callback);
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
