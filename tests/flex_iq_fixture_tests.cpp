#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>
#include "utils/audio_signal_core.h"
#include "utils/rtl_signal_conditioner.h"
#include "utils/decoder_audio_adapter.h"
#include "utils/flex_fragment_reassembly_core.h"

// Native presentation, timers and unrelated protocol sinks are replaced here.
// CMake extracts acquisition, deinterleaving and BCH bodies from current source.
// These fixtures contain generated codewords only: no radio capture or traffic.
enum { INDEX512, INDEX1200, INDEX2400, INDEX1600, INDEX3200 };
enum { MODE_POCSAG, MODE_P512, MODE_P1200, MODE_P2400, MODE_IDLE,
       STAT_POCSAG512, STAT_POCSAG1200, STAT_POCSAG2400 };
enum { STAT_FLEX1600, STAT_FLEX3200, STAT_FLEX6400 };
enum { MODE_FLEX_A=1, MODE_FLEX_B=2, MODE_FLEX_C=4, MODE_FLEX_D=8, COLOR_MISC=7 };
enum { MODE_SECURE, MODE_SHORT_INSTRUCTION, MODE_SH_TONE, MODE_STNUM,
       MODE_SFNUM, MODE_ALPHA, MODE_BINARY, MODE_NUNUM };
enum { MSG_CAPCODE, MSG_TIME, MSG_DATE, MSG_MODE, MSG_TYPE, MSG_BITRATE };
enum { COLOR_ADDRESS, COLOR_TIMESTAMP, COLOR_MODETYPEBIT, COLOR_BITERRORS,
       COLOR_MESSAGE, COLOR_INSTRUCTIONS, COLOR_NUMERIC };
const int NUM_STAT=8, STAT_ALPHA=0, STAT_NUMERIC=1, FILTER_CAPCODE_LEN=9;
const int DEFAULT_HI_AUDIO=1, DEFAULT_LO_AUDIO=0;
const int SYNC1=0xA6C6, SYNC2=0xAAAA, EOT1=0xAAAA, EOT2=0xFFFF;
const double COURSE_CLKT_HI=2.02, COURSE_CLKT_LO=.18, FINE_CLKT_HI=1.90,
    FINE_CLKT_LO=.32, FINE_CLKT3200_HI=1.92, FINE_CLKT3200_LO=.34;
