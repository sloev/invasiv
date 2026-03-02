#pragma once

#ifndef TEST_MODE
#include "ofMain.h"
#endif

class Metronome {
public:
    float bpm = 120.0f;
    double referenceTime = 0.0; // Timestamp of a Beat 1
    int beatsPerBar = 4;
    
    // Tap Tempo helper
    std::vector<double> tapTimes;

    void setup() {
        referenceTime = ofGetElapsedTimeMillis();
    }

    void tap() {
        double now = ofGetElapsedTimeMillis();
        tapTimes.push_back(now);
        if (tapTimes.size() > 4) tapTimes.erase(tapTimes.begin());

        if (tapTimes.size() >= 2) {
            double totalDiff = 0;
            for (size_t i = 1; i < tapTimes.size(); i++) {
                totalDiff += (tapTimes[i] - tapTimes[i-1]);
            }
            float avgDiff = totalDiff / (tapTimes.size() - 1);
            bpm = 60000.0f / avgDiff;
        }
        // Always reset phase to the last tap as a new "Beat 1"
        referenceTime = now;
    }

    float getBeat() {
        double elapsed = ofGetElapsedTimeMillis() - referenceTime;
        double beatDuration = 60000.0 / bpm;
        return (float)(elapsed / beatDuration);
    }

    int getBeatInBar() {
        return ((int)std::floor(getBeat()) % beatsPerBar) + 1;
    }

    float getPhase() {
        float beat = getBeat();
        return beat - std::floor(beat);
    }

    bool isBeatFirst() {
        return getBeatInBar() == 1;
    }
};
