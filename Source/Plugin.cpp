#include <JuceHeader.h>
#include "LoudnessMatch.h"
#include <atomic>

class MatchProcessor final : public juce::AudioProcessor, private juce::Timer {
public:
    MatchProcessor() : AudioProcessor(BusesProperties()
        .withInput("Processed",juce::AudioChannelSet::stereo(),true)
        .withInput("Original reference",juce::AudioChannelSet::stereo(),true)
        .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
        state(*this,nullptr,"LUFSCompensation",layout()) { startTimerHz(10); }
    ~MatchProcessor() override { stopTimer(); }
    static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
        juce::AudioProcessorValueTreeState::ParameterLayout p;
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"matchEnabled",1},"Level Match",true));
        p.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"original",1},"Original A/B",false));
        return p;
    }
    bool isBusesLayoutSupported(const BusesLayout& b) const override {
        return b.inputBuses.size()==2 && b.outputBuses.size()==1
            && b.getMainInputChannelSet()==juce::AudioChannelSet::stereo()
            && b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo()
            && (b.inputBuses[1].isDisabled() || b.inputBuses[1]==juce::AudioChannelSet::stereo());
    }
    double targetGain() const {
        return learned.load() && state.getRawParameterValue("matchEnabled")->load()>.5f
            ? std::pow(10.,static_cast<double>(trimDb.load())/20.) : 1.;
    }
    void prepareToPlay(double rate,int) override {
        meter.prepare(rate); learning=false; seconds=0;
        gain.reset(rate,.02); blend.reset(rate,.005);
        gain.setCurrentAndTargetValue(targetGain());
        const bool ref=getBus(true,1)->isEnabled();
        referenceAvailable=ref;
        blend.setCurrentAndTargetValue(ref && state.getRawParameterValue("original")->load()>.5f?0.:1.);
        referencePeak=processedPeak=outputPeak=0;
    }
    void releaseResources() override {}
    void finish() {
        const auto result=meter.finish();
        if(result==LoudnessMatch::Result::success || result==LoudnessMatch::Result::limited) {
            trimDb=static_cast<float>(meter.trimDb); inputLufs=static_cast<float>(meter.inputLufs);
            processedLufs=static_cast<float>(meter.outputLufs); learned=true; revision.fetch_add(1);
        }
        outcome=static_cast<int>(result); learning=false;
    }
    void commands(bool ref) {
        const int request=command.exchange(0);
        if(request==1) {
            outcome=0; seconds=0;
            if(ref) { meter.start(); learning=true; }
            else { meter.cancel(); learning=false; outcome=5; }
        } else if(request==2 && meter.learning()) finish();
        else if(request==3) {
            meter.cancel(); learned=false; learning=false; trimDb=0; seconds=0;
            inputLufs=processedLufs=0; outcome=0; revision.fetch_add(1);
        } else if(request==4) { meter.cancel(); learning=false; seconds=0; outcome=0; }
        if(!ref && meter.learning()) { meter.cancel(); learning=false; outcome=5; }
    }
    template<class T> void process(juce::AudioBuffer<T>& buffer) {
        juce::ScopedNoDenormals noDenormals;
        auto main=getBusBuffer(buffer,true,0);
        const bool ref=getBus(true,1)->isEnabled(); referenceAvailable=ref;
        commands(ref);
        auto reference=getBusBuffer(buffer,true,1);
        gain.setTargetValue(targetGain());
        blend.setTargetValue(ref && state.getRawParameterValue("original")->load()>.5f?0.:1.);
        float refMax=0,mainMax=0,outMax=0;
        for(int i=0;i<main.getNumSamples();++i) {
            const T l=main.getSample(0,i),r=main.getSample(1,i);
            const T a=ref?reference.getSample(0,i):T{},b=ref?reference.getSample(1,i):T{};
            if(meter.learning()) {
                meter.sample(a,b,l,r);
                if(meter.complete()) { finish(); gain.setTargetValue(targetGain()); }
            }
            const double g=gain.getNextValue(),mix=blend.getNextValue();
            T outL=l,outR=r;
            if(mix<=0) { outL=a; outR=b; }
            else if(mix>=1) { if(g!=1.) { outL=static_cast<T>(l*g); outR=static_cast<T>(r*g); } }
            else { outL=static_cast<T>(a*(1-mix)+l*g*mix); outR=static_cast<T>(b*(1-mix)+r*g*mix); }
            main.setSample(0,i,outL); main.setSample(1,i,outR);
            refMax=juce::jmax(refMax,static_cast<float>(std::max(std::abs(a),std::abs(b))));
            mainMax=juce::jmax(mainMax,static_cast<float>(std::max(std::abs(l),std::abs(r))));
            outMax=juce::jmax(outMax,static_cast<float>(std::max(std::abs(outL),std::abs(outR))));
        }
        seconds=meter.seconds(); referencePeak=refMax; processedPeak=mainMax; outputPeak=outMax; ++sequence;
    }
    void processBlock(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override { process(b); }
    void processBlock(juce::AudioBuffer<double>& b,juce::MidiBuffer&) override { process(b); }
    template<class T> void bypass(juce::AudioBuffer<T>& b) {
        const bool ref=getBus(true,1)->isEnabled(); referenceAvailable=ref; commands(ref);
        // Host bypass passes the processed main input unchanged; no learning during bypass.
        auto main=getBusBuffer(b,true,0); outputPeak=static_cast<float>(main.getMagnitude(0,main.getNumSamples()));
        processedPeak=outputPeak.load(); referencePeak=0; ++sequence;
    }
    void processBlockBypassed(juce::AudioBuffer<float>& b,juce::MidiBuffer&) override { bypass(b); }
    void processBlockBypassed(juce::AudioBuffer<double>& b,juce::MidiBuffer&) override { bypass(b); }
    bool supportsDoublePrecisionProcessing() const override { return true; }
    const juce::String getName() const override { return "LUFS Compensation"; }
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
        auto saved=state.copyState();
        saved.setProperty("trimDb",trimDb.load(),nullptr); saved.setProperty("learned",learned.load(),nullptr);
        saved.setProperty("inputLufs",inputLufs.load(),nullptr); saved.setProperty("processedLufs",processedLufs.load(),nullptr);
        saved.setProperty("uiSize",uiSize.load(),nullptr);
        if(auto xml=saved.createXml()) copyXmlToBinary(*xml,destination);
    }
    void setStateInformation(const void* data,int size) override {
        if(auto xml=getXmlFromBinary(data,size);xml && xml->hasTagName(state.state.getType())) {
            auto saved=juce::ValueTree::fromXml(*xml);
            const float db=static_cast<float>(saved.getProperty("trimDb",0.f));
            trimDb=std::isfinite(db)?juce::jlimit(-24.f,24.f,db):0.f;
            learned=std::isfinite(db) && static_cast<bool>(saved.getProperty("learned",false));
            auto finite=[](float v) { return std::isfinite(v)?v:0.f; };
            inputLufs=finite(static_cast<float>(saved.getProperty("inputLufs",0.f)));
            processedLufs=finite(static_cast<float>(saved.getProperty("processedLufs",0.f)));
            uiSize=juce::jlimit(1,3,static_cast<int>(saved.getProperty("uiSize",2)));
            command=4; state.replaceState(saved);
        }
    }
    juce::AudioProcessorValueTreeState state;
    std::atomic<float> trimDb{0},inputLufs{0},processedLufs{0},seconds{0};
    std::atomic<float> referencePeak{0},processedPeak{0},outputPeak{0};
    std::atomic<bool> learned{false},learning{false},referenceAvailable{false};
    std::atomic<int> command{0},outcome{0},uiSize{2};
    std::atomic<unsigned> sequence{0},revision{0};
