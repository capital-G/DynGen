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
                Graph* parent);

    ~EEL2Adapter();

    /*! @brief returns true if vm has been compiled successfully */
    bool init(const DynGenScript& script, const int* parameterIndices, int numParamIndices);

    static EEL_F eelBufRead(void* opaque, INT_PTR numParams, EEL_F** params);
    static EEL_F eelBufReadL(void* opaque, INT_PTR numParams, EEL_F** params);
    static EEL_F eelBufReadC(void* opaque, INT_PTR numParams, EEL_F** params);
    static EEL_F eelBufWrite(void* opaque, INT_PTR numParams, EEL_F** param);

    static EEL_F eelIn(void* opaque, EEL_F* channel);
    static EEL_F* eelOut(void* opaque, EEL_F* channel);

    static EEL_F eelClip(void*, INT_PTR numParams, EEL_F** params);
    static EEL_F eelWrap(void*, INT_PTR numParams, EEL_F** param);
    static EEL_F eelFold(void*, INT_PTR numParams, EEL_F** params);
    static EEL_F eelMod(void*, EEL_F* in, EEL_F* hi);
    static EEL_F eelLininterp(void*, EEL_F* x, EEL_F* a, EEL_F* b);
    static EEL_F eelCubicinterp(void*, INT_PTR numParams, EEL_F** params);

    static EEL_F eelPrint(void*, INT_PTR numParams, EEL_F** params);
    static EEL_F_PTR eelPrintMem(EEL_F** blocks, EEL_F* start, EEL_F* length);

    void process(float** inBuf, float** outBuf, Wire** parameterPairs, int numSamples) {
        if (mFirstBlock) {
            // initialize parameters! We do this even without an @init section because
            // we have to initialize the parameter cache for control-rate parameters.
            for (int i = 0; i < mNumParameters; ++i) {
                // Parameter automations come as index-value pairs, so we only take
                // every second odd element.
                Wire* wire = parameterPairs[i * 2 + 1];
                double value = static_cast<double>(wire->mBuffer[0]);
                *mParameters[i] = value;
                mPrevParamValues[i] = value;
            }

            if (mInitCode) {
                NSEEL_code_execute(mInitCode);
            }

            mFirstBlock = false;
        }

        if (mBlockCode) {
            // update parameters, but do not update the cache!
            // NOTE: the behavior of control-rate parameters slightly differs
            // between the @block section and the @sample section:
            // The @block section always shows the new value whereas the @sample
            // section starts with the *previous* value because of the interpolation.
            // This shouldn't be a problem because the very reason for using a
            // parameter in the @block section is to avoid repeated calculations
            // in the @sample section.
            for (int i = 0; i < mNumParameters; ++i) {
                Wire* wire = parameterPairs[i * 2 + 1];
                *mParameters[i] = static_cast<double>(wire->mBuffer[0]);
            }

            NSEEL_code_execute(mBlockCode);
        }

        double slopeFactor = 1.0 / static_cast<double>(numSamples);
        double* prevParamValues = nullptr;
        if (mNumParameters > 0) {
            // copy previous parameter values on the stack so we don't have to reload them from memory.
            prevParamValues = static_cast<double*>(alloca(mNumParameters * sizeof(double)));
            std::copy_n(mPrevParamValues.get(), mNumParameters, prevParamValues);
        }

        for (int i = 0; i < numSamples; i++) {
            // copy input buffer to vm
            for (int inChannel = 0; inChannel < mNumInputChannels; inChannel++) {
                *mInputs[inChannel] = static_cast<double>(inBuf[inChannel][i]);
            }

            // update automated parameters
            for (int paramNum = 0; paramNum < mNumParameters; paramNum++) {
                if (mParameters[paramNum] != nullptr) {
                    Wire* wire = parameterPairs[paramNum * 2 + 1];
                    double* param = mParameters[paramNum];
                    if (wire->mCalcRate == calc_FullRate) {
                        // audio rate
                        *param = static_cast<double>(wire->mBuffer[i]);
                    } else if (wire->mCalcRate == calc_BufRate) {
                        // control rate
                        double* param = mParameters[paramNum];
                        double newValue = static_cast<double>(wire->mBuffer[0]);
                        double curValue = prevParamValues[paramNum];
                        if (newValue != curValue) {
                            // ramp to new value
                            double slope = (newValue - curValue) * slopeFactor;
                            *param = curValue + slope * i;
                            if (i == (numSamples - 1)) {
                                // set to the exact new value!
                                prevParamValues[paramNum] = newValue;
                            }
                        } else {
                            // set to current value, otherwise a ramp would not finish!
                            *param = curValue;
                        }
                    }
                    // don't need to do anything for init rate
                }
            }

            // update parameter value cache
            if (prevParamValues != nullptr) {
                std::copy_n(prevParamValues, mNumParameters, mPrevParamValues.get());
            }

            NSEEL_code_execute(mSampleCode);

            // read output buffer from vm
            for (int outChannel = 0; outChannel < mNumOutputChannels; outChannel++) {
                outBuf[outChannel][i] = static_cast<float>(*mOutputs[outChannel]);
            }
        }
    }

private:
    NSEEL_VMCTX mEelState = nullptr;
    NSEEL_CODEHANDLE mInitCode = nullptr;
    NSEEL_CODEHANDLE mBlockCode = nullptr;
    ;
    NSEEL_CODEHANDLE mSampleCode = nullptr;

    int mNumInputChannels = 0;
    int mNumOutputChannels = 0;
    int mNumParameters = 0; // number of automated parameters

    double mSampleRate = 0;
    int mBlockSize = 0;
    bool mFirstBlock = true;

    std::unique_ptr<double*[]> mInputs;
    std::unique_ptr<double*[]> mOutputs;
    std::unique_ptr<double*[]> mParameters;
    std::unique_ptr<double[]> mPrevParamValues;

    World* mWorld;
    Graph* mParent;

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
            if (localBufNum <= mParent->localBufNum) {
                mSndBuf = mParent->mLocalSndBufs + localBufNum;
            }
        }

        return mSndBuf;
    }

    /*! @brief Assumes that chan is within bounds */
    static float getSample(const SndBuf* buf, int chan, int sampleNum) {
        LOCK_SNDBUF_SHARED(buf);
        sampleNum = std::clamp<int>(sampleNum, 0, buf->samples - 1);
        return buf->data[buf->channels * sampleNum + chan];
    }
};
