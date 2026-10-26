#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "prx/libSceAvPlayer/include/AvPlayer.hpp"
#include "prx/libSceAvPlayer/src/Source.cpp"

namespace {

constexpr const char* ReplacementPath = "/app0/replaced.mp4";
constexpr std::int64_t Int64Min = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t Int64Max = std::numeric_limits<std::int64_t>::max();
constexpr std::uint64_t Uint64Max = std::numeric_limits<std::uint64_t>::max();
constexpr int UnknownWhence = 4;

struct FakeFile {
    std::uint64_t size = 0;
    std::int64_t opens = 0;
    std::int64_t closes = 0;
};

std::int32_t APS5_VABI FakeOpen(void* object, const char*) {
    ++static_cast<FakeFile*>(object)->opens;
    return 0;
}

std::int32_t APS5_VABI FakeClose(void* object) {
    ++static_cast<FakeFile*>(object)->closes;
    return 0;
}

std::int32_t APS5_VABI FakeRead(void*, std::uint8_t*, std::uint64_t, std::uint32_t) {
    return 0;
}

std::uint64_t APS5_VABI FakeSize(void* object) {
    return static_cast<FakeFile*>(object)->size;
}

struct Fixture {
    FakeFile file;
    AvPlayerFileReplacement callbacks;

