#pragma once

#include "eel2/ns-eel.h"

#include <SC_Alloca.h>
#include <SC_Graph.h>
#include <SC_Unit.h>
#include <SC_Wire.h>
#include <SC_World.h>

#include "library.h"

#include <algorithm>
#include <memory>

/*! @class EEL2Adapter
 *  @brief wraps a EEL2 VM and injects special functions and variables
 *  for the usage within SuperCollider.
 */
class EEL2Adapter {
public:
    /*! @brief adds some optional modules and registers our own functions.
     *  This is called once when the plugin is loaded.
     */
    static void setup();

    EEL2Adapter(uint32 numInputChannels, uint32 numOutputChannels, int sampleRate, int blockSize, World* world,
                Unit* unit);

    ~EEL2Adapter();

    /*! @brief returns true if vm has been compiled successfully */
    bool init(const DynGenScript& script, const int* parameterIndices, int numParamIndices);

    static EEL_F eelBufRead(void* opaque, INT_PTR numParams, EEL_F** params);
    static EEL_F eelBufReadL(void* opaque, INT_PTR numParams, EEL_F** params);
    static EEL_F eelBufReadC(void* opaque, INT_PTR numParams, EEL_F** params);
    static EEL_F eelBufWrite(void* opaque, INT_PTR numParams, EEL_F** param);
    static EEL_F eelBufSampleRate(void* opaque, EEL_F* bufNum);
    static EEL_F eelBufFrames(void* opaque, EEL_F* bufNum);
    static EEL_F eelBufChannels(void* opaque, EEL_F* bufNum);

    static EEL_F eelSetDone(void* opaque, EEL_F* done);
    static EEL_F eelDoneAction(void* opaque, EEL_F* doneAction);

    static EEL_F eelIn(void* opaque, EEL_F* channel);
    static EEL_F* eelOut(void* opaque, EEL_F* channel);

    static EEL_F eelClip(void*, INT_PTR numParams, EEL_F** params);
    static EEL_F eelWrap(void*, INT_PTR numParams, EEL_F** param);
    static EEL_F eelFold(void*, INT_PTR numParams, EEL_F** params);
    static EEL_F eelMod(void*, EEL_F* in, EEL_F* hi);
    static EEL_F eelLininterp(void*, EEL_F* x, EEL_F* a, EEL_F* b);
    static EEL_F eelCubicinterp(void*, INT_PTR numParams, EEL_F** params);

    static EEL_F eelDelta(void*, EEL_F* state, EEL_F* signal);
    static EEL_F eelHistory(void*, EEL_F* state, EEL_F* signal);
    static EEL_F eelLatch(void*, EEL_F* state, EEL_F* signal, EEL_F* trigger);

    static EEL_F eelPrint(void*, INT_PTR numParams, EEL_F** params);
    static EEL_F_PTR eelPrintMem(EEL_F** blocks, EEL_F* start, EEL_F* length);
    static EEL_F eelPoll(void* opaque, INT_PTR numParams, EEL_F** params);