struct TestProfile {
    bool rtlSignalConditionerEnabled=true;
    bool monitor_mobitex=false, decodepocsag=true, decodeflex=true;
    bool pocsag_512=true, pocsag_1200=true, pocsag_2400=true, invert=false;
    bool flex_1600=true, flex_3200=true, flex_6400=true, FlexTIME=false;
    bool FlexGroupMode=false, flexFragmentReassemblyEnabled=false, convert_si=false;
    bool shownumeric=true, showtone=true, showinstr=true, showmisc=true;
    int audioThreshold[5]={}, audioResync[5]={}, audioCentering[5]={};
} Profile;
struct PdwSignalDecoderStateGuard {};
struct PocsagSink { int lastInput=-2; void frame(int bit) { lastInput=bit; } } pocsag;
struct MobitexStub { int bitrate=1600; } mb;
int au_threshold[10]={0,1,2,5,9,14,17,24,30,44};
int au_offset_center[10]={0,1,-1,2,-2,3,-3,4,-4,5};
char high_audio=1, low_audio=0, atb_bit=0;
int val=0, nSamples=0, preamble_count[3]={}, flex_cnt_1600=0, sync_bit=0;
int crossing=0, pre_threshold=0, atb_value=0, atb_len=0, atb_sig_cnt=0;
int atb_center[5]={}, atb_threshold[5]={}, atb_sample_offset[5]={};
int config_index=INDEX1600, cross_over=0, skipped_sc=0, pocsag_baud_rate=0;
int pocbit=0, flex_timer=0, g_sps2=1600, level=2, process_acars_bit=0;
long atb_ctr=0, BaudRate=1600, last_baud_rate=0;
long double WatchStep=0, clkt_hi=FINE_CLKT_HI, clkt_lo=FINE_CLKT_LO;
double WatchCtr=0, exc=0;
bool bMode_IDLE=false;
int g_modernCaptureKind=3;
pdw::signal::AdaptiveSlicer g_enhancedAudioSlicer;
std::uint32_t ActiveAudioSampleRate() { return 44100; }
void UpdateSigInd(int) {}
void display_showmo(int) {}
unsigned int bch[1025]={}, ecs[25]={};
char ob[32]={}, phase='A';
int flex_blk=0, flex_bc=0, flex_speed=STAT_FLEX1600, g_sps=1600;
unsigned short flexSyncWords[4]={};
unsigned int flexSyncBitCount=0;
int syncs[8]={0x870C,0x7B18,0xB068,0xDEA0,0,0,0,0x4C7C};
bool bFlexActive=false, bReflex=false, bFLEX_Frame_contains_SI=false;
bool bEmpty_Frame=false, bFlexTIME_detected=true, bFlexTIME_not_used=true;
int iCurrentCycle=0, iCurrentFrame=0, Pane1=0;
double rcver[65]={}, ct1600=1, ct3200=1, ct_bit=1;
double rcv_clkt=1, rcv_clkt_hi=1, rcv_clkt_fl=1;
unsigned errorsByCount[4]={}, headers=0;
void CountBiterrors(int errors) { ++errorsByCount[(std::min)(errors,3)]; }
void CountPagingBchWord(int errors) { CountBiterrors(errors); }
void display_color(int*,int) {}
void display_line(int*) {}
void display_show_strV2(int*,char*) {}
#define display_cfstatus ApplyFixtureFrameStatus
#include "flex-status.inc"
#undef display_cfstatus
void display_cfstatus(int cycle,int frame) { if (cycle>=0 && frame>=0) ++headers; ApplyFixtureFrameStatus(cycle,frame); }
bool messageFixture=false, bFLEX_groupmessage=false;
long capcode=0;
int FLEX_9=0, FlexTempAddress=0, iMessageIndex=0;
unsigned long hourly_stat[NUM_STAT][2]={}, hourly_char[NUM_STAT][2]={};
unsigned long daily_stat[NUM_STAT][2]={}, daily_char[NUM_STAT][2]={};
char Current_MSG[9][128]={}, szWindowText[6][1000]={};
char szCurrentDate[40]="01-01-00", szCurrentTime[40]="00:00:00";
char aNumeric[17]="0123456789*U -][";
char vtype[8][9]={"SECURE "," INSTR ","SH/TONE"," StNUM "," SfNUM "," ALPHA ","BINARY "," NuNUM "};
unsigned char message_buffer[4096]={};
BYTE message_color[4096]={}, messageitems_colors[7]={};
std::vector<std::string> displayedMessages, displayedModes;
pdw::flex::FragmentReassembler g_flexFragmentReassembler;
void flex_fragment_reassembly_reset() { g_flexFragmentReassembler.Reset(); }
void multipart_message_reassembly_reset() {}
std::uint64_t FlexFragmentNowMs() { return 0; }
void Get_Date_Time() {}
void display_show_char(int*,int c) { message_buffer[iMessageIndex++]=static_cast<unsigned char>(c); }
void display_show_str(int*,const char* text) { while(*text) display_show_char(&Pane1,*text++); }
void ShowMessage() {
    if (messageFixture) {
        displayedMessages.emplace_back(reinterpret_cast<char*>(message_buffer),iMessageIndex);
        displayedModes.emplace_back(Current_MSG[MSG_MODE]);
    }
    iMessageIndex=0;
}
void SuppressCurrentMessage() { throw std::runtime_error("Unexpected suppression in standalone fixture"); }
void ShowAssembledFlexCopy(const pdw::flex::FragmentResult&) { throw std::runtime_error("Unexpected assembled copy"); }
void ConvertGroupcall(long,const char*,long) { throw std::runtime_error("Unexpected group call"); }
void AddAssignment(int,int,long) { throw std::runtime_error("Unexpected assignment"); }
void Check4_MissedGroupcalls() {}
struct FLEX {
    char block[256]={}; long frame[200]={};
    void showwordhex(int) {} void showword(int) {}
    void showframe(int a,int v) { if(messageFixture) DecodeMessage(a,v); }
    void DecodeMessage(int,int); void FlexTIME() {}
    int xsumchk(long); void show_address(long,long,bool); void show_phase_speed(int);
    void showblock(int);
} phase_A, phase_B, phase_C, phase_D;

