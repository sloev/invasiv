#include <iostream>
#include <cassert>
#include <cmath>
#include <thread>
#include <chrono>

// Mock ofMain.h enough for Metronome.h
#define OF_VERSION_MAJOR 0
#include <vector>
#include <string>
#include <algorithm>

namespace of {
    unsigned long long getElapsedTimeMillis() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }
}
#define ofGetElapsedTimeMillis of::getElapsedTimeMillis

// Include the class under test
#include "../invasiv_app/src/Metronome.h"

void test_metronome_logic() {
    Metronome m;
    m.bpm = 60.0f; // 1 beat per second
    m.referenceTime = ofGetElapsedTimeMillis();
    m.beatsPerBar = 4;

    std::cout << "Testing Metronome at 60 BPM..." << std::endl;
    
    // Test initial state
    assert(m.getBeatInBar() == 1);
    
    // Wait ~1.1 seconds (should be into beat 2)
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    
    float beat = m.getBeat();
    std::cout << "Beat after 1.1s: " << beat << std::endl;
    assert(beat >= 1.0f && beat < 2.0f);
    assert(m.getBeatInBar() == 2);

    // Test tap tempo
    std::cout << "Testing Tap Tempo..." << std::endl;
    m.tap(); // First tap
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    m.tap(); // Second tap (should result in 120 BPM)
    
    std::cout << "New BPM: " << m.bpm << std::endl;
    // Allow for some timing jitter in sleep
    assert(m.bpm > 110.0f && m.bpm < 130.0f);
    
    std::cout << "Metronome Unit Tests PASSED" << std::endl;
}

int main() {
    try {
        test_metronome_logic();
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
