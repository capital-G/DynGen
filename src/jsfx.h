#pragma once

#include "eel2_adapter.h"
#include <SC_PlugIn.hpp>

static InterfaceTable *ft;

class SC_JSFX : public SCUnit {
public:
  SC_JSFX();

private:
  EEL2Adapter vm;
  SndBuf *mScriptBuffer;

  void next(int numSamples);
};
