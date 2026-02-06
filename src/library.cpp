#include "library.h"
#include "dyngen.h"
#include "eel2_adapter.h"

#include <fstream>
#include <memory>

//-------------------- DynGenScript -------------------//

namespace {

bool validateBlockOrder(size_t posInit, size_t posBlock, size_t posSample) {
    size_t lastPos = 0;
    if (posInit != std::string_view::npos) {
        lastPos = posInit;
    }
    if (posBlock != std::string_view::npos) {
        if (posBlock < lastPos) {
            return false;
        }
        lastPos = posBlock;
    }
    if (posSample != std::string_view::npos) {
        if (posSample < lastPos) {
            return false;
        }
    }
    return true;
}

} // namespace

bool DynGenScript::parse(std::string_view script) {
    std::string_view init("@init\n");
    std::string_view block("@block\n");
    std::string_view sample("@sample\n");

    auto posInit = script.find(init);
    auto posBlock = script.find(block);
    auto posSample = script.find(sample);

    // if no blocks given -> use code as sample block
    if (posInit == std::string_view::npos && posSample == std::string_view::npos
        && posBlock == std::string_view::npos) {
        mSample = script;
        return true;
    };

    if (posSample == std::string_view::npos) {
        Print("DynGen script requires a sample section\n");
        return false;
    }

    if (!validateBlockOrder(posInit, posBlock, posSample)) {
        Print("DynGen: Wrong script block order, requires @init, @block, @sample "
              "order\n");
        return false;
    }

    // skip the matched strings!
    auto startInit = (posInit != std::string_view::npos) ? posInit + init.size() : std::string_view::npos;
    auto startBlock = (posBlock != std::string_view::npos) ? posBlock + block.size() : std::string_view::npos;
    auto startSample = (posSample != std::string_view::npos) ? posSample + sample.size() : std::string_view::npos;

    if (posInit != std::string_view::npos) {
        const auto endPos = posBlock != std::string_view::npos ? posBlock : posSample;
        mInit = std::string(script.substr(startInit, endPos - startInit));
    }

    if (posBlock != std::string_view::npos) {
        mBlock = std::string(script.substr(startBlock, posSample - startBlock));
    }

    mSample = std::string(script.substr(startSample));

    return true;
}

bool DynGenScript::tryCompile() {
    EEL2Adapter state(0, 0, 0, 0, nullptr, nullptr);
    return state.init(*this, nullptr, 0);
}

//-------------------- CodeLibrary --------------------//

void CodeLibrary::addUnit(DynGen* unit) {
    // add ourselves to the linked list of DynGen nodes
    if (mDynGen) {
        mDynGen->mPrevDynGen = unit;
    }
    unit->mNextDynGen = mDynGen;
    mDynGen = unit;
}

void CodeLibrary::removeUnit(DynGen* unit) {
    // readjust the head of the linked list of the code library if necessary
    if (mDynGen == unit) {
        mDynGen = unit->mNextDynGen;
    }
    // remove ourselves from the linked list
    if (unit->mPrevDynGen != nullptr) {
        unit->mPrevDynGen->mNextDynGen = unit->mNextDynGen;
    }
    if (unit->mNextDynGen != nullptr) {
        unit->mNextDynGen->mPrevDynGen = unit->mPrevDynGen;
    }
}

bool CodeLibrary::isReadyToBeFreed() const { return mShouldBeFreed && mDynGen == nullptr; }

//--------------------- Library ----------------------//

// a global linked list which stores the code
// and its associated running DynGens.
CodeLibrary* gLibrary = nullptr;

CodeLibrary* Library::findCode(int codeID) {
    for (auto node = gLibrary; node; node = node->mNext) {
        if (node->mID == codeID) {
            return node;
        }
    }
    return nullptr;
}

