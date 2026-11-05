#pragma once
#include <algorithm>
#include <cmath>
#include <atomic>

// Smooth the displayed dB/needle position only. Never used in the audio path.
class MeterBallistics {
public:
    static constexpr double attackSeconds=0.12;
    static constexpr double releaseSeconds=0.45;
    void resetPeak(float peak) { db=peakDb(peak); }
    float advance(float peak,double elapsedSeconds) {
        if(!std::isfinite(elapsedSeconds) || elapsedSeconds<=0) return db;
        const float target=peakDb(peak);
        const double timeConstant=target>db?attackSeconds:releaseSeconds;
        const double amount=-std::expm1(-elapsedSeconds/timeConstant);
        db+=static_cast<float>(amount*static_cast<double>(target-db));
        if(db<-59.95f && target<=-60.f) db=-60.f;
        return db;
    }
    float getLevelDb() const { return db; }
private:
    static float peakDb(float peak) {
        return std::isfinite(peak) && peak>0.001f?20.f*std::log10(peak):-60.f;
    }
    float db=-60;
};

// Retain the largest audio-block peak until the UI consumes it. Short peaks
// between redraws are retained without changing audio or LUFS measurement.
class DisplayPeak {
public:
    void capture(float value) {
        auto previous=peak.load(std::memory_order_relaxed);
        while(previous<value && !peak.compare_exchange_weak(previous,value,std::memory_order_relaxed)) {}
    }
    float take() { return peak.exchange(0.f,std::memory_order_relaxed); }
    void reset() { peak=0.f; }
private:
    std::atomic<float> peak{0.f};
};