    void process(float** inBuf, float** outBuf, Wire** parameterPairs, int numSamples) {
        double* newParamValues = nullptr;
        if (mNumParameters > 0) {
            // Copy new parameter values to the stack. Let's do this for *all* parameters, not only
            // for control-rate parameters, because we might need them in the @init and @block sections.
            newParamValues = static_cast<double*>(alloca(mNumParameters * sizeof(double)));
            for (int i = 0; i < mNumParameters; ++i) {
                // Parameter automations come as index-value pairs, so we only take every second odd element.
                Wire* wire = parameterPairs[i * 2 + 1];
                double value = static_cast<double>(wire->mBuffer[0]);
                // Clamp value to the specified range!
                auto& spec = mParamSpecs[i];
                newParamValues[i] = std::clamp(value, spec.minValue, spec.maxValue);
            }
        }

        // update "blockNum" variable
        *mBlockNum = static_cast<double>(mBlockCounter);

        if (mBlockCounter == 0) {
            // First block -> initialize script parameter variables
            //
            // Strictly speaking, "lin", "step" and "trig" parameter variables only have to be set
            // if there is an @init section. However, since we have to set all "const" parameters
            // and also initialize the parameter cache for "lin" parameters, we just go ahead and
            // set all parameter variables.
            //
            // (If a parameter is not set/modulated here, it simply keeps the initial value as
            // defined in the parameter specs.)
            for (int i = 0; i < mNumParameters; ++i) {
                if (double* param = mParameters[i]) {
                    if (mParamSpecs[i].type == ParamType::Trigger) {
                        // Handle "trig" parameter. NOTE: the cache value must remain 0.0, otherwise
                        // the parameter couldn't trigger on the first sample in the @sample section!
                        *param = newParamValues[i] > 0.0 ? 1.0 : 0.0;
                    } else {
                        *param = newParamValues[i];
                        // We must initialize the parameter cache for "lin" parameters so that they
                        // immediately start with the initial value.
                        mPrevParamValues[i] = newParamValues[i];
                    }
                }
            }

            if (mInitCode) {
                // initialize in0, in1, etc. variables to first input sample
                for (int inChannel = 0; inChannel < mNumInputChannels; inChannel++) {
                    *mInputs[inChannel] = static_cast<double>(inBuf[inChannel][0]);
                }
                // init triggers
                for (int i = 0; i < mNumInitTriggers; ++i) {
                    *mParameters[mNumParameters + i] = 1.0;
                }

                NSEEL_code_execute(mInitCode);
            }
        }

        double* prevParamValues = nullptr;
        if (mNumParameters > 0) {
            // Copy previous parameter values on the stack so they are not reloaded from memory.
            // IMPORTANT: do this *after* we have initialized the cache on the first block!
            prevParamValues = static_cast<double*>(alloca(mNumParameters * sizeof(double)));
            std::copy_n(mPrevParamValues.get(), mNumParameters, prevParamValues);
        }

        if (mBlockCode) {
            // update in0, in1, etc. variables to first input sample
            for (int inChannel = 0; inChannel < mNumInputChannels; inChannel++) {
                *mInputs[inChannel] = static_cast<double>(inBuf[inChannel][0]);
            }

            // Update parameters, but do *not* update the cache!
            //
            // NOTE: the behavior of control-rate parameters slightly differs between the @block
            // section and the @sample section:
            // The @block section always shows the new value whereas the @sample section starts with
            // the *previous* value because of the interpolation. This shouldn't be a problem because
            // you would use the parameter in either the @block section *or* the @sample section,
            // but not in both.
            for (int i = 0; i < mNumParameters; ++i) {
                if (double* param = mParameters[i]) {
                    auto& spec = mParamSpecs[i];
                    if (spec.type == ParamType::Trigger) {
                        // This will miss triggers for audio-rate trigger inputs, but it's better
                        // than just setting the value as is. "trig" parameters probably shouldn't
                        // be used in the @block section unless the user is 100% certain that the
                        // parameter is not modulated at audio-rate.
                        if (newParamValues[i] > 0.0 && prevParamValues[i] <= 0.0) {
                            *param = 1.0;
                        } else {
                            *param = 0.0;
                        }
                    } else if (mParamSpecs[i].type != ParamType::Const) {
                        // Do not update "const" parameters!
                        *param = newParamValues[i];
                    }
                }
            }
            // init triggers
            for (int i = 0; i < mNumInitTriggers; ++i) {
                *mParameters[mNumParameters + i] = mBlockCounter == 0 ? 1.0 : 0.0;
            }

            NSEEL_code_execute(mBlockCode);
        }

        double slopeFactor = 1.0 / static_cast<double>(numSamples);

        for (int i = 0; i < numSamples; i++) {
            // update "sampleNum" variable
            *mSampleNum = static_cast<double>(i);

            // copy input samples to in0, in1, etc. variables
            for (int inChannel = 0; inChannel < mNumInputChannels; inChannel++) {
                *mInputs[inChannel] = static_cast<double>(inBuf[inChannel][i]);
            }

            // update automated parameters.
            for (int paramNum = 0; paramNum < mNumParameters; paramNum++) {
                if (double* param = mParameters[paramNum]) {
                    auto& spec = mParamSpecs[paramNum];
                    if (spec.type == ParamType::Const) {
                        // do not update "const" parameters!
                        continue;
                    }

                    Wire* wire = parameterPairs[paramNum * 2 + 1];
                    if (wire->mCalcRate == calc_FullRate) {
                        // 1. audio rate
                        double value = static_cast<double>(wire->mBuffer[i]);
                        if (spec.type == ParamType::Trigger) {
                            // "trig" parameter -> convert SC-style trigger to (stateless)
                            // single-sample trigger signal
                            if (value > 0.0 && prevParamValues[paramNum] <= 0.0) {
                                *param = 1.0;
                            } else {
                                *param = 0.0;
                            }
                            // Update the parameter cache!
                            prevParamValues[paramNum] = value;
                            // 'newParamValues' will be copied *unconditionally* to 'mPrevParamValues'
                            // at the end of the process() function! This makes the update very cheap.
                            // Actually, we'd only have to update our 'newParamValues' entry on the
                            // last sample in the block, but this way we avoid yet another branch.
                            newParamValues[paramNum] = value;
                        } else {
                            // "lin" or "step" parameter -> clamp to specified range
                            *param = std::clamp(value, spec.minValue, spec.maxValue);
                        }
                    } else if (wire->mCalcRate == calc_BufRate) {
                        // 2. control rate
                        if (spec.type == ParamType::Trigger) {
                            // "trig" parameter -> convert SC-style trigger to (stateless) single-sample
                            // trigger signal.
                            // Only check the first sample in the block because the remaining samples
                            // are guaranteed to be zero.
                            if (i == 0 && newParamValues[paramNum] > 0.0 && prevParamValues[paramNum] <= 0.0) {
                                *param = 1.0;
                            } else {
                                *param = 0.0;
                            }
                        } else if (spec.type == ParamType::Linear) {
                            // "lin" parameter -> ramp to new value if it has changed
                            double newValue = newParamValues[paramNum];
                            double prevValue = prevParamValues[paramNum];
                            if (newValue != prevValue) {
                                double slope = (newValue - prevValue) * slopeFactor;
                                *param = prevValue + slope * i;
                            } else {
                                *param = newValue;
                            }
                        } else {
                            // "step" parameter -> just set it as is.
                            // (Actually, we only need to do this for the first sample in the block,
                            // but repeatedly setting the variable might be cheaper than a branch.)
                            *param = newParamValues[paramNum];
                        }
                    } else {
                        // 3. init rate
                        if (spec.type == ParamType::Trigger) {
                            // only check the very first sample
                            if (mSampleCounter == 0 && newParamValues[paramNum] > 0.0) {
                                *param = 1.0;
                            } else {
                                *param = 0.0;
                            }
                        }
                        // We don't need to do anything for "lin" and "step" parameters.
                    }
                }
            }
            // init triggers
            for (int i = 0; i < mNumInitTriggers; ++i) {
                *mParameters[mNumParameters + i] = mSampleCounter == 0 ? 1.0 : 0.0;
            }

            NSEEL_code_execute(mSampleCode);

            // copy out0, out1, etc. variables to output buffer.
            for (int outChannel = 0; outChannel < mNumOutputChannels; outChannel++) {
                outBuf[outChannel][i] = static_cast<float>(*mOutputs[outChannel]);
                // clear the variable so it never contains garbage from previous iterations.
                *mOutputs[outChannel] = 0.0;
            }

            mSampleCounter++;
        }

        // Update the parameter cache. Although the parameter cache is only used by certain parameter
        // types and rates, let's do it for *all* parameters because it's a simply memcpy().
        std::copy_n(newParamValues, mNumParameters, mPrevParamValues.get());

        mBlockCounter++;
    }

private:
    struct Param {
        ParamType type;
        double minValue;
        double maxValue;
    };

