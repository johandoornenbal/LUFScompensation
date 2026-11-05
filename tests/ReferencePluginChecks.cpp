#include <juce_audio_utils/juce_audio_utils.h>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
template<class T> void copyChecks(juce::AudioProcessor& p,double rate) {
    p.setProcessingPrecision(std::is_same_v<T,float>?juce::AudioProcessor::singlePrecision:juce::AudioProcessor::doublePrecision);
    p.setRateAndBufferSizeDetails(rate,1025); p.prepareToPlay(rate,1025);
    juce::MidiBuffer midi;
    for(int count:{0,1,64,257,1025}) {
        juce::AudioBuffer<T> b(4,count),original(2,count);
        for(int i=0;i<count;++i) for(int ch=0;ch<2;++ch) {
            T sample=static_cast<T>(.4*std::sin(i*.071+ch));
            if(i==0) sample=-T{};
            if(i==1) sample=std::numeric_limits<T>::denorm_min();
            b.setSample(ch,i,sample); original.setSample(ch,i,sample);
            b.setSample(ch+2,i,T{.75});
        }
        p.processBlock(b,midi);
        for(int ch=0;ch<2;++ch) if(count>0) {
            const auto bytes=static_cast<size_t>(count)*sizeof(T);
            require(std::memcmp(b.getReadPointer(ch),original.getReadPointer(ch),bytes)==0,"Main audio changed");
            require(std::memcmp(b.getReadPointer(ch+2),original.getReadPointer(ch),bytes)==0,"Reference is not an exact sample copy");
        }
        p.processBlockBypassed(b,midi);
        for(int ch=0;ch<2;++ch) for(int i=0;i<count;++i) {
            require(b.getSample(ch,i)==original.getSample(ch,i),"Bypass changed main input");
            require(b.getSample(ch+2,i)==T{},"Bypass left a stale reference");
        }
    }
    p.releaseResources();
}
void routing() {
    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
    require(p->getName()=="LUFS Reference" && p->getParameters().isEmpty(),"Incorrect reference plugin identity/parameters");
    require(p->getTotalNumInputChannels()==2 && p->getTotalNumOutputChannels()==4,"Incorrect reference buses");
    require(p->getBus(false,1)->getName()=="Original reference","Missing named reference output");
    require(p->supportsDoublePrecisionProcessing() && p->getLatencySamples()==0,"Reference precision/latency incorrect");
    for(double rate:{44100.,48000.,96000.,192000.}) { copyChecks<float>(*p,rate); copyChecks<double>(*p,rate); }
    auto disabled=p->getBusesLayout(); disabled.outputBuses.set(1,juce::AudioChannelSet::disabled());
    require(p->setBusesLayout(disabled),"Cannot disable auxiliary output");
    p->setProcessingPrecision(juce::AudioProcessor::doublePrecision); p->prepareToPlay(48000,257);
    juce::AudioBuffer<double> audio(2,257); audio.clear();
    for(int i=0;i<257;++i) { audio.setSample(0,i,.25); audio.setSample(1,i,-.125); }
    juce::MidiBuffer midi; p->processBlock(audio,midi);
    for(int i=0;i<257;++i) require(audio.getSample(0,i)==.25 && audio.getSample(1,i)==-.125,"Disabled reference corrupted main audio");
    auto bad=disabled; bad.outputBuses.set(0,juce::AudioChannelSet::mono());
    require(!p->checkBusesLayoutSupported(bad),"Unsupported output layout accepted");
}
void editorAndState() {
    std::unique_ptr<juce::AudioProcessor> p(createPluginFilter());
    std::unique_ptr<juce::AudioProcessorEditor> editor(p->createEditor());
    require(editor->getWidth()==840 && editor->getHeight()==360,"Wrong default editor size");
    for(int id:{1,3,2}) {
        auto* panel=editor->getChildComponent(0);
        auto* sizes=dynamic_cast<juce::ComboBox*>(panel->findChildWithID("uiSize"));
        require(sizes && sizes->getNumItems()==3,"Missing reference UI sizes");
        sizes->setSelectedId(id,juce::sendNotificationSync);
        const float scale=id==1?.75f:id==3?1.25f:1.f;
        const int width=juce::roundToInt(840*scale),height=juce::roundToInt(360*scale);
        require(editor->getWidth()==width && editor->getHeight()==height,"Incorrect reference size");
        require(editor->getLocalBounds().contains(editor->getLocalArea(panel,sizes->getBounds())),"Size menu outside editor");
        juce::Image image(juce::Image::ARGB,width,height,true); juce::Graphics g(image); editor->paintEntireComponent(g,true);
        const auto name=id==2?juce::String("reference-preview.png"):"reference-size-"+juce::String(id)+".png";
        auto stream=juce::File::getCurrentWorkingDirectory().getChildFile(name).createOutputStream();
        require(stream!=nullptr,"Cannot save reference preview"); stream->setPosition(0); stream->truncate();
        require(juce::PNGImageFormat().writeImageToStream(image,*stream),"Cannot render reference UI");
        juce::MemoryBlock saved; p->getStateInformation(saved);
        std::unique_ptr<juce::AudioProcessor> recalled(createPluginFilter());
        recalled->setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
        std::unique_ptr<juce::AudioProcessorEditor> other(recalled->createEditor());
        require(other->getWidth()==width && other->getHeight()==height,"Size lost on recall");
        editor.reset(); editor.reset(p->createEditor());
        require(editor->getWidth()==width && editor->getHeight()==height,"Size lost on reopening");
    }
    juce::XmlElement bad("LUFSReference"); bad.setAttribute("uiSize",999); juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary(bad,data); p->setStateInformation(data.getData(),static_cast<int>(data.getSize()));
    editor.reset(); editor.reset(p->createEditor()); require(editor->getWidth()==1050,"Size state not bounded");
}
int main() try {
    juce::ScopedJuceInitialiser_GUI init; routing(); editorAndState();
    std::cout<<"Reference stereo buses, exact float/double copies at four rates, variable blocks, bypass, disabled output and all UI sizes/recall passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
