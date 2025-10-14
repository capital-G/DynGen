#pragma once

#include <iostream>
#include <string>

#include "eel2/ns-eel.h"

class EEL2Adapter {
public:
  EEL2Adapter(int numInputChannels, int numOutputChannels, int sampleRate) : mNumInputChannels(numInputChannels), mNumOutputChannels(numOutputChannels), mSampleRate(sampleRate) {};
  ~EEL2Adapter();

  void init(const std::string &script);

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
  NSEEL_VMCTX eel_state_ = nullptr;
  NSEEL_CODEHANDLE code_ = nullptr;


  int mNumInputChannels = 0;
  int mNumOutputChannels = 0;
  double mSampleRate = 0;

  double **mInputs = nullptr;
  double **mOutputs = nullptr;

  std::string mScript;
};
