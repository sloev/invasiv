#pragma once

#include "ofMain.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <deque>
#include <onnxruntime_cxx_api.h>
#include "kiss_fftr.h"

// A simple Particle Filter state for tempo/phase tracking
struct PFState {
    float bpm = 120.0f;
    float phase = 0.0f; // 0.0 to 1.0
};

class BeatTracker {
public:
    BeatTracker();
    ~BeatTracker();

    void setup();
    void update();
    void drawDebug(int x, int y);
    
    // Audio callback
    void audioIn(ofSoundBuffer& input);

    // Getters for Metronome sync
    float getBPM() const { return currentBpm.load(); }
    float getLastBeatTime() const { return lastBeatTime.load(); }
    
    void setLatencyOffset(float ms) { latencyOffsetMs = ms; }
    float getLatencyOffset() const { return latencyOffsetMs; }

private:
    void processingThreadFunc();
    void computeSpectrogram(const std::vector<float>& audioFrame, std::vector<float>& outSpectrogram);
    void updateParticleFilter(float beatProb, float downbeatProb);

    // Threading and Buffering
    std::thread processingThread;
    std::atomic<bool> isRunning;
    
    std::mutex audioMutex;
    std::deque<float> audioBuffer;
    
    // ONNX Runtime
    Ort::Env* ortEnv = nullptr;
    Ort::Session* ortSession = nullptr;
    Ort::AllocatorWithDefaultOptions allocator;
    
    // DSP Parameters
    int sampleRate = 22050;
    int hopLength = 441;
    int winLength = 2048; // zero padded
    kiss_fftr_cfg fftCfg;
    std::vector<float> windowFunc;
    
    // Mel filterbank
    int numBands = 136;
    std::vector<std::vector<float>> filterbank; // [band][fft_bin]
    std::vector<float> prevSpectrogram;
    
    // State
    std::atomic<float> currentBpm;
    std::atomic<float> lastBeatTime;
    float latencyOffsetMs = 0.0f;
    
    // Raw output history for debug
    std::vector<float> historyProb;
};