void Library::freeNode(CodeLibrary* node, bool async) {
    World* world = node->mWorld;

    // remove node from linked list
    assert(gLibrary != nullptr);
    auto curNode = gLibrary;
    CodeLibrary* prevNode = nullptr;
    while (curNode != nullptr && curNode != node) {
        prevNode = curNode;
        curNode = curNode->mNext;
    }
    assert(curNode == node);
    if (prevNode != nullptr) {
        prevNode->mNext = node->mNext;
    } else {
        gLibrary = node->mNext;
    }

    node->mShouldBeFreed = true;

    // we need to obtain a handle so we can delete it in the NRT thread
    auto script = node->mScript;
    // avoid a dangling pointer after mScript has been freed
    // this should not be accessed b/c mShouldbeFreed has been set, but just to be safe
    node->mScript = nullptr;

    // if no dyngen instance is associated with this script anymore,
    // we can safely delete it. This gets also checked in
    // the destructor of DynGen, so eventually it will be freed.
    if (node->mDynGen == nullptr) {
        RTFree(world, node);
    }

    if (async) {
        // defer deletion to NRT and RT thread since script is NRT allocated
        ft->fDoAsynchronousCommand(
            world, nullptr, nullptr, script,
            [](World*, void* data) {
                auto script = static_cast<DynGenScript*>(data);
                delete script;
                return false;
            },
            nullptr, nullptr, [](World* inWorld, void*) {}, 0, nullptr);
    } else {
        delete script;
    }
}

void Library::buildGenericPayload(World* inWorld, sc_msg_iter* args, const bool isFile) {
    auto newLibraryEntry = static_cast<NewDynGenLibraryEntry*>(RTAlloc(inWorld, sizeof(NewDynGenLibraryEntry)));
    if (!newLibraryEntry) {
        Print("ERROR: Failed to allocate memory for DynGen library entry\n");
        return;
    }
    // init pointers such that we can use generic cleanup method
    newLibraryEntry->oscString = nullptr;
    newLibraryEntry->numParameters = 0;
    newLibraryEntry->parameterNamesRT = nullptr;
    newLibraryEntry->oldScript = nullptr;

    newLibraryEntry->hash = args->geti();

    if (const char* codePath = args->gets()) {
        auto codePathLength = strlen(codePath) + 1;
        newLibraryEntry->oscString = static_cast<char*>(RTAlloc(inWorld, codePathLength));
        if (!newLibraryEntry->oscString) {
            Print("ERROR: Failed to allocate memory for DynGen code library\n");
            rtCleanup(inWorld, newLibraryEntry, 0);
            return;
        }
        std::copy_n(codePath, codePathLength, newLibraryEntry->oscString);
    } else {
        Print("ERROR: Invalid dyngenfile message\n");
        rtCleanup(inWorld, newLibraryEntry, 0);
        return;
    }

    newLibraryEntry->numParameters = args->geti();
    newLibraryEntry->parameterNamesRT = static_cast<char**>(RTAlloc(inWorld, newLibraryEntry->numParameters));
    for (int i = 0; i < newLibraryEntry->numParameters; i++) {
        if (const char* rawParam = args->gets()) {
            auto paramLength = strlen(rawParam) + 1;
            auto paramName = static_cast<char*>(RTAlloc(inWorld, paramLength));
            if (!paramName) {
                Print("ERROR: Failed to allocate memory for DynGen parameter names\n");
                rtCleanup(inWorld, newLibraryEntry, i - 1);
                return;
            }
            std::copy_n(rawParam, paramLength, paramName);
            newLibraryEntry->parameterNamesRT[i] = paramName;
        } else {
            Print("ERROR: Invalid dyngenscript message of parameters\n");
            rtCleanup(inWorld, newLibraryEntry, i - 1);
            return;
        }
    }

    auto [completionMsgSize, completionMsg] = getCompletionMsg(args);

    ft->fDoAsynchronousCommand(inWorld, nullptr, nullptr, static_cast<void*>(newLibraryEntry),
                               isFile ? loadFileToDynGenLibrary : loadScriptToDynGenLibrary, swapCode, deleteOldCode,
                               pluginCmdCallbackCleanup, completionMsgSize, const_cast<char*>(completionMsg));
}

void Library::rtCleanup(World* inWorld, NewDynGenLibraryEntry* newLibraryEntry, int numRtParameters) {
    if (newLibraryEntry == nullptr) {
        return;
    }

    RTFree(inWorld, newLibraryEntry->oscString);
    for (int j = 0; j < numRtParameters; j++) {
        RTFree(inWorld, newLibraryEntry->parameterNamesRT[j]);
    }
    RTFree(inWorld, newLibraryEntry->parameterNamesRT);
    RTFree(inWorld, newLibraryEntry);
}

void Library::dyngenAddFileCallback(World* inWorld, void* inUserData, sc_msg_iter* args, void* replyAddr) {
    buildGenericPayload(inWorld, args, true);
}

