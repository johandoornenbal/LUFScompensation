#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>
#include "../Source/ReferenceProcessor.h"
#include <iostream>
#include <stdexcept>

void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
struct Harness {
    std::unique_ptr<juce::AudioProcessor> p{createPluginFilter()};
    juce::MidiBuffer midi;
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    double rate=48000;
    int frame=0;
    void prepare(double r=48000,bool useDouble=false) {
        rate=r; frame=0;
        p->setProcessingPrecision(useDouble?juce::AudioProcessor::doublePrecision:juce::AudioProcessor::singlePrecision);
        p->setRateAndBufferSizeDetails(rate,1025); p->prepareToPlay(rate,1025);
    }
    juce::Component& panel() {
        if(!editor) editor.reset(p->createEditor());
        return *editor->getChildComponent(0);
    }
    juce::Button& button(const char* id) {
        auto* b=dynamic_cast<juce::Button*>(panel().findChildWithID(id)); check(b!=nullptr,"Missing button"); return *b;
    }
    void click(const char* id) { button(id).onClick(); }
    juce::MemoryBlock save() { juce::MemoryBlock b; p->getStateInformation(b); return b; }
    std::unique_ptr<juce::XmlElement> xml() {
        auto b=save(); return juce::AudioProcessor::getXmlFromBinary(b.getData(),static_cast<int>(b.getSize()));
    }
    template<class T> juce::AudioBuffer<T> signal(int samples,double ratio,bool silent=false) {
        juce::AudioBuffer<T> b(p->getTotalNumInputChannels(),samples);
        b.clear();
        for(int i=0;i<samples;++i) for(int ch=0;ch<2;++ch) {
            const T ref=static_cast<T>(silent?0.:.12*std::sin((frame+i)*2*juce::MathConstants<double>::pi*(ch?213.:997.)/rate));
            b.setSample(ch,i,static_cast<T>(ref*ratio));
            if(b.getNumChannels()==4) b.setSample(ch+2,i,ref);
        }
        frame+=samples; return b;
    }
    template<class T> void feed(double duration,double ratio,bool silent=false) {
        int remaining=static_cast<int>(rate*duration);
        const int sizes[]{1,257,1025,64,511}; int block=0;
        while(remaining>0) {
            const int n=std::min(remaining,sizes[block++%5]); auto b=signal<T>(n,ratio,silent);
            p->processBlock(b,midi); remaining-=n;
        }
    }
    template<class T> void compare(double ratio,double expectedGain,bool original=false) {
        auto b=signal<T>(1025,ratio); juce::AudioBuffer<T> before; before.makeCopyOf(b);
        p->processBlock(b,midi);
        for(int i=0;i<b.getNumSamples();++i) for(int ch=0;ch<2;++ch) {
            const double expected=original?static_cast<double>(before.getSample(ch+2,i)):before.getSample(ch,i)*expectedGain;
            check(std::abs(b.getSample(ch,i)-expected)<2.e-7,"Unexpected compensated/A-B output");
            if(before.getNumChannels()==4) check(b.getSample(ch+2,i)==before.getSample(ch+2,i),"Reference input overwritten");
        }
    }
};

