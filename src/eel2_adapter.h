#pragma once

#include "ns-eel-int.h"

#include <atomic>
#include <iostream>
#include <string>

#include "eel2/ns-eel.h"

#include <SC_Graph.h>
#include <SC_Unit.h>
#include <SC_World.h>

class EEL2Adapter {
public:
  EEL2Adapter(int numInputChannels, int numOutputChannels, int sampleRate, World *world, Graph* parent) : mNumInputChannels(numInputChannels), mNumOutputChannels(numOutputChannels), mSampleRate(sampleRate), mWorld(world), mParent(parent) {};
  ~EEL2Adapter();

  void init(const std::string &script);
  static EEL_F eelReadBuf(void* opaque, INT_PTR numParams, EEL_F** params);
  static EEL_F eelReadBufL(void* opaque, INT_PTR numParams, EEL_F** params);
  static EEL_F eelReadBufC(void* opaque, INT_PTR numParams, EEL_F** params);
  static EEL_F eelWriteBuf(void *opaque, INT_PTR numParams, EEL_F **param);

  void process(float **inBuf, float **outBuf, int numSamples) {
    for (int i = 0; i < numSamples; i++) {
      // copy input buffer to vm - cast to double!
      for (int inChannel = 0; inChannel < mNumInputChannels; inChannel++) {
        *mInputs[inChannel] = static_cast<double>(inBuf[inChannel][i]);
      }

      NSEEL_code_execute(code_);

      // read output buffer from vm
      for (int outChannel = 0; outChannel < mNumOutputChannels; outChannel++) {
        outBuf[outChannel][i] = static_cast<float>(*mOutputs[outChannel]);
      }
    }
  }

  std::atomic<bool> mReady{false};

private:
  compileContext* eel_state_ = nullptr;
  NSEEL_CODEHANDLE code_ = nullptr;

  int mNumInputChannels = 0;
  int mNumOutputChannels = 0;
  double mSampleRate = 0;

  double **mInputs = nullptr;
  double **mOutputs = nullptr;

  World *mWorld;
  Graph *mParent;

  // cache the latest sndbuf b/c it is likely that we
  // stick to one sndbuf
  SndBuf *mSndBuf = nullptr;
  int mSndBufNum = -1;

  // @todo make this a RT alloc char* or free this via NRT thread
  std::string mScript;

  // see GET_BUF macro from SC_Unit.h
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

  // Assumes that chan is within bounds
  static float getSample(const SndBuf *buf, int chan, int sampleNum) {
    LOCK_SNDBUF_SHARED(buf);
    sampleNum = std::clamp<int>(sampleNum, 0, buf->samples - 1);
    return buf->data[buf->channels * sampleNum + chan];
  }

  static int getChannelOffset(const SndBuf *buf, int chanNum) {
    if (chanNum >= buf->channels || chanNum < 0) {
      return 0;
    }
    return chanNum;
  }
};
