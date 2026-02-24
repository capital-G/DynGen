// NOTE: include eel2_adapter.h before dyngen.h to prevent collision
// with IN and OUT macros on Windows!
#include "eel2_adapter.h"

#include "library.h"
#include "dyngen.h"
#include "string_utils.h"

#include <charconv>
#include <fstream>
#include <memory>

std::optional<ParamType> getParamTypeFromString(std::string_view sv) {
    if (sv == "lin") {
        return ParamType::Linear;
    } else if (sv == "step") {
        return ParamType::Step;
    } else if (sv == "trig") {
        return ParamType::Trigger;
    } else if (sv == "const") {
        return ParamType::Const;
    } else {
        return std::nullopt;
    }
}

const char* paramTypeString(ParamType type) {
    switch (type) {
    case ParamType::Linear:
        return "lin";
    case ParamType::Step:
        return "step";
    case ParamType::Trigger:
        return "trig";
    case ParamType::Const:
        return "const";
    }
    return "?";
}

//-------------------- DynGenScript -------------------//

namespace {

/*! @brief try to find a code section name in the given line.
 *  The name must either extend to the end of the line or be followed
 *  by at least one whitespace character.
 */
CodeSection findCodeSection(std::string_view line) {
    auto matchName = [&](std::string_view name, size_t start) {
        return line.compare(start, name.size(), name) == 0
            && ((line.size() - start) == name.size() || isWhitespace(line[start + name.size()]));
    };

    for (size_t pos = 0; pos < line.size(); ++pos) {
        auto c = line[pos];
        if (c == '@') {
            if (matchName("@init", pos)) {
                return CodeSection::Init;
            } else if (matchName("@block", pos)) {
                return CodeSection::Block;
            } else if (matchName("@sample", pos)) {
                return CodeSection::Sample;
            } else if (matchName("@param", pos)) {
                return CodeSection::Param;
            }
        } else if (!isWhitespace(c)) {
            break;
        }
    }
    return CodeSection::None;
}

} // namespace

bool DynGenScript::parse(std::string_view script, char** paramNames, int numParams) {
    CodeSection currentSection = CodeSection::None;
    size_t currentSectionStart = 0;

    std::string_view initCode;
    std::string_view blockCode;
    std::string_view sampleCode;
    std::string_view paramCode;

    forEachLine(script, [&](std::string_view line, size_t linePos) {
        auto newSection = findCodeSection(line);
        if (newSection != CodeSection::None) {
            // finish current section
            size_t currentSize = linePos - currentSectionStart;
            if (currentSection == CodeSection::Init) {
                initCode = script.substr(currentSectionStart, currentSize);
            } else if (currentSection == CodeSection::Block) {
                blockCode = script.substr(currentSectionStart, currentSize);
            } else if (currentSection == CodeSection::Sample) {
                sampleCode = script.substr(currentSectionStart, currentSize);
            } else if (currentSection == CodeSection::Param) {
                paramCode = script.substr(currentSectionStart, currentSize);
            }
            // start new section
            currentSection = newSection;
            currentSectionStart = linePos + line.size() + 1; // skip header!
        }
    });

    // finish last section
    if (currentSection == CodeSection::Init) {
        initCode = script.substr(currentSectionStart);
    } else if (currentSection == CodeSection::Block) {
        blockCode = script.substr(currentSectionStart);
    } else if (currentSection == CodeSection::Sample) {
        sampleCode = script.substr(currentSectionStart);
    } else if (currentSection == CodeSection::Param) {
        paramCode = script.substr(currentSectionStart);
    } else {
        // no sections -> the whole script is used as the @sample section
        sampleCode = script;
    }

    if (sampleCode.empty()) {
        Print("ERROR: DynGen script requires a @sample section!\n");
        return false;
    }

    if (!parseParameters(paramCode, paramNames, numParams)) {
        return false;
    }

    mInit = initCode;
    mBlock = blockCode;
    mSample = sampleCode;

#if 0
    if (!paramCode.empty()) {
        Print("--- @param ---\n");
        Print("%s\n", std::string(paramCode).c_str());
    }
    if (!mInit.empty()) {
        Print("--- @init ---\n");
        Print("%s\n", mInit.c_str());
    }
    if (!mBlock.empty()) {
        Print("--- @block ---\n");
        Print("%s\n", mBlock.c_str());
    }
    if (!mSample.empty()) {
        Print("--- @sample ---\n");
        Print("%s\n", mSample.c_str());
    }
#endif

    return true;
}