private:
    void timerCallback() override {
        const auto current=revision.load();
        if(current!=notified) { notified=current; updateHostDisplay(ChangeDetails{}.withNonParameterStateChanged(true)); }
    }
    unsigned notified=0;
    LoudnessMatch meter;
    juce::SmoothedValue<double> gain{1.},blend{1.};
};

namespace theme {
const juce::Colour cream{0xfff4eddb},ink{0xff252b29},gold{0xffd7ac61},muted{0xff929b8c};
}

class LevelDisplay final : public juce::Component {
public:
    LevelDisplay() { setTitle("Signal levels"); }
    void setLevels(float reference,float processed,float output) { values={reference,processed,output}; repaint(); }
    void paint(juce::Graphics& g) override {
        const char* names[]{"ORIGINAL REFERENCE","PROCESSED INPUT","OUTPUT"};
        const auto bounds=getLocalBounds().toFloat();
        for(int i=0;i<3;++i) {
            const float y=static_cast<float>(i)*47;
            const float db=juce::Decibels::gainToDecibels(values[static_cast<size_t>(i)],-60.f);
            g.setColour(theme::cream); g.setFont(juce::FontOptions(11.f));
            g.drawText(names[i],0,static_cast<int>(y),230,20,juce::Justification::left);
            g.drawText(db<=-60?"-- dBFS":juce::String(db,1)+" dBFS",getWidth()-110,static_cast<int>(y),110,20,juce::Justification::right);
            const juce::Rectangle<float> bar{0,y+25,bounds.getWidth(),7};
            g.setColour(theme::cream.withAlpha(.12f)); g.fillRoundedRectangle(bar,3);
            const auto fraction=juce::jlimit(0.f,1.f,(db+60)/66);
            g.setColour(db>0?juce::Colour(0xffce6952):theme::gold);
            g.fillRoundedRectangle(bar.withWidth(bar.getWidth()*fraction),3);
        }
    }
private:
    std::array<float,3> values{};
};

