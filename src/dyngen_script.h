#pragma once

#include <limits>
#include <optional>
#include <string>
#include <vector>

#ifndef DEBUG_CODE_SECTIONS
#    define DEBUG_CODE_SECTIONS 0
#endif

#ifndef DEBUG_PARAM_SPECS
#    define DEBUG_PARAM_SPECS 0
#endif

#ifndef DEBUG_SCRIPT_PARAMS
#    define DEBUG_SCRIPT_PARAMS 0
#endif

enum class CodeDirective { None, Param, Init, Block, Sample, Unknown };

enum class CodeSection { None, Init, Block, Sample };

enum class ParamType { Linear, Step, Trigger, Const };

std::optional<ParamType> getParamTypeFromString(std::string_view sv);

const char* paramTypeString(ParamType type);

struct ParamSpec {
    std::string name;
    ParamType type = ParamType::Linear;
    double initValue = 0.0;
    double minValue = std::numeric_limits<double>::lowest();
    double maxValue = std::numeric_limits<double>::max();
};

/*! @class DynGenScript
 *  @brief contains the code sections of an EEL2 script
 *  plus a list of exposed parameter names.
 */
class DynGenScript {
public:
    /*! @brief Splits the DynGen scripts into its sections.
     *  non rt safe! */
    bool parse(std::string_view script, char** paramNames, int numParams);
    /*! @brief Try to compile the code sections; print error on failure.
     *  non rt safe! */
    bool tryCompile();

    void setupParameters();

    std::string mInit;
    std::string mBlock;
    std::string mSample;

    /*! @brief parameters which need to be exposed - referenced by the integer
     *  position within the array
     */
    std::vector<ParamSpec> mParameters;

private:
    void addParameters(const std::vector<ParamSpec>& specs, char** paramNames, int numParams);
};
