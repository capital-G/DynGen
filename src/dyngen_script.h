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

/*! @brief The different parameter types */
enum class ParamType {
    /*! If the parameter input is control-rate, it is converted to audio-rate
     *  with linear interpolation, just like the K2A.ar UGen. This is the default.
     */
    Linear,
    /*! The parameter is never interpolated. Control-rate inputs are converted
     *  to audio-rate with zero-order hold. This is important for certain kinds of
     *  parameters, such as buffer numbers or channel indices.
     */
    Step,
    /*! The parameter is a SC-style trigger input. DynGen automatically converts
     *  (stateful) SC-style triggers to (stateless) single-sample trigger signals.
     */
    Trigger,
    /*! The parameter is read only once at the very beginning and is guaranteed
     *  not to change after that.
     */
    Const
};

std::optional<ParamType> getParamTypeFromString(std::string_view sv);

const char* paramTypeString(ParamType type);

struct ParamSpec {
    std::string name;
    ParamType type = ParamType::Linear;
    double initValue = 0.0;
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