    NSEEL_VMCTX mEelState = nullptr;
    NSEEL_CODEHANDLE mInitCode = nullptr;
    NSEEL_CODEHANDLE mBlockCode = nullptr;
    NSEEL_CODEHANDLE mSampleCode = nullptr;

    int mNumInputChannels = 0;
    int mNumOutputChannels = 0;
    int mNumParameters = 0; // number of modulated parameters
    int mNumInitTriggers = 0; // (positive) "trig" parameters that are not modulated

    int mBlockSize = 0;
    double mSampleRate = 0;
    uint64_t mBlockCounter = 0;
    uint64_t mSampleCounter = 0;

    double* mBlockNum = nullptr;
    double* mSampleNum = nullptr;
    std::unique_ptr<double*[]> mInputs;
    std::unique_ptr<double*[]> mOutputs;
    std::unique_ptr<double*[]> mParameters;
    std::unique_ptr<Param[]> mParamSpecs;
    std::unique_ptr<double[]> mPrevParamValues;

    World* mWorld;
    Unit* mUnit;

    /*! @brief cache the latest sndbuf b/c it is likely that we
     * stick to one sndbuf
     */
    SndBuf* mSndBuf = nullptr;
    int mSndBufNum = -1;

    /*! @brief see GET_BUF macro from SC_Unit.h */
    SndBuf* getBuffer(int bufNum) {
        if (bufNum < 0) {
            return nullptr;
        }

        if (bufNum == mSndBufNum) {
            return mSndBuf;
        }

        // invalidate cache
        mSndBuf = nullptr;
        mSndBufNum = bufNum;

        if (bufNum < mWorld->mNumSndBufs) {
            mSndBuf = mWorld->mSndBufs + bufNum;
        } else {
            // looking for a matching localbuf
            int localBufNum = bufNum - mWorld->mNumSndBufs;
            // NOTE: 'localMaxBufNum' actually holds the max. number of local
            // sound buffers. It is *not* the max. local buffer number!
            // As of SC 3.14, all the Server code gets this wrong...
            if (localBufNum < mUnit->mParent->localMaxBufNum) {
                mSndBuf = mUnit->mParent->mLocalSndBufs + localBufNum;
            }
        }

        return mSndBuf;
    }

    /*! @brief Assumes that chan is within bounds and that the buffer is locked */
    static float getSample(const SndBuf* buf, int chan, int frameIndex) {
        if (frameIndex >= 0 && frameIndex < buf->frames) {
            return buf->data[buf->channels * frameIndex + chan];
        } else {
            return 0.f;
        }
    }
};
