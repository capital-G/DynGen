#include "dyngen.h"
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

// @todo create something like a tokio::channel to communicate changes/updates

typedef std::map<int, std::string> DynGenLibrary;
// the dictionary becomes accessible through the atomic reference counting
// of a shared_ptr - for write access we need an additional mutex which will
// replace the underlying library
std::shared_ptr<DynGenLibrary> gLibrary;
std::mutex gLibraryMutex;

void addDynGenScriptToLibrary(int hash, const std::string& code) {
  // lock the library and create a copy
  std::lock_guard<std::mutex> lock(gLibraryMutex);
  auto newLibrary = std::make_shared<DynGenLibrary>(*gLibrary);
  newLibrary->insert({hash, code});
  // replace the library
  gLibrary = newLibrary;
}


struct DynGenCallback {
  int scriptHash;
  EEL2Adapter *adapter;
};

void callbackCleanup(World *world, void *raw_callback) {
  auto callback = static_cast<DynGenCallback *>(raw_callback);
  RTFree(world, callback);
}

// this gets called deferred from the UGen init in a
// stage2 thread which is NRT - so it is safe to allocate
// memory and also spawn a thread to which we offload
// all the heavy lifting of initializing the VM.
bool dynGenCallback(World *world, void *rawCallbackData) {
  auto callbackData = static_cast<DynGenCallback*>(rawCallbackData);

  auto lib = gLibrary;

  // we don't spawn a thread here and accept that we may block the NRT thread
  // b/c otherwise s.sync does not work as expected
  if (auto libraryEntry = lib->find(callbackData->scriptHash); libraryEntry != lib->end()) {
    auto code = libraryEntry->second;
    callbackData->adapter->init(code);
  } else {
    Print("Could not find script with hash %i\n", callbackData->scriptHash);
    // @todo clear unit/outputs
  }

  // do not continue to stage 3
  return false;
}

DynGen::DynGen() : vm(static_cast<int>(in0(2)), static_cast<int>(in0(0)), static_cast<int>(sampleRate())) {
  int scriptHash = static_cast<int>(in0(1));
  bool useAudioThread = in0(3) > 0.5;

  if (useAudioThread) {
    auto lib = gLibrary;
    if (auto libraryEntry = lib->find(scriptHash); libraryEntry != lib->end()) {
      auto code = libraryEntry->second;
      vm.init(code);
    } else {
      Print("Could not find script with hash %i\n", scriptHash);
      // @todo clear unit/outputs
    }
  } else {
    auto payload = static_cast<DynGenCallback*>(RTAlloc(mWorld, sizeof(DynGenCallback)));

    auto unit = this; // the macro needs a reference to unit
    ClearUnitIfMemFailed(payload);

    payload->scriptHash = scriptHash;
    payload->adapter = &vm;

    ft->fDoAsynchronousCommand(
        mWorld, nullptr, nullptr, static_cast<void*>(payload),
        dynGenCallback, nullptr,nullptr, callbackCleanup, 0, nullptr);
  }

  set_calc_function<DynGen, &DynGen::next>();
  next(1);
}

void DynGen::next(int numSamples) {
  if (!vm.mReady.load()) {
    for (int i = 0; i < mNumOutputs; i++) {
      Clear(numSamples, mOutBuf[i]);
    }
  } else {
    // skip first 3 channels since those are not signals
    vm.process(mInBuf + 4, mOutBuf, numSamples);
  }
}

struct NewDynGenLibraryEntry {
  int hash;
  char* codePath;
};

// this runs in stage 2 and enters the content of the file to
// the library
bool enterToDynGenLibrary(World *world, void *raw_callback) {
  auto entry = static_cast<NewDynGenLibraryEntry*>(raw_callback);

  // read file, see https://stackoverflow.com/a/19922123
  std::ifstream codeFile;
  codeFile.open(entry->codePath);
  std::stringstream codeStream;
  codeStream << codeFile.rdbuf();
  std::string code = codeStream.str();

  // @todo maybe perform a test compile to tell the user if it works?
  addDynGenScriptToLibrary(entry->hash, code);

  // do not continue to stage 3
  return false;
}

void dynGenCallbackCleanup(World *world, void *raw_callback) {
  auto newEntry = static_cast<NewDynGenLibraryEntry*>(raw_callback);
  RTFree(world, newEntry->codePath);
  RTFree(world, newEntry);
}


// responds to an osc message on the RT thread - we therefore have to
// copy the OSC data to a new struct which we pass to a callback
void dynGenAddCallback(World* inWorld, void* inUserData, struct sc_msg_iter* args, void* replyAddr) {
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
  // std::cout << "Code path is " << newLibraryEntry->codePath << std::endl;

  ft->fDoAsynchronousCommand(
    inWorld, nullptr, nullptr, static_cast<void*>(newLibraryEntry),
    enterToDynGenLibrary, nullptr,nullptr, dynGenCallbackCleanup, 0, nullptr);
}

PluginLoad("DynGen") {
  ft = inTable;

  {
    std::lock_guard<std::mutex> lock(gLibraryMutex);
    gLibrary = std::make_shared<DynGenLibrary>();
  }

  NSEEL_init();

  registerUnit<DynGen>(inTable, "DynGen", false);
  registerUnit<DynGen>(inTable, "DynGenRT", false);

  ft->fDefinePlugInCmd(
    "dyngenadd",
    dynGenAddCallback,
    nullptr
  );
}
