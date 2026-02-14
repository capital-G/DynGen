// NOTE: include eel2_adapter.h before dyngen.h to prevent collision
// with IN and OUT macros on Windows!
#include "eel2_adapter.h"

#include "dyngen.h"

InterfaceTable* ft;

DynGen::DynGen() {
    mCodeID = static_cast<int>(in0(CodeIDIndex));
    const bool useAudioThread = in0(SyncIndex) != 0.0;
    mNumDynGenInputs = static_cast<int>(in0(NumInputsIndex));
    mNumDynGenParameters = static_cast<int>(in0(NumParametersIndex));
    assert(mNumDynGenInputs + mNumDynGenParameters + InputOffset <= numInputs());

    // call this before initializing the VM because we only want to clear the outputs.
    set_calc_function<DynGen, &DynGen::next>();

    // necessary for `ClearUnitIfMemFailed` macro
    auto unit = this;
    mStub = static_cast<DynGenStub*>(RTAlloc(mWorld, sizeof(DynGenStub)));
    ClearUnitIfMemFailed(mStub);
    mStub->mObject = this;
    mStub->mRefCount = 1;

    mParameterIndices = static_cast<int*>(RTAlloc(mWorld, sizeof(int) * mNumDynGenParameters));
    ClearUnitIfMemFailed(mParameterIndices);
    for (int i = 0; i < mNumDynGenParameters; i++) {
        // parameters come in index-value pairs, so only take each 2nd position
        auto paramIndex = static_cast<int>(in0(InputOffset + mNumDynGenInputs + (2 * i)));
        mParameterIndices[i] = paramIndex;
    }

    // get/create code library for this code ID. This only returns NULL when we're out of RT memory.
    mCodeLibrary = Library::getCode(mWorld, mCodeID);
    ClearUnitIfMemFailed(mCodeLibrary);
    // add ourselves to the code node so we can receive code updates.
    // we have to unregister ourself when the Unit is freed, see ~DynGen().
    mCodeLibrary->addUnit(this);

    // check if the entry actually contains a script. If not, we return with an error message.
    // NOTE: the user might later create the script, which would in turn update the UGen!
    if (mCodeLibrary->mScript == nullptr) {
        Print("ERROR: Could not find script with hash %i\n", mCodeID);
        return;
    }

    if (useAudioThread) {
        // do init of VM in RT thread - this is dangerous and should not be done,
        // yet it get rids of one block size delay until the signal appears.
        // Since the VM init seems to be often fast enough we allow the user
        // to decide, yet this is not the default case.
        auto vm =
            new EEL2Adapter(mNumDynGenInputs, mNumOutputs, static_cast<int>(sampleRate()), mBufLength, mWorld, mParent);

        if (vm->init(*mCodeLibrary->mScript, mParameterIndices, mNumDynGenParameters)) {
            mVm = vm;
        } else {
            delete vm;
        }
    } else {
        // offload VM init to NRT thread
        if (!updateCode(mCodeLibrary->mScript)) {
            ClearUnitOnMemFailed
        }
    }
}

void DynGen::next(int numSamples) {
    if (mVm == nullptr) {
        for (int i = 0; i < mNumOutputs; i++) {
            Clear(numSamples, mOutBuf[i]);
        }
    } else {
        mVm->process(mInBuf + InputOffset, mOutBuf, mInput + InputOffset + mNumDynGenInputs, numSamples);
    }
}

bool DynGen::updateCode(const DynGenScript* script) const {
    // If we already have a VM, our code is being updated.
    // In this case, the input at UpdateIndex controls the update behavior.
    if (mVm != nullptr) {
        bool shouldUpdate = in0(UpdateIndex) != 0.0;
        if (!shouldUpdate) {
            return true;
        }
    }

    // allocate extra space for parameter indices, see DynGenCallbackData.
    auto payloadSize = sizeof(DynGenCallbackData) + sizeof(int) * mNumDynGenParameters;
    auto payload = static_cast<DynGenCallbackData*>(RTAlloc(mWorld, payloadSize));

    // guard in case allocation fails
    if (payload) {
        payload->dynGenStub = mStub;
        payload->numInputChannels = mNumDynGenInputs;
        payload->numOutputChannels = mNumOutputs;
        payload->numParameters = mNumDynGenParameters;
        payload->sampleRate = static_cast<int>(sampleRate());
        payload->blockSize = mBufLength;
        payload->world = mWorld;
        payload->parent = mParent;
        payload->oldVm = nullptr;
        payload->script = script;

        for (int i = 0; i < mNumDynGenParameters; ++i) {
            payload->parameterIndices[i] = mParameterIndices[i];
        }

        // increment ref counter before we start the async command
        mStub->mRefCount += 1;

        ft->fDoAsynchronousCommand(mWorld, nullptr, nullptr, static_cast<void*>(payload), createVmAndCompile,
                                   swapVmPointers, deleteOldVm, dynGenInitCallbackCleanup, 0, nullptr);
    }

    return payload != nullptr;
}