void Library::addScriptCallback(World* inWorld, void* inUserData, sc_msg_iter* args, void* replyAddr) {
    buildGenericPayload(inWorld, args, false);
}

void Library::freeScriptCallback(World* inWorld, void* inUserData, sc_msg_iter* args, void* replyAddr) {
    if (args->nextTag('f') != 'i') {
        Print("Error: Invalid DynGenFree message\n");
        return;
    }
    const auto codeId = args->geti();
    const auto code = findCode(codeId);
    if (code == nullptr) {
        Print("Error: Could not free DynGen script with ID %d: not found\n", codeId);
        return;
    };

    freeNode(code, true);
}

void Library::freeAllScriptsCallback(World* inWorld, void* inUserData, sc_msg_iter* args, void* replyAddr) {
    while (gLibrary != nullptr) {
        freeNode(gLibrary, true);
    }
}

void Library::cleanup() {
    // NOTE: we reuse the logic from freeNode() because it is actually not defined
    // *when* the plugin's unload function is called. In fact, as of SC 3.14 it is
    // called *before* all Graphs are destroyed! (This can be considered a bug and
    // and should be fixed in SC 3.15.) If we just destroy the list, the remaining
    // DynGen units would try to access a stale pointer and crash!
    while (gLibrary != nullptr) {
        // free synchronously!
        freeNode(gLibrary, false);
    }
}

bool Library::loadCodeToDynGenLibrary(NewDynGenLibraryEntry* newLibraryEntry, std::string_view code) {
    auto script = std::make_unique<DynGenScript>();

    if (!script->parse(code)) {
        return false;
    }

#if 1
    // already try to compile before creating/updating any DynGen instances.
    if (!script->tryCompile()) {
        return false;
    }
#endif

    // create parameter list
    for (int i = 0; i < newLibraryEntry->numParameters; i++) {
        script->mParameters.push_back(newLibraryEntry->parameterNamesRT[i]);
    }

    newLibraryEntry->script = script.release();

    // continue with next stage
    return true;
}

bool Library::loadScriptToDynGenLibrary(World* world, void* rawCallbackData) {
    const auto entry = static_cast<NewDynGenLibraryEntry*>(rawCallbackData);

    return loadCodeToDynGenLibrary(entry, entry->oscString);
}

bool Library::loadFileToDynGenLibrary(World* world, void* rawCallbackData) {
    auto entry = static_cast<NewDynGenLibraryEntry*>(rawCallbackData);

    auto codeFile = std::ifstream(entry->oscString, std::ios::binary);
    if (!codeFile.is_open()) {
        Print("ERROR: Could not open DynGen file at %s\n", entry->oscString);
        return false;
    }

    codeFile.seekg(0, std::ios::end);
    const std::streamsize codeSize = codeFile.tellg();
    codeFile.seekg(0);

    std::string codeBuffer;
    codeBuffer.resize(codeSize);
    codeFile.read(codeBuffer.data(), codeSize);

    return loadCodeToDynGenLibrary(entry, codeBuffer);
}