bool DynGenScript::tryCompile() {
    EEL2Adapter state(0, 0, 0, 0, nullptr, nullptr);
    return state.init(*this, nullptr, 0);
}

namespace {

/*! @brief try to parse a std::string_view as a floating point number.
 *  'sv' should already be trimmed. Returns an empty optional on failure.
 */
std::optional<double> parseDouble(std::string_view sv) {
#if defined(__APPLE__)
    // Apple Clang does not ship std::from_chars for doubles. This sucks...
    try {
        return std::stod(std::string(sv));
    } catch (...) { return std::nullopt; }
#else
    double value;
    auto [ptr, err] = std::from_chars(sv.data(), sv.data() + sv.size(), value);
    if (err == std::errc {}) {
        return value;
    } else {
        return std::nullopt;
    }
#endif
}

/*! @brief try to parse a line as a parameter declaration.
 *  'line' must contain non-whitespace characters and must not be a comment.
 *  Returns an empty optional on failure (e.g. syntax error)
 *
 *  Syntax:
 *  <name>: [init=]<init>, [type=]<type>, [min=]<min>, [max=]max, [warp=]<warp>, [step=]<step>, [unit=]<unit>
 *
 *  @note Only <init>, <type>, <min> and <max> are used by the DynGen UGen.
 *  <warp>, <step> and <unit> are only meaningful to Clients, e.g. for representing parameters in GUIs.
 */
std::optional<ParamSpec> parseParameter(std::string_view line) {
    ParamSpec spec;

    // remove leading whitespace
    line = trimLeft(line);
    auto colonPos = line.find(':');
    if (colonPos == std::string_view::npos) {
        // no colon -> only parameter string.
        // ignore everything after the first whitespace character.
        auto end = std::find_if(line.begin(), line.end(), [](auto c) { return isWhitespace(c); });
        auto name = line.substr(0, end - line.begin());
        spec.name = std::string("_") += name;
        return spec;
    }
    // name: ignore colon and prepend underscore.
    auto name = line.substr(0, colonPos);
    spec.name = std::string("_") += name;
    // get arguments without surrounding whitespace
    line = trim(line.substr(colonPos + 1));
    // remove trailing commas
    while (!line.empty() && line.back() == ',') {
        line.remove_suffix(1);
    }
    if (line.empty()) {
        // no arguments
        return spec;
    }

    // iterate over comma separated arguments
    int argCount = 0;
    bool gotKeywordArg = false;
    bool parseError = false;

    auto parseInitValue = [&](std::string_view value) {
        if (auto number = parseDouble(value)) {
            spec.initValue = *number;
        } else {
            Print("ERROR: parameter '%s': bad init value '%s'\n", std::string(name).c_str(),
                  std::string(value).c_str());
            parseError = true;
        }
    };

    auto parseType = [&](std::string_view value) {
        if (auto type = getParamTypeFromString(value)) {
            spec.type = *type;
        } else {
            Print("ERROR: parameter '%s': bad type '%s'\n", std::string(name).c_str(), std::string(value).c_str());
            parseError = true;
        }
    };

    auto parseMinValue = [&](std::string_view value) {
        if (auto number = parseDouble(value)) {
            spec.minValue = *number;
        } else {
            Print("ERROR: parameter '%s': bad min. value '%s'\n", std::string(name).c_str(),
                  std::string(value).c_str());
            parseError = true;
        }
    };

    auto parseMaxValue = [&](std::string_view value) {
        if (auto number = parseDouble(value)) {
            spec.maxValue = *number;
        } else {
            Print("ERROR: parameter '%s': bad max. value '%s'\n", std::string(name).c_str(),
                  std::string(value).c_str());
            parseError = true;
        }
    };

    forEachLine(
        line,
        [&](std::string_view arg, size_t) {
            // remove all surrounding whitespace!
            arg = trim(arg);

            if (auto eqPos = arg.find('='); eqPos != std::string_view::npos) {
                // keyword argument
                auto key = trim(arg.substr(0, eqPos));
                auto value = trim(arg.substr(eqPos + 1));
                if (key == "init") {
                    parseInitValue(value);
                } else if (key == "type") {
                    parseType(value);
                } else if (key == "min") {
                    parseMinValue(value);
                } else if (key == "max") {
                    parseMaxValue(value);
                } else if (key == "warp" || key == "step" || key == "unit") {
                    // silently ignore
                } else {
                    Print("ERROR: parameter '%s': unknown key '%s'\n", std::string(name).c_str(),
                          std::string(key).c_str());
                    parseError = true;
                }

                gotKeywordArg = true;
            } else if (!gotKeywordArg) {
                // positional argument
                if (argCount == 0) {
                    // init value
                    parseInitValue(arg);
                } else if (argCount == 1) {
                    // type
                    parseType(arg);
                } else if (argCount == 2) {
                    // min. value
                    parseMinValue(arg);
                } else if (argCount == 3) {
                    // max. value
                    parseMaxValue(arg);
                } else if (argCount < 7) {
                    // silently ignore "warp", "step" and "unit" arguments.
                } else {
                    Print("WARNING: parameter '%s': ignore extra argument '%s'\n", std::string(name).c_str(),
                          std::string(arg).c_str());
                }
                argCount++;
            } else {
                Print("WARNING: parameter '%s': ignore argument '%s' after keyword args\n", std::string(name).c_str(),
                      std::string(arg).c_str());
            }
        },
        ',');

    if (!parseError) {
        return spec;
    } else {
        return std::nullopt;
    }
}

/*! @brief check if a line is a comment or only consists of whitespace */
std::optional<std::string_view> checkLine(std::string_view line) {
    line = trimLeft(line);
    if (line.empty() || (line[0] == '/' && line[1] == '/') || (line[0] == '/' && line[1] == '*')) {
        return std::nullopt;
    }
    return line;
}

} // namespace

