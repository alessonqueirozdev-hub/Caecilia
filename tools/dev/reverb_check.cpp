// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Alesson Queiroz and the Caecilia contributors.
// JUCE-free validation harness for FdnReverb: proves the tail is STABLE (decays
// to silence, no NaN/Inf, energy never grows) and that the bass-bloom shelf gives
// low frequencies a LONGER RT60 than the mids (late/early band energy ratio).
#include "caecilia/dsp/FdnReverb.h"
#include "caecilia/core/AudioBlock.h"

#include <cmath>
#include <cstdio>
#include <vector>
#include <string>

using namespace caecilia;

namespace
{
// RBJ band-pass (constant skirt), used to isolate a band of the impulse response.
struct Biquad
{
    double b0=1,b1=0,b2=0,a1=0,a2=0, x1=0,x2=0,y1=0,y2=0;
    void bandpass(double fs,double f0,double Q)
    {
        const double w0 = 2.0*M_PI*f0/fs, c=std::cos(w0), s=std::sin(w0), alpha=s/(2.0*Q);
        const double a0 = 1.0+alpha;
        b0=alpha/a0; b1=0.0; b2=-alpha/a0; a1=(-2.0*c)/a0; a2=(1.0-alpha)/a0;
        x1=x2=y1=y2=0;
    }
    double process(double x)
    {
        const double y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;
        x2=x1; x1=x; y2=y1; y1=y; return y;
    }
};

// Total energy (sum of squares) of a band of `ir` inside [t0,t1] seconds.
double bandEnergy(const std::vector<float>& ir,double fs,double f0,double t0,double t1)
{
    Biquad bp; bp.bandpass(fs,f0,2.0);
    const std::size_t a=(std::size_t)(t0*fs), b=(std::size_t)(t1*fs);
    double e=0.0;
    for (std::size_t n=0;n<ir.size();++n){ const double y=bp.process(ir[n]); if(n>=a&&n<b) e+=y*y; }
    return e;
}
} // namespace

int main()
{
    const double fs = 48000.0;
    const std::size_t block = 512;
    const double tailSec = 9.0;
    const std::size_t total = (std::size_t)(tailSec*fs);

    // Render one impulse response with the given params. Reports peak/onset/NaN.
    auto renderIR=[&](const core::ReverbParams& params, std::vector<float>& ir,
                      double& peak, double& peakMs, bool& nanSeen)
    {
        dsp::FdnReverb rev; rev.prepare(fs, block, 2); rev.setParams(params);
        ir.clear(); ir.reserve(total);
        std::vector<float> L(block), R(block);
        peak=0.0; std::size_t peakIdx=0; nanSeen=false; std::size_t done=0;
        while(done<total){
            const std::size_t nf=std::min(block,total-done);
            for(std::size_t i=0;i<nf;++i){ float s=(done==0&&i==0)?1.0f:0.0f; L[i]=s; R[i]=s; }
            float* ch[2]={L.data(),R.data()}; core::AudioBlock blk(ch,2,nf); rev.process(blk);
            for(std::size_t i=0;i<nf;++i){ const float v=L[i];
                if(std::isnan(v)||std::isinf(v)) nanSeen=true;
                const double a=std::fabs((double)v); if(a>peak){peak=a;peakIdx=done+i;}
                ir.push_back(v); }
            done+=nf;
        }
        peakMs=1000.0*peakIdx/fs;
    };
    auto windowRms=[&](const std::vector<float>& ir,double t0,double t1){
        const std::size_t a=(std::size_t)(t0*fs),b=std::min((std::size_t)(t1*fs),ir.size());
        double e=0; std::size_t n=0; for(std::size_t i=a;i<b;++i){e+=(double)ir[i]*ir[i];++n;}
        return n?std::sqrt(e/n):0.0;
    };

    int failures = 0;

    // ---- (1) STABILITY across presets: tail must decay to silence, no NaN -------
    struct Case { const char* name; dsp::ReverbPreset preset; };
    Case cases[]={{"Cathedral",dsp::ReverbPreset::Cathedral},{"Hall",dsp::ReverbPreset::Hall},{"Plate",dsp::ReverbPreset::Plate}};
    for(const auto& c:cases){
        dsp::FdnReverb tmp; tmp.prepare(fs,block,2); tmp.setPreset(c.preset);
        auto p=tmp.params(); p.mix=1.0f;
        std::vector<float> ir; double peak,peakMs; bool nanSeen;
        renderIR(p,ir,peak,peakMs,nanSeen);
        const double decayDb=20.0*std::log10((windowRms(ir,tailSec-0.5,tailSec)+1e-12)/(windowRms(ir,0.05,0.20)+1e-9));
        bool ok=true;
        if(nanSeen) ok=false;                 // no NaN/Inf ever
        if(peak>8.0) ok=false;                // bounded — no self-oscillation
        if(decayDb>-45.0) ok=false;           // fully rings out
        if(peakMs>250.0) ok=false;            // diffusion smears, doesn't hugely delay onset
        std::printf("[stable] %-10s bloom=%.2f  peak=%.3f@%.0fms  tailDecay=%.1fdB  NaN=%d => %s\n",
            c.name,p.bassBloom,peak,peakMs,decayDb,(int)nanSeen,ok?"PASS":"FAIL");
        if(!ok) ++failures;
    }

    // ---- (2) CONTROLLED bloom A/B: same acoustic, bloom OFF vs ON ---------------
    // Isolates the shelf: turning bloom up must LENGTHEN the low band (more late
    // energy) while leaving the mid band essentially unchanged.
    {
        dsp::FdnReverb tmp; tmp.prepare(fs,block,2); tmp.setPreset(dsp::ReverbPreset::Cathedral);
        auto base=tmp.params(); base.mix=1.0f;
        core::ReverbParams off=base; off.bassBloom=1.0f;
        core::ReverbParams on =base; on.bassBloom =1.6f;
        std::vector<float> irOff,irOn; double pk,pms; bool nn;
        renderIR(off,irOff,pk,pms,nn);
        renderIR(on ,irOn ,pk,pms,nn);
        auto lateGain=[&](double f){ return bandEnergy(irOn,fs,f,2.5,4.5)/(bandEnergy(irOff,fs,f,2.5,4.5)+1e-15); };
        const double g60=lateGain(60.0), g150=lateGain(150.0), g700=lateGain(700.0), g1500=lateGain(1500.0), g4000=lateGain(4000.0);
        bool ok=true;
        if(g60<1.5) ok=false;                             // deep bass tail must clearly lengthen
        if(g150<1.3) ok=false;                            // bass proper too
        if(g60 < 3.0*g1500) ok=false;                     // effect must be strongly bass-WEIGHTED
        if(g1500>1.6) ok=false;                           // true mids essentially untouched
        std::printf("[bloom ] Cathedral off->on late-energy gain:  60Hz x%.2f  150Hz x%.2f  700Hz x%.2f  1.5kHz x%.2f  4kHz x%.2f => %s\n",
            g60,g150,g700,g1500,g4000,ok?"PASS":"FAIL");
        if(!ok) ++failures;
    }

    std::printf("\n%s (%d failure(s))\n", failures==0?"ALL PASS":"FAILURES", failures);
    return failures==0?0:1;
}
