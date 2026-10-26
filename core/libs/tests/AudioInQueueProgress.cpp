#include "prx/libc/include/PreciseWait.hpp"
#define SDL_MAIN_HANDLED
#include "SDL.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static std::size_t g_getQueuedCalls = 0;
static std::size_t g_dequeueCalls = 0;
static Uint32 g_queuedSize = 0;
static Uint32 g_finalBytes = 0;
static std::uint8_t g_pattern = 0xAB;
static std::size_t g_excessRemaining = 0;
static void* g_destination = nullptr;
static bool g_zeroOnDiscard = false;

static Uint32 SDLCALL MockGetQueuedAudioSize(SDL_AudioDeviceID) {
    ++g_getQueuedCalls;
    return g_queuedSize;
}

static Uint32 SDLCALL MockDequeueAudio(SDL_AudioDeviceID, void* data, Uint32 len) {
    ++g_dequeueCalls;
    if (data == g_destination) {
        const Uint32 returned = std::min(g_finalBytes, len);
        if (returned != 0) std::memset(data, g_pattern, returned);
        return returned;
    }
    if (g_zeroOnDiscard) return 0;
    Uint32 returned = static_cast<Uint32>(std::min<std::size_t>(len, g_excessRemaining));
    if (returned > 1) returned /= 2;
    g_excessRemaining -= returned;
    return returned;
}

#define SDL_GetQueuedAudioSize(...) MockGetQueuedAudioSize(__VA_ARGS__)
#define SDL_DequeueAudio(...) MockDequeueAudio(__VA_ARGS__)

#include "prx/libSceAudioIn/Export.cpp"

#undef SDL_GetQueuedAudioSize
#undef SDL_DequeueAudio

static void ResetMocks() {
    g_getQueuedCalls = 0;
    g_dequeueCalls = 0;
    g_excessRemaining = 0;
    g_destination = nullptr;
    g_zeroOnDiscard = false;
}

static void Require(bool value) {
    if (!value) std::abort();
}

int main() {
    constexpr std::size_t bytes = 1024;
    constexpr std::uint8_t sentinel = 0xCD;
    std::array<std::uint8_t, 2048> buffer{};

    ResetMocks();
    g_queuedSize = static_cast<Uint32>(bytes * 5);
    g_excessRemaining = g_queuedSize - static_cast<Uint32>(bytes * 4);
    g_destination = buffer.data();
    g_zeroOnDiscard = true;
    g_finalBytes = static_cast<Uint32>(bytes);
    g_pattern = 0xAB;
    for (auto& b : buffer) b = sentinel;
    std::puts("checking a stalled capture queue");
    std::fflush(stdout);
    std::size_t returned = capture(1, buffer.data(), bytes, std::chrono::steady_clock::now());
    Require(returned == bytes);
    Require(g_getQueuedCalls == 2);
    Require(g_dequeueCalls == 2);
    for (std::size_t i = 0; i < bytes; ++i) Require(buffer[i] == g_pattern);
    Require(buffer[bytes] == sentinel);

    ResetMocks();
    g_queuedSize = static_cast<Uint32>(bytes * 6);
    g_excessRemaining = g_queuedSize - static_cast<Uint32>(bytes * 4);
    g_destination = buffer.data();
    g_finalBytes = 37;
    g_pattern = 0x5A;
    for (auto& b : buffer) b = sentinel;
    returned = capture(1, buffer.data(), bytes, std::chrono::steady_clock::now());
    Require(returned == 37);
    Require(g_excessRemaining == 0);
    Require(g_dequeueCalls > 2);
    for (std::size_t i = 0; i < 37; ++i) Require(buffer[i] == g_pattern);
    Require(buffer[37] == sentinel);

    std::puts("audio input queue checks passed");
    return 0;
}