class MatchPanel final : public juce::Component, private juce::Timer {
public:
    explicit MatchPanel(MatchProcessor& p):processor(p) {
        for(auto* button:{&learn,&reset,&enabled,&original}) {
            addAndMakeVisible(*button);
            button->setColour(juce::TextButton::buttonColourId,theme::ink.brighter(.15f));
            button->setColour(juce::TextButton::textColourOffId,theme::cream);
            button->setColour(juce::TextButton::textColourOnId,theme::gold);
        }
        learn.setComponentID("learnMatch"); reset.setComponentID("resetMatch");
        reset.setButtonText("Reset Match");
        enabled.setComponentID("matchEnabled"); original.setComponentID("abOriginal");
        learn.setTooltip("Learn the LUFS difference between the original reference and the processed chain. Finish after at least 3 seconds; automatic finish at 15 seconds.");
        reset.setTooltip("Clear the learned adjustment, preserving Match and A/B switches.");
        enabled.setTooltip("Enable or disable the fixed learned gain without losing it.");
        original.setTooltip("Compare the original reference with the processed chain using a short fade.");
        learn.onClick=[this] { processor.command=processor.learning.load()?2:1; };
        reset.onClick=[this] {
            processor.learned=false; processor.trimDb=0; processor.inputLufs=0; processor.processedLufs=0;
            processor.learning=false; processor.seconds=0; processor.outcome=0;
            processor.command=3; ++processor.revision; refresh();
        };
        enabled.setClickingTogglesState(true); original.setClickingTogglesState(true);
        enableAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state,"matchEnabled",enabled);
        originalAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state,"original",original);
        addAndMakeVisible(sizes); sizes.setName("Interface size"); sizes.setComponentID("uiSize");
        sizes.addItem("Small (75%)",1); sizes.addItem("Medium (100%)",2); sizes.addItem("Large (125%)",3);
        sizes.setColour(juce::ComboBox::backgroundColourId,theme::ink);
        sizes.setColour(juce::ComboBox::textColourId,theme::gold);
        sizes.setSelectedId(processor.uiSize.load(),juce::dontSendNotification);
        sizes.onChange=[this] {
            processor.uiSize=sizes.getSelectedId();
            processor.updateHostDisplay(juce::AudioProcessor::ChangeDetails{}.withNonParameterStateChanged(true));
            if(sizeChanged) sizeChanged();
        };
        for(auto* label:{&trim,&measurement,&status,&route}) {
            addAndMakeVisible(*label); label->setJustificationType(juce::Justification::centred);
            label->setColour(juce::Label::textColourId,theme::cream);
        }
        trim.setComponentID("matchReadout"); measurement.setComponentID("measurement");
        status.setComponentID("matchStatus"); route.setComponentID("routingStatus");
        trim.setFont(juce::FontOptions(44.f)); trim.setColour(juce::Label::textColourId,theme::gold);
        measurement.setFont(juce::FontOptions(14.f)); status.setFont(juce::FontOptions(12.f));
        route.setFont(juce::FontOptions(12.f)); route.setColour(juce::Label::textColourId,theme::gold);
        addAndMakeVisible(levels); refresh(); setSize(840,650); startTimerHz(30);
    }
    ~MatchPanel() override { stopTimer(); }
    std::function<void()> sizeChanged;
    void resized() override {
        sizes.setBounds(630,20,174,28);
        trim.setBounds(44,144,752,62); measurement.setBounds(44,211,752,28);
        levels.setBounds(64,268,712,144);
        learn.setBounds(44,444,174,36); reset.setBounds(236,444,174,36);
        enabled.setBounds(428,444,174,36); original.setBounds(620,444,174,36);
        status.setBounds(44,493,752,48); route.setBounds(44,552,752,36);
    }
    void paint(juce::Graphics& g) override {
        using namespace theme;
        g.fillAll(cream); g.setColour(ink); g.setFont(juce::FontOptions(11.f));
        g.drawText("YODO AUDIO    /    CHAIN LOUDNESS MATCHING",32,17,580,22,juce::Justification::left);
        g.setFont(juce::FontOptions(34.f)); g.drawText("LUFS Compensation",30,47,560,50,juce::Justification::left);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff343d36),0,110,juce::Colour(0xff1e2722),0,611,false));
        g.fillRoundedRectangle(16,110,808,501,16);
        g.setColour(gold.withAlpha(.2f)); g.drawRoundedRectangle({17.f,111.f,806.f,499.f},15,1);
        g.setColour(muted); g.setFont(juce::FontOptions(11.f));
        g.drawText("FIXED LEARNED ADJUSTMENT",44,121,752,24,juce::Justification::centred);
        g.setColour(cream.withAlpha(.12f)); g.drawHorizontalLine(426,44,796); g.drawHorizontalLine(543,44,796);
        g.setColour(ink.withAlpha(.65f)); g.setFont(juce::FontOptions(10.f));
        g.drawText("BS.1770  /  STEREO  /  FLOAT & DOUBLE",32,620,480,20,juce::Justification::left);
        g.drawText("NO LIMITER  /  ZERO ADDED LATENCY",490,620,314,20,juce::Justification::right);
    }
