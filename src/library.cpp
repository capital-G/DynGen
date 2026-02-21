// NOTE: include eel2_adapter.h before dyngen.h to prevent collision
// with IN and OUT macros on Windows!
#include "eel2_adapter.h"

#include "library.h"
#include "dyngen.h"
#include "string_utils.h"

#include <charconv>
#include <fstream>
#include <memory>
#include <sstream>

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

/*! @brief try to parse a std::string_view as a floating point number.
 *  'sv' should already be trimmed. Returns an empty optional on failure.
 */
std::optional<double> parseDouble(std::string_view sv) {
    if (sv == "-inf")
        return std::numeric_limits<double>::lowest();
    if (sv == "inf")
        return std::numeric_limits<double>::max();

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
 *  Throws an exception on failure (e.g. syntax error)
 *
 *  Syntax:
 *  <name>: [init=]<init>, [type=]<type>, [min=]<min>, [max=]max, [warp=]<warp>, [step=]<step>, [unit=]<unit>
 *
 *  @note Only <init>, <type>, <min> and <max> are used by the DynGen UGen.
 *  <warp>, <step> and <unit> are only meaningful to Clients, e.g. for representing parameters in GUIs.
 */
ParamSpec parseParameterSpec(std::string_view line) {
    ParamSpec spec;

    // remove leading whitespace
    line = trimLeft(line);
    auto colonPos = line.find(':');
    if (colonPos == std::string_view::npos) {
        // no colon -> only parameter string.
        // ignore everything after the first whitespace character.
        auto end = std::find_if(line.begin(), line.end(), [](auto c) { return isWhitespace(c); });
        auto name = line.substr(0, end - line.begin());
        if (!isAlphaNumeric(name)) {
            throw std::runtime_error("parameter name '" + std::string(name) + "' is not alphanumeric");
        }
        spec.name = std::string("_") += name;
        return spec;
    }
    auto name = line.substr(0, colonPos);
    if (!isAlphaNumeric(name)) {
        throw std::runtime_error("parameter name '" + std::string(name) + "' is not alphanumeric");
    }
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

    auto parseInitValue = [&](std::string_view value) {
        if (auto number = parseDouble(value)) {
            spec.initValue = *number;
        } else {
            std::stringstream ss;
            ss << "parameter '" << spec.name << "': bad init value '" << value << "'";
            throw std::runtime_error(ss.str());
        }
    };

    auto parseType = [&](std::string_view value) {
        if (auto type = getParamTypeFromString(value)) {
            spec.type = *type;
        } else {
            std::stringstream ss;
            ss << "parameter '" << spec.name << "': bad type '" << value << "'";
            throw std::runtime_error(ss.str());
        }
    };

    auto parseMinValue = [&](std::string_view value) {
        if (auto number = parseDouble(value)) {
            spec.minValue = *number;
        } else {
            std::stringstream ss;
            ss << "parameter '" << spec.name << "': bad min. value '" << value << "'";
            throw std::runtime_error(ss.str());
        }
    };

    auto parseMaxValue = [&](std::string_view value) {
        if (auto number = parseDouble(value)) {
            spec.maxValue = *number;
        } else {
            std::stringstream ss;
            ss << "parameter '" << spec.name << "': bad max. value '" << value << "'";
            throw std::runtime_error(ss.str());
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
                    std::stringstream ss;
                    ss << "parameter '" << spec.name << "': unknown key '" << key << "'";
                    throw std::runtime_error(ss.str());
                }

                gotKeywordArg = true;
            } else if (!gotKeywordArg) {
                // positional argument
                // ignore _ placeholder
                if (arg != "_") {
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
                }
                argCount++;
            } else {
                Print("WARNING: parameter '%s': ignore argument '%s' after keyword args\n", std::string(name).c_str(),
                      std::string(arg).c_str());
            }
        },
        ',');

    return spec;
}

/*! @brief try to find a code directive (@<name>) in the given line.
 *  The name must either extend to the end of the line or be followed by at least one whitespace character.
 *  The function returns a CodeDirective followed by the index just past the directive string.
 */
std::pair<CodeDirective, size_t> findCodeDirective(std::string_view line) {
    auto matchName = [&](std::string_view name, size_t start, size_t& end) {
        bool found = line.compare(start, name.size(), name) == 0
            && ((line.size() - start) == name.size() || isWhitespace(line[start + name.size()]));
        if (found)
            end = start + name.size();
        return found;
    };

    for (size_t pos = 0; pos < line.size(); ++pos) {
        auto c = line[pos];
        if (c == '@') {
            size_t end;
            if (matchName("@init", pos, end)) {
                return { CodeDirective::Init, end };
            } else if (matchName("@block", pos, end)) {
                return { CodeDirective::Block, end };
            } else if (matchName("@sample", pos, end)) {
                return { CodeDirective::Sample, end };
            } else if (matchName("@param", pos, end)) {
                return { CodeDirective::Param, end };
            } else {
                // just return the end of line
                return { CodeDirective::Unknown, line.size() };
            }
        } else if (!isWhitespace(c)) {
            break;
        }
    }
    return { CodeDirective::None, 0 };
}

} // namespace