#define ecd ProductionEcd
#include "bch.inc"
#undef ecd
using Word = std::array<int,32>;
std::vector<Word> expectedWords;
size_t receivedWords=0;
unsigned rawBitErrors=0, dataBitErrors=0;
bool compareFixture=false;
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int ecd() {
    if (!compareFixture) return ProductionEcd();
    Require(receivedWords < expectedWords.size(), "Unexpected decoded word");
    const Word& expected=expectedWords[receivedWords++];
    for (int b=0;b<32;++b) rawBitErrors += ob[b]!=expected[b];
    const int result=ProductionEcd();
    // The production corrector repairs information bits, not parity bits.
    for (int b=0;b<21;++b) dataBitErrors += ob[b]!=expected[b];
    return result;
}
#include "flex.inc"
#define showframe DecodeMessage
#include "flex-message.inc"
#undef showframe
#include "slicer.inc"

Word Codeword(unsigned value) {
    Word bits={}; unsigned ecc=0, parity=0, contribution=0x3B4;
    for (unsigned i=0;i<21;++i) {
        bits[i]=(value>>i)&1;
        if (bits[i]) { ecc^=contribution; parity^=1; }
        contribution=(contribution&1)?((contribution>>1)^0x3B4):(contribution>>1);
    }
    for (unsigned i=0;i<10;++i) { bits[21+i]=(ecc>>(9-i))&1; parity^=bits[21+i]; }
    bits[31]=parity;
    return bits;
}
void CheckBch() {
    setupecc(); compareFixture=false;
    for (unsigned value : {0u,1u,0x13579u,0x155555u,0x1fffffu}) {
        const Word expected=Codeword(value);
        // Every single-bit and two-bit error location, including parity.
        for (int first=-1;first<32;++first) for (int second=first;second<32;++second) {
            if (first==-1 && second!=-1) continue;
            for (int b=0;b<32;++b) ob[b]=static_cast<char>(expected[b]);
            if (first>=0) ob[first]^=1;
            if (second>first) ob[second]^=1;
            const int errorCount=ProductionEcd();
            if (errorCount!=(first<0?0:second==first?1:2))
                std::cerr<<"Synthetic BCH value="<<value<<" positions="<<first<<','<<second
                    <<" reported="<<errorCount<<std::endl;
            Require(errorCount==(first<0?0:second==first?1:2), "BCH error count mismatch");
            for (int b=0;b<21;++b) Require(ob[b]==expected[b], "BCH data recovery failed");
        }
        for (int a=0;a<32;++a) for (int b=a+1;b<32;++b) for (int c=b+1;c<32;++c) {
            for (int bit=0;bit<32;++bit) ob[bit]=static_cast<char>(expected[bit]);
            ob[a]^=1; ob[b]^=1; ob[c]^=1;
            Require(ProductionEcd()==3, "Three-bit error accepted as correctable");
        }
    }
}
std::string FixtureText(int p) { return std::string("SYNTHETIC PAGER ")+char('A'+p)+": 12345"; }
unsigned WithChecksum(unsigned word) {
    unsigned sum=0;
    for (unsigned shift=4;shift<=20;shift+=4) sum+=(word>>shift)&15;
    return (word&~15u)|((15-sum)&15u);
}
std::array<unsigned,88> MessageFrame(int p) {
    std::array<unsigned,88> frame; frame.fill(0x155555);
    frame[0]=WithChecksum(2u<<10); // one short address, followed by its vector
    frame[1]=32768+123456+p; // synthetic, never sourced from received traffic
    const std::string text=FixtureText(p);
    frame[3]=3u<<11; // complete standalone alpha message
    size_t position=0; int w=4;
    frame[w]=0;
    for (int shift=7;shift<=14;shift+=7) frame[w]|=static_cast<unsigned>(text[position++])<<shift;
    ++w;
    while (position<text.size()) {
        frame[w]=0;
        for (int shift=0;shift<=14;shift+=7)
            frame[w]|=static_cast<unsigned>(position<text.size()?text[position++]:3)<<shift;
        ++w;
    }
    frame[2]=WithChecksum((MODE_ALPHA<<4)|(3u<<7)|(static_cast<unsigned>(w-3)<<14));
    return frame;
}
std::vector<int> Symbols(bool four, bool fast, bool timed) {
    expectedWords.clear(); std::vector<int> symbols;
    auto bit=[&](int b) { symbols.push_back(b?0:3); };
    for (unsigned i=0;i<256;++i) bit(i&1);
    const unsigned header=syncs[(four?2:0)+(fast?1:0)];
    for (unsigned word : {header,0xA6C6u,0xAAAAu,header^0xffffu})
        for (int b=15;b>=0;--b) bit((word>>b)&1);
    for (unsigned i=0;i<16;++i) bit(i&1);
    const Word fiw=Codeword(0x13579); expectedWords.push_back(fiw);
    for (int b:fiw) bit(b);
    // 25 ms Sync-2 timing at the payload symbol rate. Direct-symbol tests
    // use the 40-call holdoff expected by frame_flex; audio tests use 80 at
    // 3200 sps. Alternation is a timing fixture, not a complete Sync-2 code.
    for (unsigned i=0;i<(fast&&timed?80u:40u);++i) bit(i&1);
    std::mt19937 random(554);
    for (int block=0;block<11;++block) {
        Word words[4][8]={};
        for (int p=0;p<4;++p) {
            if ((p%2 && !four) || (p>=2 && !fast)) continue;
            const auto message=MessageFrame(p);
            for (int w=0;w<8;++w) {
                words[p][w]=Codeword(messageFixture?(message[block*8+w]^0x1fffff):
                    ((random()&0x1fffff)|0x100));
                expectedWords.push_back(words[p][w]);
            }
        }
        for (int b=0;b<32;++b) for (int w=0;w<8;++w) for (int p=0;p<(fast?4:2);p+=2) {
            const int a=words[p][w][b], outer=words[p+1][w][b];
            symbols.push_back(four?(a?(outer?0:1):(outer?3:2)):(a?0:3));
        }
    }
    for (unsigned i=0;i<128;++i) bit(i&1);
    return symbols;
}
void ResetFixture(int preset) {
    Profile=TestProfile(); Profile.invert=true; Profile.audioThreshold[INDEX1600]=2;
    high_audio=0; low_audio=1; sync_bit=1; crossing=atb_len=pocbit=flex_timer=0;
    level=2; g_sps=g_sps2=1600; flex_blk=flex_bc=0;
    bReflex=bFlexActive=false; flex_speed=STAT_FLEX1600;
    phase_A=FLEX(); phase_B=FLEX(); phase_C=FLEX(); phase_D=FLEX();
    displayedMessages.clear(); displayedModes.clear(); iMessageIndex=0;
    std::memset(errorsByCount,0,sizeof(errorsByCount));
    headers=rawBitErrors=dataBitErrors=0; receivedWords=0; compareFixture=true;
    setupecc(); SetAudioConfig(preset); flex_reset(); Reset_ATB();
}
void CheckResult(bool requireClean) {
    if (dataBitErrors || rawBitErrors || receivedWords!=expectedWords.size())
        std::cerr<<"Synthetic words="<<receivedWords<<'/'<<expectedWords.size()
            <<" raw errors="<<rawBitErrors<<" data errors="<<dataBitErrors
            <<" error buckets="<<errorsByCount[0]<<'/'<<errorsByCount[1]<<'/'
            <<errorsByCount[2]<<'/'<<errorsByCount[3]<<std::endl;
    Require(headers==1, "Frame header lost or duplicated");
    Require(receivedWords==expectedWords.size(), "Decoded word count mismatch");
    Require(dataBitErrors==0, "Recovered data or phase differs from transmitted fixture");
    Require(errorsByCount[3]==0, "Uncorrectable fixture word");
    if (requireClean) Require(rawBitErrors==0, "Clean IQ required error correction");
}
unsigned char Quantize(double value) {
    return static_cast<unsigned char>((std::max)(0,(std::min)(255,
        static_cast<int>(std::floor(127.5+128*value+.5)))));
}
std::vector<unsigned char> Iq(bool four,bool fast,int scenario) {
    const auto symbols=Symbols(four,fast,true);
    std::mt19937 random(555);
    std::uniform_real_distribution<double> edgeJitter(-0.35,0.35);
    std::normal_distribution<double> noise(0,scenario==1?.01:.000001);
    const double offset=scenario==2?150:scenario==3?-150:0;
    const double ppm=scenario==4?50:scenario==5?-50:0;
    const double amplitude=scenario==6?.06:.2;
    const double deviation[]={4500,1500,-1500,-4500};
    std::vector<unsigned char> iq; double angle=0, nextSample=0;
    for (size_t s=0;s<symbols.size();++s) {
        nextSample+=(fast&&s>=368?320:640)*(1+ppm*1e-6);
        // Independently displace payload edges while retaining the symbol
        // centres. A clock that jumps to every edge can skip a short symbol.
        const double boundary=nextSample+(scenario==9 && s>410?
            edgeJitter(random)*(fast?320:640):0);
        while (iq.size()/2<static_cast<size_t>(boundary)) {
            angle+=2*3.14159265358979323846*
                ((scenario==7?-1:1)*deviation[symbols[s]]+offset)/1024000;
            iq.push_back(Quantize(amplitude*std::cos(angle)+noise(random)));
            iq.push_back(Quantize(amplitude*std::sin(angle)+noise(random)));
        }
    }
    return iq;
}
void Feed(const std::vector<float>& audio) {
    std::vector<char> pcm(audio.size());
    for (size_t i=0;i<audio.size();++i) {
        const int v=(std::max)(0,(std::min)(255,static_cast<int>(audio[i]*128+128)));
        pcm[i]=static_cast<char>(static_cast<unsigned char>(v));
    }
    if (!pcm.empty()) Audio_To_Bits(pcm.data(),static_cast<long>(pcm.size()));
}
void Replay(const std::vector<unsigned char>& iq,int preset,size_t chunk,unsigned bandwidth=12500,unsigned audioRate=48000) {
    ResetFixture(preset);
    pdw::signal::RtlSignalConditioner conditioner(1024000,audioRate,bandwidth);
    pdw::signal::DecoderAudioAdapter adapter;
    std::vector<float> audio,reference;
    size_t serviceSamples=0;
    for (size_t start=0;start<iq.size();start+=chunk) {
        conditioner.ProcessUnsignedIq(iq.data()+start,(std::min)(chunk,iq.size()-start),audio);
        if (audio.empty()) continue;
        bool reset=false; adapter.Process(audio.data(),audio.size(),audioRate,false,reference,reset);
        if (reset) Reset_ATB();
        Feed(reference);
        serviceSamples+=audio.size();
        while (serviceSamples>=audioRate/10) {
            serviceSamples-=audioRate/10;
            if (flex_timer && !--flex_timer && !pocbit) { BaudRate=1600; config_index=INDEX1600; }
        }
    }
    CheckResult(true);
}
void CheckRejectedSync() {
    // The invariant centre of a FLEX sync is insufficient by itself. Noise
    // or a different signal can match it without either valid outer word.
    // Follow that partial match with a complete synthetic message to prove
    // the decoder does not enter a frame on a rejected header.
    messageFixture=true;
    for (int rejected=0;rejected<3;++rejected) {
        auto symbols=Symbols(false,false,false);
        if (rejected==0) for (int bit=0;bit<16;++bit) symbols[256+bit]=3;
        if (rejected==1) for (int bit=0;bit<16;++bit) {
            const bool value=(0x1234u>>(15-bit))&1u;
            symbols[256+bit]=value?0:3;
            symbols[304+bit]=value?3:0;
        }
        ResetFixture(5); compareFixture=false;
        if (rejected==2) Profile.flex_1600=false;
        for (int symbol:symbols) frame_flex(static_cast<char>(symbol));
        Require(headers==0, "Rejected FLEX sync started frame processing");
        Require(displayedMessages.empty(), "Rejected FLEX sync produced a message");
        Require(errorsByCount[0]+errorsByCount[1]+errorsByCount[2]+errorsByCount[3]==0,
            "Rejected FLEX sync polluted decoder error counters");
    }
    messageFixture=false;
}
void CheckStreamBoundaries() {
    unsigned failures=0;
    const auto expect=[&](bool passed,const char* description) {
        std::cout<<(passed?"PASS: ":"FAIL: ")<<description<<'\n';
        if (!passed) ++failures;
    };
    ResetFixture(5);
    Feed(std::vector<float>(64,0.0f)); Feed(std::vector<float>(47,0.0f));
    expect(WatchCtr==-1,"Silence across callbacks must not start the symbol clock");
    ResetFixture(5); WatchCtr=0; pocbit=1; pocsag.lastInput=-2;
    Feed(std::vector<float>(1,0.0f));
    expect(pocsag.lastInput==-1,"POCSAG lock expiry must reset framing, not inject a data bit");
    const auto symbols=Symbols(false,false,false);
    for (unsigned cut:{257u,264u,288u,319u}) {
        ResetFixture(5);
        for (size_t index=0;index<cut;++index) frame_flex(static_cast<char>(symbols[index]));
        flex_reset(); Reset_ATB();
        for (size_t index=cut;index<320;++index) frame_flex(static_cast<char>(symbols[index]));
        expect(!bFlexActive,"A FLEX header straddling a stream reset must be rejected");
        for (size_t index=0;index<320;++index) frame_flex(static_cast<char>(symbols[index]));
        expect(bFlexActive,"A fresh complete header must reacquire after a gap");
    }
    ResetFixture(5); const bool initialPolarity=Profile.invert;
    for (unsigned word:{0xEDCBu,0x5939u,0x5555u,0x1234u})
        for (int bit=15;bit>=0;--bit) frame_flex((word>>bit)&1?0:3);
    expect(!bFlexActive && Profile.invert==initialPolarity,"An unsupported inverted header must not change receiver polarity");
    ResetFixture(5);
    for (unsigned word:{0u,0xA6C6u,0xAAAAu,0xFFFFu})
        for (int bit=15;bit>=0;--bit) frame_flex((word>>bit)&1?0:3);
    expect(!bFlexActive,"An unused zero sync table entry must not start a ReFLEX frame");
    for (unsigned damage:{0u,1u,2u,3u}) {
        auto headerSymbols=Symbols(false,false,false);
        ResetFixture(5); compareFixture=false;
        const unsigned information=0x12010+damage*0x310;
        auto bits=Codeword(information);
        for (unsigned position=0;position<damage;++position) bits[4+position*5]^=1;
        for (size_t index=0;index<bits.size();++index) headerSymbols[336+index]=bits[index]?0:3;
        for (size_t index=0;index<380;++index) frame_flex(static_cast<char>(headerSymbols[index]));
        if (damage<3) expect(headers==1 && iCurrentCycle==static_cast<int>(((~information)>>4)&15) &&
            iCurrentFrame==static_cast<int>(((~information)>>8)&127), "Correctable FLEX frame information must not repeat a previous cycle/frame");
        else expect(headers==0 && iCurrentCycle==99 && iCurrentFrame==999,"Uncorrectable FLEX information must mark the cycle/frame unknown");
    }
    Require(failures==0,"Stream boundary regressions");
}
int main(int argc,char** argv) {
    try {
        if (argc==3 && std::string(argv[1])=="--write-native-iq") {
            messageFixture=true;
            const auto iq=Iq(true,true,0);
            std::ofstream output(argv[2],std::ios::binary);
            output.write(reinterpret_cast<const char*>(iq.data()),static_cast<std::streamsize>(iq.size()));
            output.close();
            return output?0:1;
        }
        if (argc==2 && std::string(argv[1])=="--stream-boundaries") { CheckStreamBoundaries(); return 0; }
        messageFixture=true;
        std::cout<<"FLEX 1600 two-level payload with bounded edge jitter"<<std::endl;
        Replay(Iq(false,false,9),5,997);
        Require(displayedMessages.size()==1 && displayedMessages[0]==FixtureText(0),
            "Edge jitter corrupted the synthetic message");
        // Exercise the retained local-input timing and the RTL-TCP route,
        // as well as direct RTL-SDR used by the main matrix below.
        for (int kind : {0,2}) {
            g_modernCaptureKind=kind;
            for (bool fast:{false,true}) for (bool four:{false,true})
                Replay(Iq(four,fast,0),5,32768);
        }
        g_modernCaptureKind=3;
        messageFixture=false;
        CheckRejectedSync();
        CheckBch(); unsigned cases=0;
        for (bool fast:{false,true}) for (bool four:{false,true}) {
            const auto symbols=Symbols(four,fast,false); ResetFixture(5);
            for (int symbol:symbols) frame_flex(static_cast<char>(symbol));
            CheckResult(true);
            for (int scenario=0;scenario<9;++scenario) {
                std::cout<<"FLEX "<<(fast?3200:1600)<<" sps "<<(four?4:2)
                    <<" levels scenario "<<scenario<<std::endl;
                const auto iq=Iq(four,fast,scenario);
                Replay(iq,5,scenario==8?997:32768); ++cases;
                if (!scenario) { Replay(iq,0,32768); ++cases; }
            }
        }
        messageFixture=true;
        unsigned messageCases=0;
        for (unsigned bandwidth:{8000u,12000u,12500u})
        for (bool fast:{false,true}) for (bool four:{false,true}) for(int scenario=0;scenario<9;++scenario) {
            std::cout<<"Message fixture "<<(fast?3200:1600)<<" sps "<<(four?4:2)
                <<" levels scenario "<<scenario<<" bandwidth "<<bandwidth<<std::endl;
            Replay(Iq(four,fast,scenario),5,scenario==8?997:32768,bandwidth);
            size_t message=0;
            for (int p=0;p<4;++p) {
                if ((p%2 && !four) || (p>=2 && !fast)) continue;
                Require(message<displayedMessages.size(), "Missing formatted synthetic message");
                Require(displayedMessages[message]==FixtureText(p), "Synthetic message text is garbled");
                Require(displayedModes[message]==std::string("FLEX-")+char('A'+p), "Message phase mismatch");
                ++message;
            }
            Require(message==displayedMessages.size(), "Unexpected formatted message");
            ++messageCases;
        }
        for (unsigned rate:{44100u,96000u}) for (bool fast:{false,true}) for (bool four:{false,true}) {
            Replay(Iq(four,fast,8),5,997,12500,rate);
            size_t message=0;
            for (int phaseIndex=0;phaseIndex<4;++phaseIndex) {
                if ((phaseIndex%2 && !four) || (phaseIndex>=2 && !fast)) continue;
                Require(message<displayedMessages.size() && displayedMessages[message++]==FixtureText(phaseIndex),
                    "Rate-adapted message must match the transmitted text");
            }
            Require(message==displayedMessages.size(),"Unexpected rate-adapted row");
            ++messageCases;
        }
        std::cout<<"BCH exhaustive single/double-bit recovery and triple-bit rejection passed; "<<cases
            <<" FLEX IQ cases recovered every word in its expected phase without corrections.\n";
        std::cout<<messageCases<<" synthetic alphanumeric text cases matched exactly through IQ, acquisition, BCH and message formatting in all four modes.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<"FAILED: "<<error.what()<<'\n'; return 1;
    }
}