private:
    void refresh() {
        const bool learned=processor.learned.load(),learning=processor.learning.load();
        const float db=processor.trimDb.load();
        trim.setText(learned?(db>=0?juce::String("+"):juce::String{})+juce::String(db,1)+" dB":"MATCH  -- dB",juce::dontSendNotification);
        measurement.setText(learned?"Original "+juce::String(processor.inputLufs.load(),1)+" LUFS    /    Processed "+juce::String(processor.processedLufs.load(),1)+" LUFS":"Learn from a representative section of the track",juce::dontSendNotification);
        learn.setButtonText(learning?"Finish Learning":"Learn Match");
        enabled.setButtonText(enabled.getToggleState()?"Match ON":"Match OFF");
        original.setButtonText(original.getToggleState()?"A: Original":"B: Processed");
        juce::String message;
        if(!processor.referenceAvailable.load()) message="Original sidechain is disabled. Processed audio remains available.";
        else if(learning) message="Learning: "+juce::String(processor.seconds.load(),1)+" / 15 s of audible reference";
        else if(processor.outcome.load()==static_cast<int>(LoudnessMatch::Result::tooShort)) message="Not enough reference audio. Play at least 3 seconds and learn again.";
        else if(processor.outcome.load()==static_cast<int>(LoudnessMatch::Result::noOutput)) message="No measurable processed output. Check the chain and learn again.";
        else if(original.getToggleState()) message="Listening to the original reference";
        else if(learned && !enabled.getToggleState()) message="Listening to processed audio; compensation is disabled";
        else if(learned) message="Fixed learned gain applied; relearn after changing the chain";
        else message="Route the original to the reference input, play audio, then Learn Match";
        if(learned && std::abs(processor.inputLufs.load()-processor.processedLufs.load()-db)>.1f) message+=" | Gain capped at 24 dB";
        if(processor.outputPeak.load()>1.f) message+=" | Output exceeds 0 dBFS";
        status.setText(message,juce::dontSendNotification);
        const bool active=idle<16;
        levels.setLevels(active?processor.referencePeak.load():0.f,active?processor.processedPeak.load():0.f,active?processor.outputPeak.load():0.f);
        route.setText(!active?"Play audio to check the original reference":processor.referencePeak.load()>1.e-6f?"REFERENCE SIGNAL PRESENT  /  Channels selected in the host pin connector":"NO REFERENCE SIGNAL  /  Check the sender and the host pin connector",juce::dontSendNotification);
    }
    void timerCallback() override {
        const auto seq=processor.sequence.load(); idle=seq==lastSequence?juce::jmin(idle+1,16):0; lastSequence=seq;
        if(sizes.getSelectedId()!=processor.uiSize.load()) {
            sizes.setSelectedId(processor.uiSize.load(),juce::dontSendNotification); if(sizeChanged) sizeChanged();
        }
        refresh();
    }
    MatchProcessor& processor;
    juce::TextButton learn,reset,enabled,original;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> enableAttachment,originalAttachment;
    juce::ComboBox sizes;
    juce::Label trim,measurement,status,route;
    LevelDisplay levels;
    juce::TooltipWindow tooltip{this,600};
    unsigned lastSequence=0;
    int idle=0;
};

class MatchEditor final : public juce::AudioProcessorEditor {
public:
    explicit MatchEditor(MatchProcessor& p):AudioProcessorEditor(p),processor(p),panel(p) {
        setResizable(false,false); addAndMakeVisible(panel); panel.sizeChanged=[this] { applySize(); }; applySize();
    }
private:
    void applySize() {
        const float scale=processor.uiSize.load()==1?.75f:processor.uiSize.load()==3?1.25f:1.f;
        panel.setTransform(juce::AffineTransform::scale(scale)); panel.setBounds(0,0,840,650);
        setSize(juce::roundToInt(840*scale),juce::roundToInt(650*scale));
    }
    MatchProcessor& processor;
    MatchPanel panel;
};
juce::AudioProcessorEditor* MatchProcessor::createEditor() { return new MatchEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MatchProcessor; }
