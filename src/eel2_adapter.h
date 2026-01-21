#pragma once

#include "eel2/ns-eel.h"
#include "ns-eel-int.h"

#include <SC_Graph.h>
#include <SC_Unit.h>
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
    EEL2Adapter(uint32 numInputChannels, uint32 numOutputChannels, int sampleRate, int blockSize, World* world,
                Graph* parent);

    ~EEL2Adapter();

    /*! @brief returns true if vm has been compiled successfully */
    bool init(const DynGenScript& script, const int* parameterIndices, int numParamIndices);

    static EEL_F eelReadBuf(void* opaque, INT_PTR numParams, EEL_F** params);
    static EEL_F eelReadBufL(void* opaque, INT_PTR numParams, EEL_F** params);
    static EEL_F eelReadBufC(void* opaque, INT_PTR numParams, EEL_F** params);
    static EEL_F eelWriteBuf(void* opaque, INT_PTR numParams, EEL_F** param);
    static EEL_F* in(void* opaque, EEL_F* channel);
    static EEL_F* out(void* opaque, EEL_F* channel);

    void process(float** inBuf, float** outBuf, float** parameterPairs, int numSamples) {
        if (mBlockCode) {
            NSEEL_code_execute(mBlockCode);
        }

        for (int i = 0; i < numSamples; i++) {
            // copy input buffer to vm - cast to double!
            for (int inChannel = 0; inChannel < mNumInputChannels; inChannel++) {
                *mInputs[inChannel] = static_cast<double>(inBuf[inChannel][i]);
            }

            // update automated parameters.
            // parameter automations come as index-value pairs, so we only take
            // every second odd element.
            for (int paramNum = 0; paramNum < mNumParameters; paramNum++) {
                if (mParameters[paramNum] != nullptr) {
                    auto paramValue = parameterPairs[paramNum * 2 + 1];
                    *mParameters[paramNum] = static_cast<double>(paramValue[i]);
                }
            }

            NSEEL_code_execute(mSampleCode);

            // read output buffer from vm
            for (int outChannel = 0; outChannel < mNumOutputChannels; outChannel++) {
                outBuf[outChannel][i] = static_cast<float>(*mOutputs[outChannel]);
            }
        }
    }

private:
    compileContext* mEelState = nullptr;
    NSEEL_CODEHANDLE mInitCode = nullptr;
    NSEEL_CODEHANDLE mBlockCode = nullptr;
    ;
    NSEEL_CODEHANDLE mSampleCode = nullptr;

    int mNumInputChannels = 0;
    int mNumOutputChannels = 0;
    int mNumParameters = 0; // number of automated parameters

    double mSampleRate = 0;
    int mBlockSize = 0;

    std::unique_ptr<double*[]> mInputs;
    std::unique_ptr<double*[]> mOutputs;
    std::unique_ptr<double*[]> mParameters;

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
        };
        if (bufNum == mSndBufNum && mSndBuf != nullptr) {
            return mSndBuf;
        }

        // de-validate cache
        mSndBuf = nullptr;
        mSndBufNum = bufNum;

        if (bufNum < mWorld->mNumSndBufs) {
            mSndBuf = mWorld->mSndBufs + bufNum;
            return mSndBuf;
        };

        // looking for a matching localbuf
        int localBufNum = bufNum - mWorld->mNumSndBufs;
        if (localBufNum <= mParent->localBufNum) {
            mSndBuf = mParent->mLocalSndBufs + localBufNum;
            return mSndBuf;
        }

        // no buffer found
        mSndBuf = nullptr;
        return nullptr;
    }

    /*! @brief Assumes that chan is within bounds */
    static float getSample(const SndBuf* buf, int chan, int sampleNum) {
        LOCK_SNDBUF_SHARED(buf);
        sampleNum = std::clamp<int>(sampleNum, 0, buf->samples - 1);
        return buf->data[buf->channels * sampleNum + chan];
    }

    static int getChannelOffset(const SndBuf* buf, int chanNum) {
        if (chanNum >= buf->channels || chanNum < 0) {
            return 0;
        }
        return chanNum;
    }
};
