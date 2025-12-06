#pragma once

#include "ns-eel-int.h"

#include <atomic>
#include <iostream>
#include <string>

#include "eel2/ns-eel.h"

#include <SC_Graph.h>
#include <SC_Unit.h>
#include <SC_World.h>


// non rt safe! does not trim output
class DynGenScript {
public:
  DynGenScript(const std::string &script) {
    std::string_view stringView(script);
    // do not search for \n@init\n b/c the script may start with @init
    // which is fine.
    auto posInit = stringView.find("@init\n");
    auto posBlock = stringView.find("@block\n");
    auto posSample = stringView.find("@sample\n");

    // if no blocks given -> use code as sample block
    if (posInit == std::string::npos && posSample == std::string::npos && posBlock == std::string::npos) {
      sample = script;
      return;
    };

    if (posSample == std::string::npos) {
      std::cout << "DynGen script requires a sample section" << std::endl;
      return;
    }

    if (!validateBlockOrder(posInit, posBlock, posSample)) {
      std::cout << "DynGen: Wrong script block order, requires @init, @block, @sample order" << std::endl;
      return;
    }

    // + offsets b/c matching e.g. `@init\n` needs to shift by len 5
    const auto lenInit = 5; // length of string `\n@init\n`
    const auto lenBlock = 6;
    const auto lenSample = 7;
    auto startInit = (posInit != std::string::npos) ? posInit + lenInit : std::string::npos;
    auto startBlock = (posBlock != std::string::npos) ? posBlock + lenBlock : std::string::npos;
    auto startSample = (posSample != std::string::npos) ? posSample + lenSample : std::string::npos;

    if (posInit != std::string::npos) {
      const auto endPos = posBlock != std::string::npos ? posBlock : posSample;
      init = std::string(stringView.substr(startInit, endPos - startInit));
    }

    if (posBlock != std::string::npos) {
      block = std::string(stringView.substr(startBlock, posSample - startBlock));
    }

    sample = std::string(stringView.substr(startSample));
  }

  // script sections
  std::string init;
  std::string block;
  std::string sample;

private:
  static bool validateBlockOrder(size_t posInit, size_t posBlock, size_t posSample) {
    size_t lastPos = 0;
    if (posInit != std::string::npos) {
      lastPos = posInit;
    }
    if (posBlock != std::string::npos) {
      if (posBlock < lastPos) {
        return false;
      }
      lastPos = posBlock;
    }
    if (posSample != std::string::npos) {
      if (posSample < lastPos) {
        return false;
      }
    }
    return true;
  }
};

class EEL2Adapter {
public:
  EEL2Adapter(const uint32 numInputChannels, const uint32 numOutputChannels, const int sampleRate, const int blockSize, World *world, Graph* parent) : mNumInputChannels(numInputChannels), mNumOutputChannels(numOutputChannels), mSampleRate(sampleRate), mBlockSize(blockSize), mWorld(world), mParent(parent) {};
  ~EEL2Adapter();

  void init(const std::string &script);
  static EEL_F eelReadBuf(void* opaque, INT_PTR numParams, EEL_F** params);
  static EEL_F eelReadBufL(void* opaque, INT_PTR numParams, EEL_F** params);
  static EEL_F eelReadBufC(void* opaque, INT_PTR numParams, EEL_F** params);
  static EEL_F eelWriteBuf(void *opaque, INT_PTR numParams, EEL_F **param);

  void process(float **inBuf, float **outBuf, int numSamples) {
    if (mBlockCode) {
      NSEEL_code_execute(mBlockCode);
    }

    for (int i = 0; i < numSamples; i++) {
      // copy input buffer to vm - cast to double!
      for (int inChannel = 0; inChannel < mNumInputChannels; inChannel++) {
        *mInputs[inChannel] = static_cast<double>(inBuf[inChannel][i]);
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
  NSEEL_CODEHANDLE mBlockCode = nullptr;;
  NSEEL_CODEHANDLE mSampleCode = nullptr;

  int mNumInputChannels = 0;
  int mNumOutputChannels = 0;
  double mSampleRate = 0;
  int mBlockSize = 0;

  double **mInputs = nullptr;
  double **mOutputs = nullptr;

  World *mWorld;
  Graph *mParent;

  // cache the latest sndbuf b/c it is likely that we
  // stick to one sndbuf
  SndBuf *mSndBuf = nullptr;
  int mSndBufNum = -1;

  // @todo make this a RT alloc char* or free this via NRT thread
  DynGenScript *mScript;

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