    explicit Fixture(std::uint64_t size)
        : callbacks{&file, &FakeOpen, &FakeClose, &FakeRead, &FakeSize} {
        file.size = size;
    }
};

int Failures = 0;

void Check(std::int64_t actual, std::int64_t expected, const char* label) {
    if (actual == expected) return;
    std::printf("FAIL %s: expected %lld, got %lld\n", label, static_cast<long long>(expected),
                static_cast<long long>(actual));
    ++Failures;
}

void CheckTrue(bool condition, const char* label) {
    if (condition) return;
    std::printf("FAIL %s\n", label);
    ++Failures;
}

std::int64_t Seek(AvPlayer::FileReplacementStream& stream, std::int64_t offset, int whence) {
    AVIOContext* context = stream.Context();
    return context->seek(context->opaque, offset, whence);
}

void CheckSeek(AvPlayer::FileReplacementStream& stream, std::int64_t offset, int whence,
               std::int64_t expected, const char* label) {
    Check(Seek(stream, offset, whence), expected, label);
}

void CheckPosition(AvPlayer::FileReplacementStream& stream, std::int64_t expected,
                   const char* label) {
    CheckSeek(stream, 0, SEEK_CUR, expected, label);
}

void CheckSeekFails(AvPlayer::FileReplacementStream& stream, std::int64_t offset, int whence,
                    const char* label) {
    CheckTrue(Seek(stream, offset, whence) < 0, label);
}

void TestSmallFile() {
    constexpr std::uint64_t Size = 1000;
    Fixture fixture(Size);
    {
        AvPlayer::FileReplacementStream stream(fixture.callbacks);
        const bool opened = stream.Open(ReplacementPath);
        CheckTrue(opened, "small open");
        if (opened && stream.Context()) {
            CheckSeek(stream, 0, SEEK_SET, 0, "small SET start");
            CheckSeek(stream, 250, SEEK_SET, 250, "small SET 250");
            CheckSeek(stream, 100, SEEK_CUR, 350, "small CUR ahead");
            CheckSeek(stream, -50, SEEK_CUR, 300, "small CUR behind");
            CheckSeek(stream, -1000, SEEK_CUR, 0, "small CUR negative clamp");
            CheckSeek(stream, 1000, SEEK_CUR, 1000, "small CUR to end");
            CheckSeek(stream, Int64Max, SEEK_CUR, 1000, "small CUR INT64_MAX clamp");
            CheckSeek(stream, 0, SEEK_END, 1000, "small END 0");
            CheckSeek(stream, -1000, SEEK_END, 0, "small END negative clamp");
            CheckSeek(stream, 500, SEEK_END, 1000, "small END positive clamp");
            CheckSeek(stream, Int64Max, SEEK_END, 1000, "small END INT64_MAX clamp");
            CheckSeek(stream, Int64Min, SEEK_END, 0, "small END INT64_MIN clamp");
            CheckSeek(stream, Int64Min, SEEK_SET, 0, "small SET INT64_MIN");
            CheckSeek(stream, Int64Max, SEEK_SET, 1000, "small SET INT64_MAX");
            CheckSeek(stream, 600, SEEK_SET | AVSEEK_FORCE, 600, "small SET forced");
            CheckSeek(stream, -1, SEEK_CUR | AVSEEK_FORCE, 599, "small CUR forced");
            CheckSeek(stream, 400, SEEK_SET, 400, "small SET before size");
            CheckSeek(stream, 0, AVSEEK_SIZE, 1000, "small size query");
            CheckPosition(stream, 400, "small size query position");
            CheckSeekFails(stream, 0, UnknownWhence, "small unknown whence fails");
            CheckPosition(stream, 400, "small unknown whence position");
        }
    }
    Check(fixture.file.opens, 1, "small open count");
    Check(fixture.file.closes, 1, "small close count");
}

void TestInt64MaxSize() {
    Fixture fixture(static_cast<std::uint64_t>(Int64Max));
    {
        AvPlayer::FileReplacementStream stream(fixture.callbacks);
        const bool opened = stream.Open(ReplacementPath);
        CheckTrue(opened, "max open");
        if (opened && stream.Context()) {
            CheckSeek(stream, 0, AVSEEK_SIZE, Int64Max, "max size query");
            CheckSeek(stream, Int64Max, SEEK_END, Int64Max, "max END INT64_MAX");
            CheckPosition(stream, Int64Max, "max END INT64_MAX position");
            CheckSeek(stream, Int64Min, SEEK_END, 0, "max END INT64_MIN clamp");
            CheckSeek(stream, Int64Max, SEEK_SET, Int64Max, "max SET INT64_MAX");
            CheckSeek(stream, 0, AVSEEK_SIZE, Int64Max, "max size query at end");
            CheckPosition(stream, Int64Max, "max size query at end position");
        }
    }
    Check(fixture.file.opens, 1, "max open count");
    Check(fixture.file.closes, 1, "max close count");
}

void TestUnrepresentableSize() {
    Fixture fixture(static_cast<std::uint64_t>(Int64Max) + 1);
    {
        AvPlayer::FileReplacementStream stream(fixture.callbacks);
        const bool opened = stream.Open(ReplacementPath);
        CheckTrue(opened, "overflow open");
        if (opened && stream.Context()) {
            CheckSeek(stream, 123, SEEK_SET, 123, "overflow SET 123");
            CheckSeek(stream, 0, AVSEEK_SIZE, AVERROR(EOVERFLOW), "overflow size query");
            CheckPosition(stream, 123, "overflow size query position");
            CheckSeek(stream, 0, SEEK_END, AVERROR(EOVERFLOW), "overflow END 0");
            CheckPosition(stream, 123, "overflow END 0 position");
            CheckSeek(stream, Int64Max, SEEK_SET, Int64Max, "overflow SET INT64_MAX");
            CheckSeek(stream, 0, AVSEEK_SIZE, AVERROR(EOVERFLOW), "overflow size query at max");
            CheckPosition(stream, Int64Max, "overflow size query at max position");
        }
    }
    Check(fixture.file.opens, 1, "overflow open count");
    Check(fixture.file.closes, 1, "overflow close count");
}

void TestUint64MaxSize() {
    Fixture fixture(Uint64Max);
    {
        AvPlayer::FileReplacementStream stream(fixture.callbacks);
        const bool opened = stream.Open(ReplacementPath);
        CheckTrue(opened, "huge open");
        if (opened && stream.Context()) {
            CheckSeek(stream, Int64Min, SEEK_END, Int64Max, "huge END INT64_MIN");
            CheckPosition(stream, Int64Max, "huge END INT64_MIN position");
            CheckSeek(stream, 0, AVSEEK_SIZE, AVERROR(EOVERFLOW), "huge size query");
            CheckPosition(stream, Int64Max, "huge size query position");
            CheckSeek(stream, -1, SEEK_END, AVERROR(EOVERFLOW), "huge END -1");
            CheckPosition(stream, Int64Max, "huge END -1 position");
            CheckSeek(stream, 0, SEEK_END, AVERROR(EOVERFLOW), "huge END 0");
            CheckPosition(stream, Int64Max, "huge END 0 position");
            CheckSeek(stream, Int64Min, SEEK_SET, 0, "huge SET INT64_MIN");
            CheckPosition(stream, 0, "huge SET INT64_MIN position");
        }
    }
    Check(fixture.file.opens, 1, "huge open count");
    Check(fixture.file.closes, 1, "huge close count");
}

}

int main() {
    TestSmallFile();
    TestInt64MaxSize();
    TestUnrepresentableSize();
    TestUint64MaxSize();
    if (Failures != 0) {
        std::printf("%d checks failed\n", Failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
