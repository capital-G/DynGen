#include "library.h"

#include <sstream>

void Library::buildGenericPayload(
  World *inWorld,
  sc_msg_iter *args,
  const bool isFile
) {
  auto newLibraryEntry = static_cast<NewDynGenLibraryEntry *>(
      RTAlloc(inWorld, sizeof(NewDynGenLibraryEntry)));
  if (!newLibraryEntry) {
    Print("ERROR: Failed to allocate memory for DynGen library entry\n");
    return;
  }
  // init pointers such that we can use generic cleanup method
  newLibraryEntry->oscString = nullptr;
  newLibraryEntry->numParameters = 0;
  newLibraryEntry->parameterNamesRT = nullptr;
  newLibraryEntry->oldCode = nullptr;
  newLibraryEntry->oldParameterNames = nullptr;
  newLibraryEntry->numOldParameterNames = 0;

  newLibraryEntry->hash = args->geti();

  if (const char *codePath = args->gets()) {
    auto codePathLength = strlen(codePath) + 1;
    newLibraryEntry->oscString =
        static_cast<char *>(RTAlloc(inWorld, codePathLength));
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
  newLibraryEntry->parameterNamesRT =
      static_cast<char **>(RTAlloc(inWorld, newLibraryEntry->numParameters));
  for (int i = 0; i < newLibraryEntry->numParameters; i++) {
    if (const char *rawParam = args->gets()) {
      auto paramLength = strlen(rawParam) + 1;
      auto paramName = static_cast<char *>(RTAlloc(inWorld, paramLength));
      if (!paramName) {
        Print("ERROR: Failed to allocate memory for DynGen parameter names\n");
        rtCleanup(inWorld, newLibraryEntry, i-1);
        return;
      }
      std::copy_n(rawParam, paramLength, paramName);
      newLibraryEntry->parameterNamesRT[i] = paramName;
    } else {
      Print("ERROR: Invalid dyngenscript message of parameters\n");
      rtCleanup(inWorld, newLibraryEntry, i-1);
      return;
    }
  }

  auto [completionMsgSize, completionMsg] = getCompletionMsg(args);

  ft->fDoAsynchronousCommand(
    inWorld,
    nullptr,
    nullptr,
    static_cast<void *>(newLibraryEntry),
    isFile ? loadFileToDynGenLibrary : loadScriptToDynGenLibrary,
    swapCode,
    deleteOldCode,
    pluginCmdCallbackCleanup,
    completionMsgSize,
    const_cast<char *>(completionMsg)
  );

}

void Library::rtCleanup(World* inWorld, NewDynGenLibraryEntry* newLibraryEntry, const int numRtParameters) {
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

void Library::dyngenAddFileCallback(
  World *inWorld,
  void *inUserData,
  sc_msg_iter *args,
  void *replyAddr
) {
  buildGenericPayload(inWorld, args, true);
}

void Library::addScriptCallback(
  World *inWorld,
  void *inUserData,
  sc_msg_iter *args,
  void *replyAddr
) {
  buildGenericPayload(inWorld, args, false);
}

bool Library::loadScriptToDynGenLibrary(World *world,
                                               void *rawCallbackData) {
  const auto entry = static_cast<NewDynGenLibraryEntry*>(rawCallbackData);
  const auto codeLength = strlen(entry->oscString) + 1;
  auto *codeBuffer = new char[codeLength];
  std::copy_n(entry->oscString, codeLength, codeBuffer);
  entry->code = codeBuffer;

  entry->parameterNamesNRT = new char *[entry->numParameters];
  for (int i = 0; i < entry->numParameters; i++) {
    auto paramLength = strlen(entry->parameterNamesRT[i]) + 1;
    auto *paramBuffer = new char[paramLength];
    std::copy_n(entry->parameterNamesRT[i], paramLength, paramBuffer);
    entry->parameterNamesNRT[i] = paramBuffer;
  }
  return true;
}

bool Library::loadFileToDynGenLibrary(World *world,
                                             void *rawCallbackData) {
  auto entry = static_cast<NewDynGenLibraryEntry *>(rawCallbackData);

  auto codeFile = std::ifstream(entry->oscString, std::ios::binary);
  if (!codeFile.is_open()) {
    Print("ERROR: Could not open DynGen file at %s\n", entry->oscString);
    return false;
  }

  std::stringstream codeStream;

  codeFile.seekg(0, std::ios::end);
  const std::streamsize codeSize = codeFile.tellg();
  codeFile.seekg(0);

  // add /0
  auto *codeBuffer = new char[codeSize + 1];
  codeFile.read(codeBuffer, codeSize);
  codeBuffer[codeSize] = '\0';

  entry->code = codeBuffer;

  entry->parameterNamesNRT = new char *[entry->numParameters];
  for (int i = 0; i < entry->numParameters; i++) {
    auto paramLength = strlen(entry->parameterNamesRT[i]) + 1;
    auto *paramBuffer = new char[paramLength];
    std::copy_n(entry->parameterNamesRT[i], paramLength, paramBuffer);
    entry->parameterNamesNRT[i] = paramBuffer;
  }

  // continue to next stage
  return true;
}

bool Library::swapCode(World *world, void *rawCallbackData) {
  const auto entry = static_cast<NewDynGenLibraryEntry *>(rawCallbackData);

  CodeLibrary *node = gLibrary;
  while (node && node->id != entry->hash) {
    node = node->next;
  }

  if (!node) {
    auto *newNode = static_cast<CodeLibrary *>(RTAlloc(world, sizeof(CodeLibrary)));
    if (!newNode) {
      Print("ERROR: Failed to allocate memory for new code library\n");
      return true;
    }
    newNode->next = gLibrary;
    newNode->id = entry->hash;
    newNode->dynGen = nullptr;
    newNode->code = entry->code;
    newNode->numParameters = entry->numParameters;
    newNode->parameters = entry->parameterNamesNRT;
    newNode->shouldBeFreed = false;
    gLibrary = newNode;
  } else {
    // swap code
    entry->oldCode = node->code;
    entry->numOldParameterNames = node->numParameters;
    entry->oldParameterNames = node->parameters;

    node->code = entry->code;
    node->numParameters = entry->numParameters;
    node->parameters = entry->parameterNamesNRT;

    auto dynGen = node->dynGen;
    while (dynGen != nullptr) {
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
      dynGen->updateCode(entry->code, entry->parameterNamesNRT);
      dynGen = dynGen->mNextDynGen;
    }
  }
  return true;
}

bool Library::deleteOldCode(World *world, void *rawCallbackData) {
  auto entry = static_cast<NewDynGenLibraryEntry *>(rawCallbackData);
  delete[] entry->oldCode;
  for (int i = 0; i < entry->numOldParameterNames; i++) {
    delete[] entry->oldParameterNames[i];
  }
  delete[] entry->oldParameterNames;
  return true;
}

void Library::pluginCmdCallbackCleanup(World *world,
                                              void *rawCallbackData) {
  auto callBackData = static_cast<NewDynGenLibraryEntry *>(rawCallbackData);
  for (int i = 0; i < callBackData->numParameters; i++) {
    RTFree(world, callBackData->parameterNamesRT[i]);
  }
  RTFree(world, callBackData->parameterNamesRT);
  RTFree(world, callBackData->oscString);
  RTFree(world, callBackData);
}

void Library::freeScriptCallback(World *inWorld, void *inUserData,
                                 sc_msg_iter *args, void *replyAddr) {
  if (const int hash = args->geti()) {
    auto node = gLibrary;
    CodeLibrary* prevNode = nullptr;
    while (node != nullptr && node->id != hash) {
      prevNode = node;
      node = node->next;
    };
    if (node != nullptr) {
      node->shouldBeFreed = true;
      if (prevNode != nullptr) {
        prevNode->next = node->next;
      } else {
        gLibrary = node->next;
      }
      if (node->dynGen == nullptr) {
        RTFree(inWorld, node);
      }
    } else {
      Print("ERROR: Could not find and free node with hash %d\n", hash);
    }
  } else {
    Print("ERROR: Invalid free DynGen script message\n");
  }
}

std::pair<int, const char *> Library::getCompletionMsg(sc_msg_iter *args) {
  auto const completionMsgSize = static_cast<int>(args->getbsize());
  const char *completionMsg = nullptr;
  if (completionMsgSize > 0) {
    auto *readPos = args->rdpos;
    // point to the buf data of the completion msg - args->getb
    // would make a copy which we do not want since
    // `fDoAsynchronousCommand` already copies the buffer
    completionMsg = readPos + sizeof(int32_t);
    args->skipb();
  }
  return {completionMsgSize, completionMsg};
}