void transparency() {
    for(bool useDouble:{false,true}) {
        Harness h; h.prepare(48000,useDouble);
        check(h.p->getTotalNumInputChannels()==4 && h.p->getTotalNumOutputChannels()==2,"Incorrect sidechain buses");
        auto run=[&](auto value) {
            using T=decltype(value);
            for(int count:{0,1,257,1025}) {
                auto b=h.signal<T>(count,2); juce::AudioBuffer<T> expected; expected.makeCopyOf(b);
                h.p->processBlock(b,h.midi);
                for(int ch=0;ch<4;++ch) for(int i=0;i<count;++i)
                    check(b.getSample(ch,i)==expected.getSample(ch,i),"Unlearned processor is not bit-transparent");
            }
        };
        if(useDouble) run(double{}); else run(float{});
    }
}
void learningAndRecall() {
    for(double rate:{44100.,48000.,96000.,192000.}) {
        Harness h; h.prepare(rate); h.click("learnMatch"); h.feed<float>(16,2);
        auto xml=h.xml();
        check(xml && xml->getBoolAttribute("learned"),"Automatic learning failed");
        check(std::abs(xml->getDoubleAttribute("trimDb")+6.0206)<.001,"Incorrect 2x learned adjustment");
        h.compare<float>(2,.5);
        // Fixed gain remains fixed when the chain's level changes.
        h.compare<float>(3,.5);
        h.p->getParameters()[0]->setValueNotifyingHost(0); h.feed<float>(.05,2); h.compare<float>(2,1);
        check(h.xml()->getBoolAttribute("learned"),"Match off lost learned gain");
        h.p->getParameters()[0]->setValueNotifyingHost(1); h.feed<float>(.05,2);
        h.p->getParameters()[1]->setValueNotifyingHost(1); h.feed<float>(.02,2); h.compare<float>(2,1,true);
        h.p->getParameters()[1]->setValueNotifyingHost(0); h.feed<float>(.02,2); h.compare<float>(2,.5);
        // Remeasure before compensation, with no cumulative feedback.
        h.click("learnMatch"); h.feed<float>(4,2); h.click("learnMatch"); h.feed<float>(.05,2);
        check(std::abs(h.xml()->getDoubleAttribute("trimDb")+6.0206)<.001,"Relearning fed back previous gain");
        auto saved=h.save(); Harness restored;
        restored.p->setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
        restored.prepare(rate,true); restored.compare<double>(2,.5);
        restored.p->releaseResources();
        auto noReference=restored.p->getBusesLayout(); noReference.inputBuses.set(1,juce::AudioChannelSet::disabled());
        check(restored.p->setBusesLayout(noReference),"Cannot disable recalled sidechain");
        restored.p->getParameters()[1]->setValueNotifyingHost(1);
        restored.prepare(rate,true); restored.compare<double>(2,.5);
        // Host bypass must pass the processed chain unchanged, even after learning.
        auto bypass=h.signal<float>(1025,2); juce::AudioBuffer<float> before; before.makeCopyOf(bypass);
        h.p->processBlockBypassed(bypass,h.midi);
        for(int ch=0;ch<4;++ch) for(int i=0;i<1025;++i)
            check(bypass.getSample(ch,i)==before.getSample(ch,i),"Host bypass changed audio");
        h.click("resetMatch"); h.feed<float>(.05,2); h.compare<float>(2,1);
        check(!h.xml()->getBoolAttribute("learned"),"Reset did not clear match");
    }
    Harness quiet; quiet.prepare(); quiet.click("learnMatch"); quiet.feed<float>(16,.5);
    check(std::abs(quiet.xml()->getDoubleAttribute("trimDb")-6.0206)<.001,"Positive adjustment incorrect");
    quiet.compare<float>(.5,2);
    // Save a real learned state for the UI preview.
    quiet.editor.reset(quiet.p->createEditor());
    auto* label=dynamic_cast<juce::Label*>(quiet.panel().findChildWithID("matchReadout"));
    check(label && label->getText().contains("+6.0"),"Reopened UI did not show learned gain");
    juce::Image image(juce::Image::ARGB,quiet.editor->getWidth(),quiet.editor->getHeight(),true);
    juce::Graphics graphics(image); quiet.editor->paintEntireComponent(graphics,true);
    auto stream=juce::File::getCurrentWorkingDirectory().getChildFile("ui-preview.png").createOutputStream();
    check(stream!=nullptr,"Cannot save preview"); stream->setPosition(0); stream->truncate();
    check(juce::PNGImageFormat().writeImageToStream(image,*stream),"Cannot render editor");
}
void failedLearningAndMissingReference() {
    Harness h; h.prepare(); h.click("learnMatch"); h.feed<float>(16,2,true);
    check(!h.xml()->getBoolAttribute("learned"),"Silence generated learned gain");
    h.click("learnMatch"); h.feed<float>(.05,2,true);
    check(!h.xml()->getBoolAttribute("learned"),"Silent finish generated learned gain");
    h.click("learnMatch"); h.feed<float>(1,2); h.click("learnMatch"); h.feed<float>(.05,2);
    check(!h.xml()->getBoolAttribute("learned"),"Short learn accepted");
    h.click("learnMatch"); h.feed<float>(4,0); h.click("learnMatch"); h.feed<float>(.05,0);
    check(!h.xml()->getBoolAttribute("learned"),"Muted chain generated infinite gain");
    h.click("learnMatch"); h.feed<float>(4,.03); h.click("learnMatch"); h.feed<float>(.05,.03);
    check(h.xml()->getDoubleAttribute("trimDb")==24,"Gain limit failed");
    // Disabling the auxiliary bus must never mute the main input or guess a reference.
    Harness missing; auto layout=missing.p->getBusesLayout(); layout.inputBuses.set(1,juce::AudioChannelSet::disabled());
    check(missing.p->setBusesLayout(layout),"Optional sidechain could not be disabled");
    missing.prepare(); missing.p->getParameters()[1]->setValueNotifyingHost(1);
    missing.click("learnMatch"); missing.feed<float>(16,2); missing.compare<float>(2,1);
    check(!missing.xml()->getBoolAttribute("learned"),"Missing reference produced a match");
    auto bad=missing.p->getBusesLayout(); bad.inputBuses.set(0,juce::AudioChannelSet::mono());
    check(!missing.p->checkBusesLayoutSupported(bad),"Unsupported mono layout accepted");
}
void sizesAndState() {
    Harness h; h.prepare();
    check(h.button("resetMatch").getButtonText()=="Reset Match","Reset button has no label");
    for(int id:{1,3,2}) {
        auto* sizes=dynamic_cast<juce::ComboBox*>(h.panel().findChildWithID("uiSize"));
        check(sizes && sizes->getNumItems()==3,"Missing UI sizes");
        sizes->setSelectedId(id,juce::sendNotificationSync);
        const float scale=id==1?.75f:id==3?1.25f:1.f;
        const int width=juce::roundToInt(840*scale),height=juce::roundToInt(650*scale);
        check(h.editor->getWidth()==width && h.editor->getHeight()==height,"Incorrect scaled dimensions");
        for(auto* child:h.panel().getChildren()) {
            check(h.editor->getLocalBounds().contains(h.editor->getLocalArea(&h.panel(),child->getBounds())),"Control outside scaled editor");
            const auto centre=h.editor->getLocalPoint(child,child->getLocalBounds().getCentre());
            check(h.editor->getComponentAt(centre)!=h.editor.get(),"Scaled hit testing failed");
        }
        juce::Image image(juce::Image::ARGB,width,height,true); juce::Graphics g(image); h.editor->paintEntireComponent(g,true);
        auto out=juce::File::getCurrentWorkingDirectory().getChildFile("ui-size-"+juce::String(id)+".png").createOutputStream();
        check(out!=nullptr,"Cannot save size preview"); out->setPosition(0); out->truncate();
        check(juce::PNGImageFormat().writeImageToStream(image,*out),"Cannot render size preview");
        auto saved=h.save(); Harness recalled; recalled.p->setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
        recalled.panel(); check(recalled.editor->getWidth()==width && recalled.editor->getHeight()==height,"Size not recalled");
        h.editor.reset(); h.panel(); check(h.editor->getWidth()==width && h.editor->getHeight()==height,"Size lost on reopening");
    }
    auto xml=h.xml(); xml->setAttribute("trimDb",1000.); xml->setAttribute("learned",true); xml->setAttribute("uiSize",999);
    juce::MemoryBlock malformed; juce::AudioProcessor::copyXmlToBinary(*xml,malformed);
    h.p->setStateInformation(malformed.getData(),static_cast<int>(malformed.getSize()));
    check(h.xml()->getDoubleAttribute("trimDb")==24 && h.xml()->getIntAttribute("uiSize")==3,"Malformed state not bounded");
}
// Model the host's configurable pin mapping, including reference on track 5/6.
void referenceToMatcherChain() {
    for(int pair:{2,4}) for(bool useDouble:{false,true}) {
        ReferenceProcessor sender; Harness receiver;
        sender.setProcessingPrecision(useDouble?juce::AudioProcessor::doublePrecision:juce::AudioProcessor::singlePrecision);
        sender.prepareToPlay(48000,257); receiver.prepare(48000,useDouble); receiver.click("learnMatch");
        auto run=[&](auto sampleType) {
            using T=decltype(sampleType);
            for(int block=0;block<3000;++block) {
                juce::AudioBuffer<T> sent(4,257),track(6,257),received(4,257);
                sent.clear(); track.clear();
                for(int i=0;i<257;++i) for(int ch=0;ch<2;++ch)
                    sent.setSample(ch,i,static_cast<T>(.12*std::sin((block*257+i)*(.13+ch*.03))));
                sender.processBlock(sent,receiver.midi);
                for(int ch=0;ch<2;++ch) {
                    // The host maps Reference's auxiliary output to a selected track pair.
                    track.copyFrom(pair+ch,0,sent,ch+2,0,257);
                    track.copyFrom(ch,0,sent,ch,0,257); track.applyGain(ch,0,257,static_cast<T>(2));
                    // The host maps that track pair to Compensation's reference input.
                    received.copyFrom(ch,0,track,ch,0,257);
                    received.copyFrom(ch+2,0,track,pair+ch,0,257);
                }
                receiver.p->processBlock(received,receiver.midi);
                if(block==2999) for(int ch=0;ch<2;++ch) for(int i=0;i<257;++i)
                    check(std::abs(received.getSample(ch,i)-sent.getSample(ch,i))<2.e-7,"Reference-to-matcher chain did not restore loudness");
            }
        };
        if(useDouble) run(double{}); else run(float{});
        auto learned=receiver.xml();
        check(learned->getBoolAttribute("learned") && std::abs(learned->getDoubleAttribute("trimDb")+6.0206)<.001,"End-to-end chain learned the wrong gain");
    }
}
int main() try {
    juce::ScopedJuceInitialiser_GUI init;
    referenceToMatcherChain(); transparency(); learningAndRecall(); failedLearningAndMissingReference(); sizesAndState();
    std::cout<<"Stereo sidechain routing, float/double transparency, fixed LUFS gain at four rates, relearn, A/B, bypass, reset, failures, state and all UI sizes passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
