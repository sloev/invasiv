#include "BeatTracker.h"
#include <cmath>
#include <algorithm>

BeatTracker::BeatTracker() {
    currentBpm.store(120.0f);
    lastBeatTime.store(0.0f);
    isRunning.store(false);
}

BeatTracker::~BeatTracker() {
    isRunning.store(false);
    if (processingThread.joinable()) {
        processingThread.join();
    }
    
    if (ortSession) delete ortSession;
    if (ortEnv) delete ortEnv;
    
    if (fftCfg) kiss_fftr_free(fftCfg);
}

void BeatTracker::setup() {
    // 1. Setup DSP
    fftCfg = kiss_fftr_alloc(winLength, 0, NULL, NULL);
    windowFunc.resize(winLength);
    // Hanning window
    for (int i = 0; i < winLength; ++i) {
        windowFunc[i] = 0.5f * (1.0f - cos(2.0f * PI * i / (winLength - 1)));
    }
    
    // Create simplistic log filterbank (30Hz to 17000Hz, 136 bands)
    filterbank.resize(numBands, std::vector<float>(winLength / 2 + 1, 0.0f));
    float minLogFreq = log10(30.0f);
    float maxLogFreq = log10(17000.0f);
    
    for (int b = 0; b < numBands; ++b) {
        float fCenter = pow(10.0f, minLogFreq + (maxLogFreq - minLogFreq) * (float)b / (numBands - 1));
        // Find bin
        int bin = (fCenter / (sampleRate / 2.0f)) * (winLength / 2);
        if (bin >= 0 && bin <= winLength / 2) {
            filterbank[b][bin] = 1.0f; // Simplified: just map 1-to-1 to nearest bin instead of triangle
        }
    }
    
    prevSpectrogram.resize(numBands, 0.0f);
    
    // 2. Setup ONNX Runtime
    try {
        ortEnv = new Ort::Env(ORT_LOGGING_LEVEL_WARNING, "BeatNet");
        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(1);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        
        std::string modelPath = ofToDataPath("models/beatnet.onnx", true);
        ortSession = new Ort::Session(*ortEnv, modelPath.c_str(), sessionOptions);
        ofLogNotice("BeatTracker") << "ONNX model loaded successfully: " << modelPath;
    } catch (const Ort::Exception& e) {
        ofLogError("BeatTracker") << "Failed to load ONNX model: " << e.what();
    }
    
    // 3. Start processing thread
    isRunning.store(true);
    processingThread = std::thread(&BeatTracker::processingThreadFunc, this);
}

void BeatTracker::update() {
    // Only used for visualization updates if needed
}

void BeatTracker::drawDebug(int x, int y) {
    ofSetColor(255);
    ofDrawBitmapString("BeatTracker", x, y);
    ofDrawBitmapString("BPM: " + ofToString(currentBpm.load()), x, y + 20);
    ofDrawBitmapString("Last Beat: " + ofToString(lastBeatTime.load()), x, y + 40);
    
    // Draw activation history
    ofPushMatrix();
    ofTranslate(x, y + 60);
    ofSetColor(100);
    ofDrawRectangle(0, 0, 200, 50);
    ofSetColor(255, 0, 0);
    ofNoFill();
    ofBeginShape();
    float w = 200.0f / std::max(1, (int)historyProb.size());
    for (size_t i = 0; i < historyProb.size(); ++i) {
        ofVertex(i * w, 50 - historyProb[i] * 50);
    }
    ofEndShape();
    ofFill();
    ofPopMatrix();
}

void BeatTracker::audioIn(ofSoundBuffer& input) {
    // Push audio to lock-free or mutex queue
    std::lock_guard<std::mutex> lock(audioMutex);
    for (size_t i = 0; i < input.getNumFrames(); ++i) {
        // Mix down to mono
        float mono = 0.0f;
        for (size_t c = 0; c < input.getNumChannels(); ++c) {
            mono += input.getSample(i, c);
        }
        mono /= input.getNumChannels();
        audioBuffer.push_back(mono);
    }
}

void BeatTracker::computeSpectrogram(const std::vector<float>& audioFrame, std::vector<float>& outSpectrogram) {
    // 1. Apply window
    std::vector<float> windowed(winLength, 0.0f);
    int copyLen = std::min((int)audioFrame.size(), winLength);
    for (int i = 0; i < copyLen; ++i) {
        windowed[i] = audioFrame[i] * windowFunc[i];
    }
    
    // 2. FFT
    std::vector<kiss_fft_cpx> fftOut(winLength / 2 + 1);
    kiss_fftr(fftCfg, windowed.data(), fftOut.data());
    
    // 3. Magnitudes
    std::vector<float> magnitudes(winLength / 2 + 1);
    for (int i = 0; i < winLength / 2 + 1; ++i) {
        magnitudes[i] = sqrt(fftOut[i].r * fftOut[i].r + fftOut[i].i * fftOut[i].i);
    }
    
    // 4. Log Mel Filterbank + Difference
    outSpectrogram.resize(numBands * 2);
    for (int b = 0; b < numBands; ++b) {
        float bandMag = 0.0f;
        for (int i = 0; i < winLength / 2 + 1; ++i) {
            bandMag += magnitudes[i] * filterbank[b][i];
        }
        // Log scale
        float logMag = log(1.0f + bandMag);
        
        // Difference
        float diff = std::max(0.0f, logMag - prevSpectrogram[b]);
        prevSpectrogram[b] = logMag;
        
        // Feature vector is concatenated [logMag, diff]
        outSpectrogram[b] = logMag;
        outSpectrogram[numBands + b] = diff;
    }
}

