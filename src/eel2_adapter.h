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
    static EEL_F eelBufSampleRate(void* opaque, EEL_F* bufNum);
    static EEL_F eelBufFrames(void* opaque, EEL_F* bufNum);
    static EEL_F eelBufChannels(void* opaque, EEL_F* bufNum);

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

    void process(float** inBuf, float** outBuf, Wire** parameterPairs, int numSamples) {
        double* newParamValues = nullptr;
        if (mNumParameters > 0) {
            // copy new parameter values to the stack. Let's do this for *all* parameters, not only
            // for control-rate parameters, because we might need them in the @init and @block sections.
            newParamValues = static_cast<double*>(alloca(mNumParameters * sizeof(double)));
            for (int i = 0; i < mNumParameters; ++i) {
                // Parameter automations come as index-value pairs, so we only take every second odd element.
                Wire* wire = parameterPairs[i * 2 + 1];
                newParamValues[i] = static_cast<double>(wire->mBuffer[0]);
            }
        }

        // update "blockNum" variable
        *mBlockNum = static_cast<double>(mBlockCounter);

        if (mBlockCounter == 0) {
            // First block: initialize script parameters and the parameter cache!
            // This is necessary so that control-rate parameters really start with their original value.
            // The parameter cache itself is used in the @sample section to compare the new (control-rate)
            // parameter value with the previous one. If the value has changed, we need to interpolate;
            // otherwise we keep the script parameter at its previous value.
            for (int i = 0; i < mNumParameters; ++i) {
                if (double* param = mParameters[i]) {
                    *param = newParamValues[i];
                }
            }
            std::copy_n(newParamValues, mNumParameters, mPrevParamValues.get());

            if (mInitCode) {
                // initialize in0, in1, etc. variables to first input sample
                for (int inChannel = 0; inChannel < mNumInputChannels; inChannel++) {
                    *mInputs[inChannel] = static_cast<double>(inBuf[inChannel][0]);
                }

                NSEEL_code_execute(mInitCode);
            }
        }

        double* prevParamValues = nullptr;
        if (mNumParameters > 0) {
            // copy previous parameter values on the stack so they are not reloaded from memory.
            // IMPORTANT: do this *after* we have initialized the cache on the first block!
            prevParamValues = static_cast<double*>(alloca(mNumParameters * sizeof(double)));
            std::copy_n(mPrevParamValues.get(), mNumParameters, prevParamValues);
        }

        if (mBlockCode) {
            // update in0, in1, etc. variables to first input sample
            for (int inChannel = 0; inChannel < mNumInputChannels; inChannel++) {
                *mInputs[inChannel] = static_cast<double>(inBuf[inChannel][0]);
            }

            // update parameters, but do *not* update the cache!
            // NOTE: the behavior of control-rate parameters slightly differs
            // between the @block section and the @sample section:
            // The @block section always shows the new value whereas the @sample
            // section starts with the *previous* value because of the interpolation.
            // This shouldn't be a problem because you would use the parameter in
            // either the @block section *or* the @sample section, but not in both.
            // Sometimes it is even necessary to capture the parameter in the @block
            // section to avoid any interpolation. A good example are buffer numbers!
            for (int i = 0; i < mNumParameters; ++i) {
                if (double* param = mParameters[i]) {
                    *param = newParamValues[i];
                }
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
                    Wire* wire = parameterPairs[paramNum * 2 + 1];
                    if (wire->mCalcRate == calc_FullRate) {
                        // audio rate
                        *param = static_cast<double>(wire->mBuffer[i]);
                    } else if (wire->mCalcRate == calc_BufRate) {
                        // control rate
                        double newValue = newParamValues[paramNum];
                        double curValue = prevParamValues[paramNum];
                        if (newValue != curValue) {
                            // the value has changed -> ramp to new value
                            double slope = (newValue - curValue) * slopeFactor;
                            *param = curValue + slope * i;
                        }
                        // otherwise just keep the previous value
                    }
                    // don't need to do anything for init rate
                }
            }

            NSEEL_code_execute(mSampleCode);

            // copy out0, out1, etc. variables to output buffer.
            for (int outChannel = 0; outChannel < mNumOutputChannels; outChannel++) {
                outBuf[outChannel][i] = static_cast<float>(*mOutputs[outChannel]);
            }
        }

        // update all parameters (including the cache!) to the *exact* new value.
        // Let's do this for *all* parameters because it simplifies the code and avoids
        // some branching. Note that control-rate parameters are only updated in the
        // @sample section if the new value differs from the last value.
        for (int i = 0; i < mNumParameters; i++) {
            if (double* param = mParameters[i]) {
                *param = newParamValues[i];
                mPrevParamValues[i] = newParamValues[i];
            }
        }

        mBlockCounter++;
    }

private:
    NSEEL_VMCTX mEelState = nullptr;
    NSEEL_CODEHANDLE mInitCode = nullptr;
    NSEEL_CODEHANDLE mBlockCode = nullptr;
    NSEEL_CODEHANDLE mSampleCode = nullptr;

    int mNumInputChannels = 0;
    int mNumOutputChannels = 0;
    int mNumParameters = 0; // number of automated parameters

    int mBlockSize = 0;
    double mSampleRate = 0;
    uint64_t mBlockCounter = 0;

    double* mBlockNum = nullptr;
    double* mSampleNum = nullptr;
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
            // NOTE: 'localMaxBufNum' actually holds the max. number of local
            // sound buffers. It is *not* the max. local buffer number!
            // As of SC 3.14, all the Server code gets this wrong...
            if (localBufNum < mParent->localMaxBufNum) {
                mSndBuf = mParent->mLocalSndBufs + localBufNum;
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
