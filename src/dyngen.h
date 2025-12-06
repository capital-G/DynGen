#pragma once

#include "eel2_adapter.h"
#include <SC_PlugIn.hpp>

static InterfaceTable *ft;

struct CodeLibrary;

class DynGen : public SCUnit {
public:
  DynGen();
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
private:
  int mCodeID;

  void next(int numSamples);
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

  // the running dyngen to be updtade
  DynGen *dynGen;
  // the new code to be used
  char* code;

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

// used as callback data. Gets created in RT thread, passed to NRT threads
// for consumption and freed in RT thread again, so it is managed by RT malloc.
struct NewDynGenLibraryEntry {
  // while in sclang land we use strings to identify
  int hash;
  // absolute path to the file storing the DynGen code
  char* codePath;

  // the newly received code - RTAlloc
  char* code;
  // the code to be replaced and should be deleted - RTAlloc
  char* oldCode;
};
