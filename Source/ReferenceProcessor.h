#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include "MeterDisplay.h"

// One stereo input, unchanged main output, and an independent stereo reference output.
// All sample routing is allocation-free; track-channel mapping belongs to the host.
class ReferenceProcessor final : public juce::AudioProcessor {
public:
    ReferenceProcessor() : AudioProcessor(BusesProperties()
        .withInput("Source",juce::AudioChannelSet::stereo(),true)
        .withOutput("Main through",juce::AudioChannelSet::stereo(),true)
        .withOutput("Original reference",juce::AudioChannelSet::stereo(),true)) {}
    bool isBusesLayoutSupported(const BusesLayout& b) const override {
        return b.inputBuses.size()==1 && b.outputBuses.size()==2
            && b.getMainInputChannelSet()==juce::AudioChannelSet::stereo()
            && b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()
            && (b.outputBuses[1].isDisabled() || b.outputBuses[1]==juce::AudioChannelSet::stereo());
    }
    void prepareToPlay(double,int) override { peak=0; displayPeak.reset(); referenceEnabled=getBus(false,1)->isEnabled(); }
    void releaseResources() override {}
    template<class T> void process(juce::AudioBuffer<T>& buffer) {
        auto source=getBusBuffer(buffer,true,0);
        const bool enabled=getBus(false,1)->isEnabled(); referenceEnabled=enabled;
        if(enabled) {
            auto reference=getBusBuffer(buffer,false,1);
            for(int ch=0;ch<2;++ch)
                std::copy_n(source.getReadPointer(ch),source.getNumSamples(),reference.getWritePointer(ch));
        }
        // Main input/output share their stereo buffer: preserve every sample exactly.
        peak=static_cast<float>(source.getMagnitude(0,source.getNumSamples())); displayPeak.capture(peak.load()); ++sequence;
    }
    void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override { process(b); }
    void processBlock(juce::AudioBuffer<double>& b,juce::MidiBuffer&) override { process(b); }
    template<class T> void bypass(juce::AudioBuffer<T>& buffer) {
        // Bypass preserves the main signal and clears the reference output.
        const bool enabled=getBus(false,1)->isEnabled(); referenceEnabled=enabled;
        if(enabled) getBusBuffer(buffer,false,1).clear();
        auto source=getBusBuffer(buffer,true,0);
        peak=static_cast<float>(source.getMagnitude(0,source.getNumSamples())); displayPeak.capture(peak.load()); ++sequence;
    }
    void processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override { bypass(b); }
    void processBlockBypassed(juce::AudioBuffer<double>& b,juce::MidiBuffer&) override { bypass(b); }
    bool supportsDoublePrecisionProcessing() const override { return true; }
    const juce::String getName() const override { return "LUFS Reference"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    void getStateInformation(juce::MemoryBlock& destination) override {
        juce::XmlElement saved("LUFSReference"); saved.setAttribute("uiSize",uiSize.load()); copyXmlToBinary(saved,destination);
    }
    void setStateInformation(const void* data,int size) override {
        if(auto saved=getXmlFromBinary(data,size);saved && saved->hasTagName("LUFSReference"))
            uiSize=juce::jlimit(1,3,saved->getIntAttribute("uiSize",2));
    }
    std::atomic<int> uiSize{2};
    std::atomic<float> peak{0};
    std::atomic<bool> referenceEnabled{true};
    std::atomic<unsigned> sequence{0};
    DisplayPeak displayPeak;
};

class ReferencePanel final : public juce::Component,private juce::Timer {
public:
    explicit ReferencePanel(ReferenceProcessor& p):processor(p) {
        sizes.setName("Interface size"); sizes.setComponentID("uiSize");
        sizes.addItem("Small (75%)",1); sizes.addItem("Medium (100%)",2); sizes.addItem("Large (125%)",3);
        sizes.setColour(juce::ComboBox::backgroundColourId,juce::Colour(0xff252b29));
        sizes.setColour(juce::ComboBox::textColourId,juce::Colour(0xffd7ac61));
        sizes.setSelectedId(processor.uiSize.load(),juce::dontSendNotification); addAndMakeVisible(sizes);
        sizes.onChange=[this] {
            processor.uiSize=sizes.getSelectedId();
            processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
            if(sizeChanged) sizeChanged();
        };
        target=processor.peak.load(); ballistics.resetPeak(target); lastSequence=processor.sequence.load();
        setSize(840,360); startTimerHz(30);
    }
    ~ReferencePanel() override { stopTimer(); }
    std::function<void()> sizeChanged;
    void resized() override { sizes.setBounds(630,20,174,28); }
    void paint(juce::Graphics& g) override {
        const juce::Colour cream{0xfff4eddb},ink{0xff252b29},gold{0xffd7ac61};
        g.fillAll(cream); g.setColour(ink); g.setFont(juce::FontOptions(11.f));
        g.drawText("YODO AUDIO    /    START OF CHAIN",32,17,570,22,juce::Justification::left);
        g.setFont(juce::FontOptions(34.f)); g.drawText("LUFS Reference",30,47,560,50,juce::Justification::left);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff343d36),0,110,juce::Colour(0xff1e2722),0,320,false));
        g.fillRoundedRectangle(16,110,808,210,16);
        g.setColour(gold); g.setFont(juce::FontOptions(25.f));
        const auto db=ballistics.getLevelDb();
        g.drawText(db<=-60?"SOURCE  -- dBFS":"SOURCE  "+juce::String(db,1)+" dBFS",44,137,752,40,juce::Justification::centred);
        g.setColour(cream); g.setFont(juce::FontOptions(14.f));
        g.drawText("MAIN THROUGH  +  ORIGINAL REFERENCE",44,197,752,26,juce::Justification::centred);
        g.setFont(juce::FontOptions(12.f));
        g.drawText(processor.referenceEnabled.load()?"Map the reference output to a spare stereo pair in your host":"Reference output bus is disabled: enable it in your host",44,244,752,28,juce::Justification::centred);
        g.setColour(ink.withAlpha(.65f)); g.setFont(juce::FontOptions(10.f));
        g.drawText("STEREO SAMPLE COPY  /  UNCHANGED MAIN AUDIO  /  ZERO ADDED LATENCY",32,332,772,20,juce::Justification::centred);
    }