bool DynGenScript::parseParameters(std::string_view text, char** paramNames, int numParams) {
    std::vector<ParamSpec> params;
    bool ok = true;
    // parse parameter section
    forEachLine(text, [&](std::string_view line, size_t) {
        if (auto result = checkLine(line)) {
            if (auto param = parseParameter(*result)) {
#if 0
                Print("name: %s, init: %f, type: %s, min: %g, max: %g\n", param->name.c_str(), param->initValue,
                      paramTypeString(param->type), param->minValue, param->maxValue);
#endif
                params.push_back(std::move(*param));
            } else {
                ok = false;
            }
        }
    });

    if (ok) {
        // try to find parameter names in declared parameters. If not found, use default specs.
        for (int i = 0; i < numParams; ++i) {
            auto name = paramNames[i];
            auto it =
                std::find_if(params.begin(), params.end(), [name](const auto& spec) { return spec.name == name; });
            if (it != params.end()) {
                mParameters.push_back(*it);
            } else {
                ParamSpec spec;
                spec.name = name;
                mParameters.push_back(std::move(spec));
            }
#if 0
            auto& p = mParameters.back();
            Print("#%d: name: %s, init: %f, type: %s, min: %g, max: %g\n", i, p.name.c_str(), p.initValue,
                  paramTypeString(p.type), p.minValue, p.maxValue);
#endif
        }
    }

    return ok;
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

CodeLibrary* Library::getCode(World* world, int codeID) {
    auto code = findCode(codeID);
    if (!code) {
        // create new code node
        code = static_cast<CodeLibrary*>(RTAlloc(world, sizeof(CodeLibrary)));
        if (!code) {
            return nullptr; // out of memory
        }
        code->mNext = gLibrary;
        code->mWorld = world;
        code->mID = codeID;
        code->mDynGen = nullptr;
        code->mScript = nullptr;
        code->mShouldBeFreed = false;
        gLibrary = code;
    }
    return code;
}

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

    if (script != nullptr) {
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

    if (!script->parse(code, newLibraryEntry->parameterNamesRT, newLibraryEntry->numParameters)) {
        return false;
    }

    // already try to compile before creating/updating any DynGen instances.
    if (!script->tryCompile()) {
        return false;
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
