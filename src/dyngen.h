#pragma once

#include "eel2_adapter.h"
#include <SC_PlugIn.hpp>

static InterfaceTable *ft;

class DynGen : public SCUnit {
public:
  DynGen();

private:
  EEL2Adapter vm;
  SndBuf *mScriptBuffer;

  void next(int numSamples);
};