bool Library::swapCode(World* world, void* rawCallbackData) {
    const auto entry = static_cast<NewDynGenLibraryEntry*>(rawCallbackData);

    CodeLibrary* node = Library::findCode(entry->hash);

    if (!node) {
        // create new code node
        auto* newNode = static_cast<CodeLibrary*>(RTAlloc(world, sizeof(CodeLibrary)));
        if (!newNode) {
            Print("ERROR: Failed to allocate memory for new code library\n");
            return true;
        }
        newNode->mNext = gLibrary;
        newNode->mWorld = world;
        newNode->mID = entry->hash;
        newNode->mDynGen = nullptr;
        newNode->mScript = entry->script;
        newNode->mShouldBeFreed = false;
        gLibrary = newNode;
    } else {
        // swap code
        entry->oldScript = node->mScript;
        node->mScript = entry->script;

        for (auto dynGen = node->mDynGen; dynGen != nullptr; dynGen = dynGen->mNextDynGen) {
            // although the code can be updated, the referenced code
            // lives long enough b/c in worst case there is already
            // a new code in the pipeline at stage2 where the old code
            // would be destroyed in its stage4.
            // Yet we only need to access the code in stage 2 in our callback,
            // where it could not have been destroyed yet.
            // See
            // https://github.com/capital-G/DynGen/pull/40#discussion_r2599579920
            // clang-format off
/*
     ┌─────────┐             ┌──────────┐           ┌─────────┐          ┌──────────┐
     │STAGE1_RT│             │STAGE2_NRT│           │STAGE3_RT│          │STAGE4_NRT│
     └────┬────┘             └─────┬────┘           └────┬────┘          └─────┬────┘
          │loadFileToDynGenLibrary │                     │                     │
          │───────────────────────>│                     │                     │
          │                        │                     │                     │
          │                        │      swapCode       │                     │
          │                        │────────────────────>│                     │
          │                        │                     │                     │
          │loadFileToDynGenLibrary │                     │                     │
          │ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ >│                     │                     │
          │                        │                     │                     │
          │     ╔════════════════╗ │createVmAndCompileA  │                     │
          │     ║accessing code ░║ │<────────────────────│                     │
          │     ╚════════════════╝ │                     │                     │
          │                        │createVmAndCompileB  │                     │
          │                        │<────────────────────│                     │
          │                        │                     │                     │
          │    ╔═════════════════╗ │      swapCode       │                     │
          │    ║code -> oldCode ░║ │ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─>│                     │
          │    ╚═════════════════╝ │                     │                     │
          │                        │                     │   deleteOldCode     │
          │                        │                     │────────────────────>│
          │                        │                     │                     │
          │                        │  swapVmPointersA    │                     │
          │                        │────────────────────>│                     │
          │                        │                     │                     │
          │                        │  swapVmPointersB    │                     │
          │                        │────────────────────>│                     │
          │                        │                     │                     │
          │                 ╔══════╧═══════════════════╗ │   deleteOldCode     │
          │                 ║deleting code as oldCode ░║ │ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─>│
          │                 ╚══════╤═══════════════════╝ │                     │
          │                        │                     │    deleteOldVm      │
          │                        │                     │────────────────────>│
     ┌────┴────┐             ┌─────┴────┐           ┌────┴────┐          ┌─────┴────┐
     │STAGE1_RT│             │STAGE2_NRT│           │STAGE3_RT│          │STAGE4_NRT│
     └─────────┘             └──────────┘           └─────────┘          └──────────┘

source code:
```plantuml
@startuml
STAGE1_RT -> STAGE2_NRT : loadFileToDynGenLibrary
STAGE2_NRT -> STAGE3_RT : swapCode
STAGE1_RT --> STAGE2_NRT : loadFileToDynGenLibrary
STAGE3_RT -> STAGE2_NRT : createVmAndCompileA
note left: accessing code
STAGE3_RT -> STAGE2_NRT : createVmAndCompileB
STAGE2_NRT --> STAGE3_RT: swapCode
note left: code -> oldCode
STAGE3_RT -> STAGE4_NRT : deleteOldCode
STAGE2_NRT -> STAGE3_RT: swapVmPointersA
STAGE2_NRT -> STAGE3_RT: swapVmPointersB
STAGE3_RT --> STAGE4_NRT : deleteOldCode
note left: deleting code as oldCode
STAGE3_RT -> STAGE4_NRT : deleteOldVm
@enduml
```
*/
            // clang-format on
            dynGen->updateCode(entry->script);
        }
    }
    return true;
}

bool Library::deleteOldCode(World* world, void* rawCallbackData) {
    auto entry = static_cast<NewDynGenLibraryEntry*>(rawCallbackData);
    delete entry->oldScript;
    return true;
}

void Library::pluginCmdCallbackCleanup(World* world, void* rawCallbackData) {
    auto callBackData = static_cast<NewDynGenLibraryEntry*>(rawCallbackData);
    for (int i = 0; i < callBackData->numParameters; i++) {
        RTFree(world, callBackData->parameterNamesRT[i]);
    }
    RTFree(world, callBackData->parameterNamesRT);
    RTFree(world, callBackData->oscString);
    RTFree(world, callBackData);
}

std::pair<int, const char*> Library::getCompletionMsg(sc_msg_iter* args) {
    auto const completionMsgSize = static_cast<int>(args->getbsize());
    const char* completionMsg = nullptr;
    if (completionMsgSize > 0) {
        auto* readPos = args->rdpos;
        // point to the buf data of the completion msg - args->getb
        // would make a copy which we do not want since
        // `fDoAsynchronousCommand` already copies the buffer
        completionMsg = readPos + sizeof(int32_t);
        args->skipb();
    }
    return { completionMsgSize, completionMsg };
}