private:
    void timerCallback() override {
        const auto now=juce::Time::getMillisecondCounterHiRes();
        const auto elapsed=(now-lastMeterTime)*.001; lastMeterTime=now;
        const auto current=processor.sequence.load();
        if(current!=lastSequence) { lastSequence=current; idleSeconds=0; target=processor.displayPeak.take(); }
        else idleSeconds+=elapsed;
        ballistics.advance(idleSeconds>.5?0.f:target,elapsed);
        if(sizes.getSelectedId()!=processor.uiSize.load()) {
            sizes.setSelectedId(processor.uiSize.load(),juce::dontSendNotification); if(sizeChanged) sizeChanged();
        }
        repaint();
    }
    ReferenceProcessor& processor;
    juce::ComboBox sizes;
    unsigned lastSequence=0;
    MeterBallistics ballistics;
    float target=0;
    double lastMeterTime=juce::Time::getMillisecondCounterHiRes(),idleSeconds=0;
};

class ReferenceEditor final : public juce::AudioProcessorEditor {
public:
    explicit ReferenceEditor(ReferenceProcessor& p):AudioProcessorEditor(p),processor(p),panel(p) {
        setResizable(false,false); addAndMakeVisible(panel); panel.sizeChanged=[this] { applySize(); }; applySize();
    }
private:
    void applySize() {
        const float scale=processor.uiSize.load()==1?.75f:processor.uiSize.load()==3?1.25f:1.f;
        panel.setTransform(juce::AffineTransform::scale(scale)); panel.setBounds(0,0,840,360);
        setSize(juce::roundToInt(840*scale),juce::roundToInt(360*scale));
    }
    ReferenceProcessor& processor;
    ReferencePanel panel;
};
inline juce::AudioProcessorEditor* ReferenceProcessor::createEditor() { return new ReferenceEditor(*this); }