DynGen::~DynGen() {
    RTFree(mWorld, mParameterIndices);

    mStub->mObject = nullptr; // invalidate stub!
    mStub->mRefCount -= 1;
    if (mStub->mRefCount == 0) {
        RTFree(mWorld, mStub);
    }

    if (mCodeLibrary) {
        // remove ourselves from the code library
        mCodeLibrary->removeUnit(this);

        if (mCodeLibrary->isReadyToBeFreed()) {
            RTFree(mWorld, mCodeLibrary);
        }
    }

    if (mVm) {
        // free the vm in RT context through async command
        ft->fDoAsynchronousCommand(
            mWorld, nullptr, nullptr, static_cast<void*>(mVm), deleteVmOnSynthDestruction, nullptr, nullptr,
            [](World*, void*) {}, 0, nullptr);
    }
}

bool DynGen::createVmAndCompile(World* world, void* rawCallbackData) {
    auto callbackData = static_cast<DynGenCallbackData*>(rawCallbackData);

    callbackData->vm =
        new EEL2Adapter(callbackData->numInputChannels, callbackData->numOutputChannels, callbackData->sampleRate,
                        callbackData->blockSize, callbackData->world, callbackData->parent);

    auto success =
        callbackData->vm->init(*callbackData->script, callbackData->parameterIndices, callbackData->numParameters);
    if (!success) {
        // if not successful, remove vm and do not attempt to replace
        // running vm.
        delete callbackData->vm;
        return false;
    }
    // continue with stage 3
    return true;
}

bool DynGen::swapVmPointers(World* world, void* rawCallbackData) {
    auto callbackData = static_cast<DynGenCallbackData*>(rawCallbackData);
    // only replace if DynGen instance is still existing
    if (callbackData->dynGenStub->mObject) {
        callbackData->oldVm = callbackData->dynGenStub->mObject->mVm;
        callbackData->dynGenStub->mObject->mVm = callbackData->vm;
    } else {
        // mark the vm we just created ready for deletion since the DynGen
        // it was created for does not exist anymore.
        callbackData->oldVm = callbackData->vm;
    }
    return true;
}

bool DynGen::deleteOldVm(World* world, void* rawCallbackData) {
    auto callbackData = static_cast<DynGenCallbackData*>(rawCallbackData);
    delete callbackData->oldVm;
    return true;
}

void DynGen::dynGenInitCallbackCleanup(World* world, void* rawCallbackData) {
    auto callback = static_cast<DynGenCallbackData*>(rawCallbackData);
    callback->dynGenStub->mRefCount -= 1;
    // destroy if there are no references to the DynGen
    if (callback->dynGenStub->mRefCount == 0) {
        RTFree(world, callback->dynGenStub);
    }
    RTFree(world, callback);
}

bool DynGen::deleteVmOnSynthDestruction(World* world, void* rawCallbackData) {
    const auto vm = static_cast<EEL2Adapter*>(rawCallbackData);
    delete vm;
    // do not return to stage 3 - we are done
    return false;
}

// ********************
// Plugin bootstrapping
// ********************

PluginLoad("DynGen") {
    ft = inTable;

    NSEEL_init();

    EEL2Adapter::setup();

    // disable buffer aliasing so that users do not have to worry about
    // 'out*' variables potentially aliasing 'in*' variables.
    registerUnit<DynGen>(inTable, "DynGen", true);

    ft->fDefinePlugInCmd("dyngenfile", Library::dyngenAddFileCallback, nullptr);

    ft->fDefinePlugInCmd("dyngenscript", Library::addScriptCallback, nullptr);

    ft->fDefinePlugInCmd("dyngenfree", Library::freeScriptCallback, nullptr);
    ft->fDefinePlugInCmd("dyngenfreeall", Library::freeAllScriptsCallback, nullptr);
}

PluginUnload("DynGen") {
    Library::cleanup();

    NSEEL_quit();
}
