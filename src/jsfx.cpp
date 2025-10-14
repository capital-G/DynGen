#include "jsfx.h"
#include <thread>
#include <map>
#include <fstream>
#include <iostream>
#include <sstream>

// @todo create something like a tokio::channel to communicate changes/updates

typedef std::map<int, std::string> JSFXLibrary;
// the dictionary becomes accessible through the atomic reference counting
// of a shared_ptr - for write access we need an additional mutex which will
// replace the underlying library
std::shared_ptr<JSFXLibrary> gLibrary;
std::mutex gLibraryMutex;

void addJSFXScriptToLibrary(int hash, const std::string& code) {
  // lock the library and create a copy
  std::lock_guard<std::mutex> lock(gLibraryMutex);
  auto newLibrary = std::make_shared<JSFXLibrary>(*gLibrary);
  newLibrary->insert({hash, code});
  // replace the library
  gLibrary = newLibrary;
}


struct SC_JSFX_Callback {
  int scriptHash;
  EEL2Adapter *adapter;
};

void callbackCleanup(World *world, void *raw_callback) {
  auto callback = static_cast<SC_JSFX_Callback *>(raw_callback);
  RTFree(world, callback);
}

// this gets called deferred from the UGen init in a
// stage2 thread which is NRT - so it is safe to allocate
// memory and also spawn a thread to which we offload
// all the heavy lifting of initializing the VM.
bool jsfxCallback(World *world, void *rawCallbackData) {
  auto callbackData = static_cast<SC_JSFX_Callback*>(rawCallbackData);

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

SC_JSFX::SC_JSFX() : vm(static_cast<int>(in0(2)), static_cast<int>(in0(0)), static_cast<int>(sampleRate())) {
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
    auto payload = static_cast<SC_JSFX_Callback*>(RTAlloc(mWorld, sizeof(SC_JSFX_Callback)));

    auto unit = this; // the macro needs a reference to unit
    ClearUnitIfMemFailed(payload);

    payload->scriptHash = scriptHash;
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

struct NewJSFXLibraryEntry {
  int hash;
  char* codePath;
};

// this runs in stage 2 and enters the content of the file to
// the library
bool enterToJSFXLibrary(World *world, void *raw_callback) {
  auto entry = static_cast<NewJSFXLibraryEntry*>(raw_callback);

  // read file, see https://stackoverflow.com/a/19922123
  std::ifstream codeFile;
  codeFile.open(entry->codePath);
  std::stringstream codeStream;
  codeStream << codeFile.rdbuf();
  std::string code = codeStream.str();

  // @todo maybe perform a test compile to tell the user if it works?
  addJSFXScriptToLibrary(entry->hash, code);

  // do not continue to stage 3
  return false;
}

void jsfxCallbackCleanup(World *world, void *raw_callback) {
  auto newEntry = static_cast<NewJSFXLibraryEntry*>(raw_callback);
  RTFree(world, newEntry->codePath);
  RTFree(world, newEntry);
}


// responds to an osc message on the RT thread - we therefore have to
// copy the OSC data to a new struct which we pass to a callback
void jsfxaddCallback(World* inWorld, void* inUserData, struct sc_msg_iter* args, void* replyAddr) {
  auto newLibraryEntry = static_cast<NewJSFXLibraryEntry*>(RTAlloc(inWorld, sizeof(NewJSFXLibraryEntry)));
  newLibraryEntry->hash = args->geti();
  if (const char* codePath = args->gets()) {
    newLibraryEntry->codePath = static_cast<char*>(RTAlloc(inWorld, strlen(codePath) + 1));
    if (!newLibraryEntry->codePath) {
      Print("Failed to allocate memory for JSFXCodeLibrary");
      return;
    }
    strcpy(newLibraryEntry->codePath, codePath);
  } else {
    Print("Invalid jsfxadd message\n");
    return;
  }
  // std::cout << "Code path is " << newLibraryEntry->codePath << std::endl;

  ft->fDoAsynchronousCommand(
    inWorld, nullptr, nullptr, static_cast<void*>(newLibraryEntry),
    enterToJSFXLibrary, nullptr,nullptr, jsfxCallbackCleanup, 0, nullptr);
}

PluginLoad("SC_JSFX") {
  ft = inTable;

  {
    std::lock_guard<std::mutex> lock(gLibraryMutex);
    gLibrary = std::make_shared<JSFXLibrary>();
  }

  NSEEL_init();

  registerUnit<SC_JSFX>(inTable, "JSFX", false);
  registerUnit<SC_JSFX>(inTable, "JSFXRT", false);

  ft->fDefinePlugInCmd(
    "jsfxadd",
    jsfxaddCallback,
    nullptr
  );
}
