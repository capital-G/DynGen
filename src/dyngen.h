#pragma once

#include "eel2_adapter.h"
#include "library.h"
#include <SC_PlugIn.hpp>

struct CodeLibrary;
struct DynGenStub;

/*! @class DynGen
 *  @brief The UGen which runs an associated DynGen script from the Library.
 */
class DynGen : public SCUnit {
public:
  DynGen();
  /*! @brief updates vm in an async manner.
   *  Returns false in case the allocation of the callback data failed.
   */
  bool updateCode(const char *code, char** parameters) const;
  ~DynGen();

  /*! @brief the active vm - at the point it is not a null pointer it will
   *  be consumed. Owned by NRT thread.
   */
  EEL2Adapter* mVm = nullptr;
  /*! @brief since a DynGen is linked to a single code instance it
   *  is sufficient to link all DynGen instances with the same
   *  code internally
   */
  DynGen* mPrevDynGen;
  DynGen* mNextDynGen;
  /*! @brief we need a reference to the used CodeLibrary b/c in case we get
   *  freed we may have to update the associated linked list
   */
  CodeLibrary* mCodeLibrary;

  DynGenStub* mStub;
private:
  int mCodeID;
  int mNumDynGenInputs;
  int mNumDynGenParameters;
  int* mParameterIndices;
  int mNumParameters;

  void next(int numSamples);

  /*! @brief ~DynGen callback to destroy the vm in a NRT thread on stage 2 */
  static bool deleteVmOnSynthDestruction(World *world, void *rawCallbackData);

  // *****************************
  // NRT callbacks for DynGen init
  // *****************************

  /*! @brief stage 2 - NRT */
  static bool createVmAndCompile(World* world, void *rawCallbackData);
  /*! @brief stage 3 - RT */
  static bool swapVmPointers(World* world, void *rawCallbackData);
  /*! @brief stage 4 - NRT */
  static bool deleteOldVm(World* world, void *rawCallbackData);
  /*! @brief cleanup - RT */
  static void dynGenInitCallbackCleanup(World *world, void *rawCallbackData);

  /*! @brief dummy task b/c we are already deleting the vm above which
   *  is the pointer we pass around
   */
  static void doNothing(World *world, void *rawCallbackData) {}
};