bool DynGenScript::parse(std::string_view script, char** paramNames, int numParams) {
    if (script.empty()) {
        Print("ERROR: empty script\n");
        return false;
    }

    std::string_view initCode;
    std::string_view blockCode;
    std::string_view sampleCode;
    std::vector<ParamSpec> paramSpecs;

    CodeSection currentSection = CodeSection::None;
    size_t currentSectionStart = 0;

    auto closeSection = [&initCode, &blockCode, &sampleCode](CodeSection section, std::string_view code) {
        if (section == CodeSection::Init) {
            if (initCode.empty()) {
                initCode = code;
            } else {
                throw std::runtime_error("duplicate @init section");
            }
        } else if (section == CodeSection::Block) {
            if (blockCode.empty()) {
                blockCode = code;
            } else {
                throw std::runtime_error("duplicate @block section");
            }
        } else if (section == CodeSection::Sample) {
            if (sampleCode.empty()) {
                sampleCode = code;
            } else {
                throw std::runtime_error("duplicate @sample section");
            }
        }
    };

    auto startNewSection = [&](CodeSection newSection, size_t linePos, size_t lineSize) {
        // finish open section (if any)
        if (currentSection != CodeSection::None) {
            size_t currentSize = linePos - currentSectionStart;
            auto code = script.substr(currentSectionStart, currentSize);
            closeSection(currentSection, code);
        }
        // start new section
        currentSection = newSection;
        currentSectionStart = linePos + lineSize + 1; // skip header!
    };

    try {
        forEachLine(script, [&](std::string_view line, size_t linePos) {
            auto [directive, endPos] = findCodeDirective(line);
            if (directive == CodeDirective::Init) {
                startNewSection(CodeSection::Init, linePos, line.size());
            } else if (directive == CodeDirective::Block) {
                startNewSection(CodeSection::Block, linePos, line.size());
            } else if (directive == CodeDirective::Sample) {
                startNewSection(CodeSection::Sample, linePos, line.size());
            } else if (directive != CodeDirective::None) {
                // @ directives are only allowed before code sections
                if (currentSection == CodeSection::None) {
                    if (directive == CodeDirective::Param) {
                        // throws on error!
                        auto spec = parseParameterSpec(line.substr(endPos));
#if DEBUG_PARAM_SPECS
                        Print("@param name: %s, init: %f, type: %s, min: %g, max: %g\n", spec.name.c_str(),
                              spec.initValue, paramTypeString(spec.type), spec.minValue, spec.maxValue);
#endif
                        paramSpecs.push_back(std::move(spec));
                    } else if (directive == CodeDirective::Unknown) {
                        // just skip unknown directive.
                    }
                } else {
                    throw std::runtime_error("cannot have @ directives in code sections");
                }
                // Advance the section start position in case there are no code sections and
                // the remaining script should be used as the @sample section.
                currentSectionStart = linePos + line.size() + 1; // skip line
            }
        });

        if (currentSection != CodeSection::None) {
            // finish open section
            auto code = script.substr(currentSectionStart);
            closeSection(currentSection, code);
        } else {
            // no sections -> the whole script (after @ directives) is used as the @sample section
            sampleCode = script.substr(currentSectionStart);
        }
    } catch (const std::exception& e) {
        Print("ERROR: %s\n", e.what());
        return false;
    }

    if (sampleCode.empty()) {
        Print("ERROR: DynGen script requires a @sample section!\n");
        return false;
    }

    // do not compile and evaluate empty sections.
    if (!isWhitespace(initCode))
        mInit = initCode;
    if (!isWhitespace(blockCode))
        mBlock = blockCode;
    if (!isWhitespace(sampleCode))
        mSample = sampleCode;

    addParameters(paramSpecs, paramNames, numParams);

#if DEBUG_CODE_SECTIONS
    Print("Code sections:\n");
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
    Print("---------------\n");
#endif

    return true;
}

bool DynGenScript::tryCompile() {
    EEL2Adapter state(0, 0, 0, 0, nullptr, nullptr);
    return state.init(*this, nullptr, 0);
}

/*! @brief add the given parameter names to the DynGen script. */
void DynGenScript::addParameters(const std::vector<ParamSpec>& specs, char** paramNames, int numParams) {
#if DEBUG_SCRIPT_PARAMS
    Print("Parameters:\n");
    if (numParams == 0) {
        Print("[none]\n");
    }
#endif
    // try to find parameter names in declared parameter specs. If not found, use default specs.
    for (int i = 0; i < numParams; ++i) {
        auto name = paramNames[i];
        auto it = std::find_if(specs.begin(), specs.end(), [name](const auto& spec) { return spec.name == name; });
        if (it != specs.end()) {
            mParameters.push_back(*it);
        } else {
            ParamSpec spec;
            spec.name = name;
            mParameters.push_back(std::move(spec));
        }
#if DEBUG_SCRIPT_PARAMS
        auto& p = mParameters.back();
        Print("  #%d: name: %s, init: %f, type: %s, min: %g, max: %g\n", i, p.name.c_str(), p.initValue,
              paramTypeString(p.type), p.minValue, p.maxValue);
#endif
    }
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
