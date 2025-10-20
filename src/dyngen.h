#pragma once

#include "eel2_adapter.h"
#include <SC_PlugIn.hpp>

static InterfaceTable *ft;

class DynGen : public SCUnit {
public:
  DynGen();
  ~DynGen();

  // not RT safe
  void updateCode(const std::string &code);
private:
  EEL2Adapter vm;
  SndBuf *mScriptBuffer;
  int mCodeID;

  void next(int numSamples);
};

// A linked list which manages stores the code under a given ID (id/code) and
// also another internal linked list (DynGenNode) which stores all the running
// DynGen instances with the associated code, which allows us to update
// the running instances in case the code changes.
// There is currently no way to delete items from the linked list.
struct CodeLibrary {
  struct DynGenNode {
    DynGenNode *next;
    // this is RT managed
    DynGen *dynGenUnit;
  };
  CodeLibrary *next;
  int id;
  DynGenNode *dynGenNodes;
  std::string code;
};

struct DynGenCallbackData {
  int scriptHash;
  EEL2Adapter *adapter;
  CodeLibrary::DynGenNode* dynGenNode;
};

// used as callback data. Gets created in RT thread, passed to NRT threads
// for consumption and freed in RT thread again, so it is managed by RT malloc.
struct NewDynGenLibraryEntry {
  int hash;
  char* codePath;
};
