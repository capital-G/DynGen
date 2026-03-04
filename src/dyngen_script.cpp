// NOTE: include eel2_adapter.h before SC_InterfaceTable.h to prevent collision
// with IN and OUT macros on Windows!
#include "eel2_adapter.h"
#include <SC_InterfaceTable.h>

#include "dyngen_script.h"
#include "string_utils.h"

#include <cassert>
#include <charconv>
#include <sstream>

//-------------------- ParamType -------------------//

std::optional<ParamType> getParamTypeFromString(std::string_view string) {
    if (string == "lin") {
        return ParamType::Linear;
    } else if (string == "step") {
        return ParamType::Step;
    } else if (string == "trig") {
        return ParamType::Trigger;
    } else if (string == "const") {
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
    assert(false);
    return "?";
}

//-------------------- helper functions -------------------//

namespace {

/*! @brief try to parse a std::string_view as a floating point number.
 *  'sv' should already be trimmed. Returns an empty optional on failure.
 */
std::optional<double> parseDouble(std::string_view string) {
    if (string == "-inf")
        return std::numeric_limits<double>::lowest();
    if (string == "inf")
        return std::numeric_limits<double>::max();

#if defined(__APPLE__)
    // Apple Clang does not ship std::from_chars for doubles. This sucks...
    try {
        return std::stod(std::string(string));
    } catch (...) { return std::nullopt; }
#else
    double value;
    auto [ptr, err] = std::from_chars(string.data(), string.data() + string.size(), value);
    if (err == std::errc {}) {
        return value;
    } else {
        return std::nullopt;
    }
#endif
}

void parseInitValue(ParamSpec& spec, std::string_view value) {
    if (auto number = parseDouble(value)) {
        spec.initValue = *number;
    } else {
        std::stringstream stream;
        stream << "parameter '" << spec.name << "': bad init value '" << value << "'";
        throw std::runtime_error(stream.str());
    }
}

void parseType(ParamSpec& spec, std::string_view value) {
    if (auto type = getParamTypeFromString(value)) {
        spec.type = *type;
    } else {
        std::stringstream stream;
        stream << "parameter '" << spec.name << "': bad type '" << value << "'";
        throw std::runtime_error(stream.str());
    }
}

/*! @brief try to parse a line as a parameter declaration.
 *  'line' must contain non-whitespace characters and must not be a comment.
 *  Throws an exception on failure (e.g. syntax error)
 *
 *  Syntax:
 *  <name>: [init=]<init>, [type=]<type>
 *
 *  More properties might be added in the future, e.g. <min>, <max>, <warp>, <step>, <unit>, etc.
 *  For example, these could be used by Clients for GUI representations (e.g. ControlSpec in SC)
 */
ParamSpec parseParameterSpec(std::string_view line) {
    ParamSpec spec;

    // remove leading whitespace
    line = trimLeft(line);
    auto nameEndPos = line.find(':');
    size_t argStartPos = 0;
    if (nameEndPos != std::string_view::npos) {
        // skip colon
        argStartPos = nameEndPos + 1;
    } else {
        // no colon -> only parameter string
        // ignore everything after the first whitespace character.
        auto end = std::find_if(line.begin(), line.end(), [](auto c) { return isWhitespace(c); });
        nameEndPos = end - line.begin();
        argStartPos = nameEndPos;
    }
    auto name = line.substr(0, nameEndPos);
    if (!isAlphaNumeric(name)) {
        throw std::runtime_error("parameter name '" + std::string(name) + "' is not alphanumeric");
    }
    spec.name = std::string("_") += name;

    // get arguments without surrounding whitespace
    line = trim(line.substr(argStartPos));
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
                    parseInitValue(spec, value);
                } else if (key == "type") {
                    parseType(spec, value);
                } else {
                    std::stringstream stream;
                    stream << "parameter '" << spec.name << "': unknown key '" << key << "'";
                    throw std::runtime_error(stream.str());
                }

                gotKeywordArg = true;
            } else if (!gotKeywordArg) {
                // positional argument
                // ignore _ placeholder
                if (arg != "_") {
                    if (argCount == 0) {
                        // init value
                        parseInitValue(spec, arg);
                    } else if (argCount == 1) {
                        // type
                        parseType(spec, arg);
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

//-------------------- DynGenScript -------------------//

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
