extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

struct Faults {
    bool failExtradata = false;
    bool failOpen = false;
};

Faults faults;
int liveContexts = 0;

}

AVCodecContext* test_avcodec_alloc_context3(const AVCodec* codec);
void test_avcodec_free_context(AVCodecContext** context);
void* test_av_mallocz(std::size_t size);
int test_avcodec_open2(AVCodecContext* context, const AVCodec* codec, AVDictionary** options);

#define avcodec_alloc_context3 test_avcodec_alloc_context3
#define avcodec_free_context test_avcodec_free_context
#define av_mallocz test_av_mallocz
#define avcodec_open2 test_avcodec_open2
#include "prx/libSceAudiodec/src/Codecs.cpp"
#undef avcodec_alloc_context3
#undef avcodec_free_context
#undef av_mallocz
#undef avcodec_open2

AVCodecContext* test_avcodec_alloc_context3(const AVCodec* codec) {
    AVCodecContext* context = avcodec_alloc_context3(codec);
    if (context) ++liveContexts;
    return context;
}

void test_avcodec_free_context(AVCodecContext** context) {
    if (context && *context) --liveContexts;
    avcodec_free_context(context);
}

void* test_av_mallocz(std::size_t size) {
    if (faults.failExtradata) return nullptr;
    return av_mallocz(size);
}

int test_avcodec_open2(AVCodecContext* context, const AVCodec* codec, AVDictionary** options) {
    if (faults.failOpen) return AVERROR(EINVAL);
    return avcodec_open2(context, codec, options);
}

namespace {

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void CheckNoLiveContexts(const char* message) {
    Check(liveContexts == 0, message);
}

std::array<std::uint8_t, 16> AdtsFrame(std::uint8_t channelBits) {
    std::array<std::uint8_t, 16> frame{};
    frame[0] = 0xFF;
    frame[1] = 0xF1;
    frame[2] = 0x40;
    frame[3] = channelBits;
    frame[4] = 0x02;
    return frame;
}

void ExtradataAllocationFailure() {
    for (int cycle = 0; cycle < 3; ++cycle) {
        faults = Faults{};
        faults.failExtradata = true;
        bool threw = false;
        try {
            Audiodec::CreateAac(Audiodec::WORD_SIZE_16BIT, false, 4, 2, false);
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        Check(threw, "extradata allocation failure did not throw bad_alloc");
        CheckNoLiveContexts("extradata allocation failure retained a context");
    }
    std::puts("Audiodec FFmpeg extradata allocation failure: 3 cycles, no live contexts");
}

void OpenFailure() {
    for (int cycle = 0; cycle < 3; ++cycle) {
        faults = Faults{};
        faults.failOpen = true;
        bool threw = false;
        try {
            Audiodec::CreateMp3(Audiodec::WORD_SIZE_16BIT);
        } catch (const std::runtime_error& error) {
            threw = std::string(error.what()).find("cannot open") != std::string::npos;
        }
        Check(threw, "open failure did not throw the decoder-open error");
        CheckNoLiveContexts("open failure retained a context");
    }
    std::puts("Audiodec FFmpeg open failure: 3 cycles, no live contexts");
}

void OrdinaryDestruction() {
    for (int cycle = 0; cycle < 4; ++cycle) {
        faults = Faults{};
        {
            const auto decoder = Audiodec::CreateMp3(Audiodec::WORD_SIZE_16BIT);
            Check(liveContexts == 1, "decoder did not own one live context");
            decoder->Reset();
        }
        CheckNoLiveContexts("destruction retained a context");
    }
    std::puts("Audiodec FFmpeg destruction: 4 cycles, no live contexts");
}

void FailedReopen() {
    for (int cycle = 0; cycle < 3; ++cycle) {
        faults = Faults{};
        const auto decoder = Audiodec::CreateAac(Audiodec::WORD_SIZE_16BIT, true, 0, 2, false);
        Check(liveContexts == 1, "decoder did not own one live context");
        const auto frame = AdtsFrame(0x80);
        std::array<std::uint8_t, 4096> pcm{};
        faults.failOpen = true;
        bool threw = false;
        try {
            decoder->Decode(frame.data(), frame.size(), pcm.data(), pcm.size());
        } catch (const std::runtime_error& error) {
            threw = std::string(error.what()).find("cannot open") != std::string::npos;
        }
        Check(threw, "failed reopen did not throw the decoder-open error");
        Check(liveContexts == 1, "failed reopen did not retain its allocated context");
    }
    CheckNoLiveContexts("failed reopen cycles retained a context");
    std::puts("Audiodec FFmpeg failed reopen: 3 cycles, context retained and released");
}

}

int main(int argc, char* argv[]) {
    try {
        constexpr std::pair<std::string_view, void (*)()> cases[] = {
            {"extradata", ExtradataAllocationFailure},
            {"open", OpenFailure},
            {"destruction", OrdinaryDestruction},
            {"reopen", FailedReopen},
        };
        Check(argc <= 2, "expected at most one case name");
        bool matched = false;
        for (const auto& [name, run] : cases) {
            if (argc == 2 && name != argv[1]) continue;
            matched = true;
            run();
            CheckNoLiveContexts("test case finished with a live context");
        }
        Check(matched, "unknown test case");
        CheckNoLiveContexts("test finished with a live context");
        std::puts("Audiodec FFmpeg context lifetime passed");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Audiodec FFmpeg context lifetime failed: %s\n", error.what());
        return 1;
    }
}
