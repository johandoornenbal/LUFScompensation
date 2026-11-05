#include "MeterDisplay.h"
#include <iostream>
#include <stdexcept>
void require(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
int main() try {
    MeterBallistics meter;
    require(meter.advance(1.f,1./30)<-40.f,"Meter jumps immediately to a transient");
    for(int i=0;i<90;++i) meter.advance(.5f,1./30);
    require(std::abs(meter.getLevelDb()+6.0206f)<.05f,"Steady level not calibrated");
    const auto before=meter.getLevelDb(); meter.advance(0,1./30);
    require(meter.getLevelDb()<before && meter.getLevelDb()>before-5.f,"Meter fall is abrupt");
    for(int i=0;i<180;++i) meter.advance(0,1./30);
    require(meter.getLevelDb()==-60.f,"Stopped meter does not settle");
    MeterBallistics regular,jittered;
    for(int i=0;i<60;++i) regular.advance(.5f,1./60);
    for(int i=0;i<20;++i) { jittered.advance(.5f,.01); jittered.advance(.5f,.04); }
    require(std::abs(regular.getLevelDb()-jittered.getLevelDb())<1.e-4f,"Meter depends on timer cadence");
    MeterBallistics alternating; alternating.resetPeak(.5f);
    float low=0,high=-60;
    for(int i=0;i<240;++i) {
        const float db=alternating.advance(i%2==0?.25f:1.f,1./60);
        if(i>120) { low=std::min(low,db); high=std::max(high,db); }
    }
    require(high-low<2.f,"Rapid peaks are not averaged");
    DisplayPeak peak; peak.capture(.1f); peak.capture(.9f); peak.capture(.2f);
    require(peak.take()==.9f,"Peak between redraws lost");
    require(peak.take()==0.f,"Peak not consumed");
    peak.capture(.8f); peak.reset(); require(peak.take()==0.f,"Peak reset failed");
    std::cout<<"Meter rise/fall, jitter independence, rapid-peak averaging and inter-redraw peak retention passed\n";
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
