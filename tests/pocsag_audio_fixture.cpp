#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include "headers/pdw.h"
#include "headers/sound_in.h"
#include "pocsag_test_environment.h"
#include "utils/audio_signal_core.h"
#include "utils/decoder_audio_adapter.h"

extern PROFILE Profile;
extern int pocsag_baud_rate, pocbit, sync_bit;
extern char atb_bit;

void SignalDecoderStateEnter() {}
void SignalDecoderStateLeave() {}

namespace pdw_test { namespace {
const double COURSE_CLKT_HI=2.02, COURSE_CLKT_LO=.18, FINE_CLKT_HI=1.90,
    FINE_CLKT_LO=.32, FINE_CLKT3200_HI=1.92, FINE_CLKT3200_LO=.34;
POCSAG pocsag;
struct MobitexSink { int bitrate=1600; } mb;
int au_threshold[10]={0,1,2,5,9,14,17,24,30,44};
int au_offset_center[10]={0,1,-1,2,-2,3,-3,4,-4,5};
int val=0, nSamples=0, preamble_count[3]={}, flex_cnt_1600=0, crossing=0;
int pre_threshold=0, atb_value=0, atb_len=0, atb_sig_cnt=0;
int atb_center[5]={}, atb_threshold[5]={}, atb_sample_offset[5]={};
int config_index=INDEX1600, cross_over=0, skipped_sc=0, flex_timer=0;
int g_sps2=1600, level=2, process_acars_bit=0, g_modernCaptureKind=3;
long atb_ctr=0, BaudRate=1600, last_baud_rate=0;
long double WatchStep=0, clkt_hi=FINE_CLKT_HI, clkt_lo=FINE_CLKT_LO;
double WatchCtr=-1, exc=0;
bool bMode_IDLE=false, bFlexActive=false;
pdw::signal::AdaptiveSlicer g_enhancedAudioSlicer;
std::uint32_t ActiveAudioSampleRate() { return pdw::signal::DecoderAudioAdapter::ReferenceRate; }
void UpdateSigInd(int) {}
void display_showmo(int) {}
void frame_flex(char) {}
#include "slicer.inc"
}

std::vector<unsigned char> SyntheticPocsagIq(const std::vector<int>& bits, unsigned baud, bool reversePolarity)
{
    constexpr unsigned iqRate=1024000, preambleBits=576, tailBits=16;
    const std::size_t samples=((preambleBits+bits.size()+tailBits)*std::uint64_t(iqRate)+baud-1)/baud;
    const double radians=2.0*3.14159265358979323846*4500/iqRate;
    const double cosine=std::cos(radians), sine=std::sin(radians);
    double inPhase=1.0, quadrature=0.0;
    std::vector<unsigned char> iq(samples*2);
    for (std::size_t index=0;index<samples;++index) {
        const std::size_t symbol=index*std::uint64_t(baud)/iqRate;
        const bool value=symbol<preambleBits || symbol>=preambleBits+bits.size() ?
            (symbol%2)!=0 : bits[symbol-preambleBits]!=0;
        const double step=value!=reversePolarity?sine:-sine;
        const double next=inPhase*cosine-quadrature*step;
        quadrature=quadrature*cosine+inPhase*step; inPhase=next;
        iq[index*2]=static_cast<unsigned char>(128+60*inPhase);
        iq[index*2+1]=static_cast<unsigned char>(128+60*quadrature);
    }
    return iq;
}

void RunPocsagAudio(const std::vector<int>& bits, unsigned baud, unsigned rate,
    std::size_t callbackBytes, bool reversePolarity)
{
    ResetPocsagEnvironment();
    Profile.decodepocsag=Profile.decodeflex=1;
    Profile.pocsag_512=Profile.pocsag_1200=Profile.pocsag_2400=1;
    Profile.rtlSignalConditionerEnabled=1;
    pocsag.frame(-1);
    SetAudioConfig(5);
    Reset_ATB();
    const auto iq=SyntheticPocsagIq(bits,baud,reversePolarity);
    pdw::signal::RtlSignalConditioner conditioner(1024000,rate,12500);
    pdw::signal::DecoderAudioAdapter adapter;
    std::vector<float> audio,reference;
    std::vector<char> pcm;
    for (std::size_t start=0;start<iq.size();start+=callbackBytes) {
        conditioner.ProcessUnsignedIq(iq.data()+start,(std::min)(callbackBytes,iq.size()-start),audio);
        if (audio.empty()) continue;
        bool reset=false;
        if (!adapter.Process(audio.data(),audio.size(),rate,false,reference,reset))
            throw std::runtime_error("POCSAG audio adapter rejected fixture rate");
        if (reset) Reset_ATB();
        pcm.clear();
        for (float sample:reference) pcm.push_back(static_cast<char>((std::max)(0,(std::min)(255,static_cast<int>(128*sample+128)))));
        if (!pcm.empty()) Audio_To_Bits(pcm.data(),static_cast<long>(pcm.size()));
    }
}
}
