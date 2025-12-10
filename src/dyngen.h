#pragma once

#include "eel2_adapter.h"
#include <SC_PlugIn.hpp>

// forward declarations
struct CodeLibrary;
struct DynGenStub;
struct DynGenCallbackData;

class DynGen : public SCUnit {
public:
  DynGen();
  // updates vm in an async manner.
  // Returns false in case the allocation of the callback data failed.
  bool updateCode(const char *code) const;
  ~DynGen();

  // the active vm - at the point it is not a null pointer it will
  // be consumed. Owned by NRT thread.
  EEL2Adapter* mVm = nullptr;
  // since a DynGen is linked to a single code instance it
  // is sufficient to link all DynGen instances with the same
  // code internally
  DynGen* mPrevDynGen;
  DynGen* mNextDynGen;
  // we need a reference to the used CodeLibrary b/c in case we get freed
  // we may have to update the associated linked list
  CodeLibrary* mCodeLibrary;

  DynGenStub* mStub;
private:
  int mCodeID;

  void next(int numSamples);
};

// RT owned
// because we use async command to delay the vm initialization of a DynGen
// it is possible that the server deletes the UGen/DynGen while we are preparing
// the vm.
// In case we would put the vm in place, this would result in a crash
// because the DynGen object has already been deleted by the server.
// Therefore, instead of passing around the DynGen itself we pass around
// this DynGenStub, which also holds a reference counter.
// The ref count starts at 1 and gets incremented during each callback.
// When a DynGen gets destroyed, the ref counter gets decremented and its DynGen
// reference gets set to nullptr.
// When an async command wants to swap the vm,  a nullptr check
// can be performed on mObject, indicating if the object is still
// existing or not.
// Once the reference counter hits 0, the stub can be destroyed, which can
// either happen during ~DynGen or within the free function of the async
// update command.
struct DynGenStub {
  DynGen* mObject;
  size_t mRefCount;
};

// A linked list which manages stores the code under a given ID (id/code) and
// also another linked list (DynGen*) which stores all the running
// DynGen instances with the associated code, which allows us to update
// the running instances in case the code changes.
// There is currently no way to delete items from the linked list.
struct CodeLibrary {
  // the next entry in the linked list
  CodeLibrary *next;
  // we refer to scripts via ID in order to avoid storing
  // and sending strings via OSC
  int id;
  // references the first DynGen - all other instances can be accessed
  // through the double linked list of DynGen
  DynGen *dynGen;
  // the eel2 code currently associated with the DynGen instance
  char* code;
};

// struct to be passed around to update already running dyngen nodes
struct DynGenCallbackData {
  // vm is NRT managed - we flip the vm in RT thread, but perpare and
  // delete it in NRT context
  EEL2Adapter *vm;
  EEL2Adapter *oldVm;

  // the running dyngen stub to be updated
  DynGenStub *dynGenStub;
  // the new code to be used
  const char* code;

  // vm init
  uint32 numInputChannels;
  uint32 numOutputChannels;
  int sampleRate;
  int blockSize;
  // necessary to access params such as sample rate and RTFree
  World *world;
  // necessary for accessing local buffers
  Graph *parent;
};

// The callback payload to enter a new entry into the code library, which
// gets invoked via an OSC message/command.
// This can either
// a) hold `codePath` in which case we read its content in stage 2
// b) hold `rtCode` in which we copy the RT char* into a NRT char* in stage2
struct NewDynGenLibraryEntry {
  // while in sclang land we use strings to identify
  int hash;

  // This can hold 2 alternatives, both RT managed and 0 terminated
  // Alternative A: read code from file
  // absolute path to the file storing the DynGen code
  // Alternative B: code was bundled within OSC message
  // RT managed code we receive via script command
  char* oscString;

  // the newly received code - NRT managed
  char* code;
  // the code to be replaced and should be deleted - NRT managed
  char* oldCode;
};
