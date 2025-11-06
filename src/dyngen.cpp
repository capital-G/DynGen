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

// Gets called deferred from the UGen init in a
// stage2 thread which is NRT - so it is safe to allocate
// memory and also spawn a thread to which we offload
// all the heavy lifting of initializing the VM.
bool dynGenCompileCallback(World *world, void *rawCallbackData) {
  auto callbackData = static_cast<DynGenCallbackData*>(rawCallbackData);

  CodeLibrary* node = gLibrary;
  while (node && node->id != callbackData->scriptHash) {
    node = node->next;
  }

  if (!node) {
    Print("Could not find script with hash %i\n", callbackData->scriptHash);
    return false;
  }

  callbackData->adapter->init(node->code);

  // insert the dyngen node
  callbackData->dynGenNode->next = node->dynGenNodes;
  node->dynGenNodes = callbackData->dynGenNode;

  // do not continue to stage 3
  return false;
}

void dynGenCompileCallbackCleanup(World *world, void *raw_callback) {
  auto callback = static_cast<DynGenCallbackData *>(raw_callback);
  RTFree(world, callback);
}

// *********
// UGen code
// *********
DynGen::DynGen() : vm(mNumInputs-2, mNumOutputs, static_cast<int>(sampleRate()), mWorld, mParent) {
  mCodeID = static_cast<int>(in0(0));
  bool useAudioThread = in0(1) > 0.5;

  if (useAudioThread) {
    // do init of VM in RT thread - this is dangerous and should not be done,
    // yet it get rids of one block size delay until the signal appears.
    // Since the VM init seems to be often fast enough we allow the user
    // to decide, yet this is not the default case.
    auto node = gLibrary;
    bool found = false;
    while (node!=nullptr) {
      if (node->id == mCodeID) {
        found = true;
        vm.init(node->code);

        auto const unitNode = static_cast<CodeLibrary::DynGenNode*>(RTAlloc(mWorld, sizeof(CodeLibrary::DynGenNode)));
        unitNode->dynGenUnit = this;
        unitNode->next = node->dynGenNodes;
        node->dynGenNodes = unitNode;
        break;
      }
      node = node->next;
    }
    if (!found) {
      Print("Could not find script with hash %i\n", mCodeID);
    }
  } else {
    // offload VM init to NRT thread
    auto payload = static_cast<DynGenCallbackData*>(RTAlloc(mWorld, sizeof(DynGenCallbackData)));

    auto unit = this; // the macro needs a reference to unit
    ClearUnitIfMemFailed(payload);

    auto codeNode = static_cast<CodeLibrary::DynGenNode*>(RTAlloc(mWorld, sizeof(CodeLibrary::DynGenNode)));
    ClearUnitIfMemFailed(codeNode);
    codeNode->dynGenUnit = this;
    codeNode->next = nullptr;

    payload->scriptHash = mCodeID;
    payload->adapter = &vm;
    payload->dynGenNode = codeNode;

    ft->fDoAsynchronousCommand(
        mWorld, nullptr, nullptr, static_cast<void*>(payload),
        dynGenCompileCallback, nullptr,nullptr, dynGenCompileCallbackCleanup, 0, nullptr);
  }

  set_calc_function<DynGen, &DynGen::next>();
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
}

void DynGen::next(int numSamples) {
  if (!vm.mReady.load()) {
    for (int i = 0; i < mNumOutputs; i++) {
      Clear(numSamples, mOutBuf[i]);
    }
  } else {
    // skip first 2 channels since those are not signals
    vm.process(mInBuf + 2, mOutBuf, numSamples);
  }
}

void DynGen::updateCode(const std::string &code) {
  vm.mReady.store(false, std::memory_order_release);
  // init sets the mReady variable when finished/successful
  vm.init(code);
}


// ************
// Library code
// ************

// this runs in stage 2 (NRT) and enters the content of the file to
// the library by traversing the library which is a linked list.
// If the hash ID already exists the code gets updated and all running
// instances should be updated.
bool enterFileToDynGenLibrary(World *world, void *raw_callback) {
  auto entry = static_cast<NewDynGenLibraryEntry*>(raw_callback);

  // read file, see https://stackoverflow.com/a/19922123
  std::ifstream codeFile;
  codeFile.open(entry->codePath);
  std::stringstream codeStream;
  codeStream << codeFile.rdbuf();
  const std::string code = codeStream.str();

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
      code,
    };
    gLibrary = newNode;
  } else {
    node->code = code;
    for (auto* unit = node->dynGenNodes; unit; unit = unit->next) {
      unit->dynGenUnit->updateCode(code);
    }
  }

  // do not continue to stage 3 by returning false
  return false;
}

// frees the created struct. Uses RTFree since this has been managed
// by the RT thread.
void pluginCmdCallbackCleanup(World *world, void *raw_callback) {
  auto callBackData = static_cast<NewDynGenLibraryEntry*>(raw_callback);
  RTFree(world, callBackData->codePath);
  RTFree(world, callBackData);
}


// responds to an osc message on the RT thread - we therefore have to
// copy the OSC data to a new struct which then gets passed to another
// callback which runs in stage 2, aka non-rt. We have to
// free the created struct afterward.
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

  ft->fDoAsynchronousCommand(
    inWorld, nullptr, nullptr, static_cast<void*>(newLibraryEntry),
    enterFileToDynGenLibrary, nullptr,nullptr, pluginCmdCallbackCleanup, 0, nullptr);
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