void BeatTracker::processingThreadFunc() {
    std::vector<float> frame(winLength, 0.0f);
    
    // LSTM states
    std::vector<float> hidden(2 * 1 * 150, 0.0f);
    std::vector<float> cell(2 * 1 * 150, 0.0f);
    
    while (isRunning.load()) {
        bool hasEnoughData = false;
        {
            std::lock_guard<std::mutex> lock(audioMutex);
            if (audioBuffer.size() >= hopLength) {
                hasEnoughData = true;
                // Peek frame
                int readLen = std::min((int)audioBuffer.size(), winLength);
                for (int i = 0; i < readLen; ++i) {
                    frame[i] = audioBuffer[i];
                }
                // Consume hop
                for (int i = 0; i < hopLength; ++i) {
                    audioBuffer.pop_front();
                }
            }
        }
        
        if (hasEnoughData) {
            std::vector<float> feats;
            computeSpectrogram(frame, feats); // Size 272
            
            if (ortSession) {
                // Prepare ONNX inputs
                std::vector<int64_t> inputShape = {1, 1, 272};
                std::vector<int64_t> hiddenShape = {2, 1, 150};
                std::vector<int64_t> cellShape = {2, 1, 150};
                
                Ort::MemoryInfo memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
                Ort::Value inputTensor = Ort::Value::CreateTensor<float>(memoryInfo, feats.data(), feats.size(), inputShape.data(), inputShape.size());
                Ort::Value hiddenTensor = Ort::Value::CreateTensor<float>(memoryInfo, hidden.data(), hidden.size(), hiddenShape.data(), hiddenShape.size());
                Ort::Value cellTensor = Ort::Value::CreateTensor<float>(memoryInfo, cell.data(), cell.size(), cellShape.data(), cellShape.size());
                
                std::vector<const char*> inputNames = {"input", "hidden_in", "cell_in"};
                std::vector<Ort::Value> inputTensors;
                inputTensors.push_back(std::move(inputTensor));
                inputTensors.push_back(std::move(hiddenTensor));
                inputTensors.push_back(std::move(cellTensor));
                
                std::vector<const char*> outputNames = {"output", "hidden_out", "cell_out"};
                
                try {
                    auto outputTensors = ortSession->Run(Ort::RunOptions{nullptr}, inputNames.data(), inputTensors.data(), 3, outputNames.data(), 3);
                    
                    // output shape: (1, 3, 1) -> [beat_prob, downbeat_prob, non_beat_prob]
                    float* outData = outputTensors[0].GetTensorMutableData<float>();
                    float beatProb = outData[0];
                    float downbeatProb = outData[1];
                    
                    // Update hidden and cell for next frame
                    float* nextHidden = outputTensors[1].GetTensorMutableData<float>();
                    float* nextCell = outputTensors[2].GetTensorMutableData<float>();
                    std::copy(nextHidden, nextHidden + hidden.size(), hidden.begin());
                    std::copy(nextCell, nextCell + cell.size(), cell.begin());
                    
                    updateParticleFilter(beatProb, downbeatProb);
                    
                    // Store for debug UI
                    if (historyProb.size() > 100) historyProb.erase(historyProb.begin());
                    historyProb.push_back(beatProb);
                    
                } catch (const Ort::Exception& e) {
                    ofLogError("BeatTracker") << "ONNX Inference Error: " << e.what();
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

void BeatTracker::updateParticleFilter(float beatProb, float downbeatProb) {
    // SIMPLIFIED PARTICLE FILTER / PEAK PICKER
    // In a full implementation, this uses a formal Monte Carlo Particle Filter
    // Here we use a simple peak-picking mechanism with a momentum buffer 
    // to simulate the "lookahead" phase-locking discussed in beattracker.md.
    
    static float prevBeatProb = 0.0f;
    static float lastTriggerTime = 0.0f;
    static float currentExpectedInterval = 60.0f / 120.0f; // 120 BPM
    
    float now = ofGetElapsedTimef();
    
    // Very simple peak picker
    if (beatProb > 0.5f && prevBeatProb <= 0.5f) {
        // We have a beat trigger!
        // Calculate raw interval
        float interval = now - lastTriggerTime;
        
        // Ignore rapid double-triggers (e.g. bounce)
        if (interval > 0.3f) { // Max ~200 BPM
            // Lookahead Compensation: We assume the audio was buffered for ~50ms
            // plus we subtract user latency offset
            float triggerTimestamp = now - 0.05f - (latencyOffsetMs / 1000.0f);
            
            // Soft-sync BPM update (Phase-Locked Loop style)
            if (interval < 1.5f) { // Min ~40 BPM
                float rawBpm = 60.0f / interval;
                float oldBpm = currentBpm.load();
                // Exponential moving average (stubborn tempo enforcement)
                float smoothedBpm = (oldBpm * 0.8f) + (rawBpm * 0.2f);
                currentBpm.store(smoothedBpm);
                currentExpectedInterval = 60.0f / smoothedBpm;
            }
            
            lastTriggerTime = triggerTimestamp;
            lastBeatTime.store(triggerTimestamp);
        }
    }
    prevBeatProb = beatProb;
}
