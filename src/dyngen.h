#pragma once

#include "library.h"

#include <SC_PlugIn.hpp>

struct EEL2Adapter;

/*! @class DynGen
 *  @brief The UGen which runs an associated DynGen script from the Library.
 */
class DynGen : public SCUnit {
public:
    DynGen();
    /*! @brief updates vm in an async manner.
     *  Returns false in case the allocation of the callback data failed.
     */
    bool updateCode(const DynGenScript* script) const;
    ~DynGen();

    /*! @brief the active vm - at the point it is not a null pointer it will
     *  be consumed. Owned by NRT thread.
     */
    EEL2Adapter* mVm = nullptr;
    /*! @brief since a DynGen is linked to a single code instance it
     *  is sufficient to link all DynGen instances with the same
     *  code internally
     */
    DynGen* mPrevDynGen = nullptr;
    DynGen* mNextDynGen = nullptr;
    /*! @brief we need a reference to the used CodeLibrary b/c in case we get
     *  freed we may have to update the associated linked list
     */
    CodeLibrary* mCodeLibrary = nullptr;

    DynGenStub* mStub = nullptr;

private:
    enum {
        CodeIDIndex = 0,
        UpdateIndex = 1,
        RealTimeIndex = 2,
        NumInputsIndex = 3,
        NumParametersIndex = 4,
        InputOffset = 5
    };
    int mCodeID;
    int mNumDynGenInputs;
    int mNumDynGenParameters;
    int* mParameterIndices = nullptr;

    void next(int numSamples);

    /*! @brief ~DynGen callback to destroy the vm in a NRT thread on stage 2 */
    static bool deleteVmOnSynthDestruction(World* world, void* rawCallbackData);

    // *****************************
    // NRT callbacks for DynGen init
    // *****************************

    /*! @brief stage 2 - NRT */
    static bool createVmAndCompile(World* world, void* rawCallbackData);
    /*! @brief stage 3 - RT */
    static bool swapVmPointers(World* world, void* rawCallbackData);
    /*! @brief stage 4 - NRT */
    static bool deleteOldVm(World* world, void* rawCallbackData);
    /*! @brief cleanup - RT */
    static void dynGenInitCallbackCleanup(World* world, void* rawCallbackData);
};
