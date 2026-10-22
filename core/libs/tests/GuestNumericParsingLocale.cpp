#include "prx/libc/include/general/VabiMacros.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <cerrno>
#include <cfenv>
#include <clocale>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <locale.h>
#include <stdexcept>
#include <string>
#include <thread>

extern "C" {
int* APS5_VABI __error_nid_postfix();
double APS5_VABI atof_nid_postfix(const char*);
double APS5_VABI strtod_nid_postfix(const char*, char**);
float APS5_VABI strtof_nid_postfix(const char*, char**);
long double APS5_VABI strtold_nid_postfix(const char*, char**);
double APS5_VABI wcstod_nid_postfix(const char16_t*, char16_t**);
float APS5_VABI wcstof_nid_postfix(const char16_t*, char16_t**);
long double APS5_VABI wcstold_nid_postfix(const char16_t*, char16_t**);
}

namespace {

constexpr int SentinelError = 73;
constexpr int SkipCode = 77;

struct Result {
    std::uint64_t bits = 0;
    std::uint16_t exponent = 0;
    std::size_t used = 0;
    int error = 0;
    int exceptions = 0;
};

struct DoubleCase {
    const char* text;
    std::uint64_t bits;
    std::size_t used;
    int error;
    bool exact = true;
};

struct FloatCase {
    const char* text;
    std::uint32_t bits;
    std::size_t used;
    int error;
    bool exact = true;
};

struct ExtendedCase {
    const char* text;
    std::uint64_t mantissa;
    std::uint16_t exponent;
    std::size_t used;
    int error;
    bool exact;
};

constexpr DoubleCase doubleCases[] = {
    {"1.5", 0x3ff8000000000000ull, 3, SentinelError},
    {"-1.5tail", 0xbff8000000000000ull, 4, SentinelError},
    {"+0.0", 0x0000000000000000ull, 4, SentinelError},
    {"-0.0", 0x8000000000000000ull, 4, SentinelError},
    {".5", 0x3fe0000000000000ull, 2, SentinelError},
    {"1e3", 0x408f400000000000ull, 3, SentinelError},
    {"0x1p3", 0x4020000000000000ull, 5, SentinelError},
    {"0x1.8p1", 0x4008000000000000ull, 7, SentinelError},
    {"1,5", 0x3ff0000000000000ull, 1, SentinelError},
    {"1.5,", 0x3ff8000000000000ull, 3, SentinelError},
    {"words", 0x0000000000000000ull, 0, SentinelError},
    {"-.", 0x0000000000000000ull, 0, SentinelError, false},
    {"2.2250738585072014e-308", 0x0010000000000000ull, 23, SentinelError},
    {"1e-310", 0x000012688b70e62bull, 6, SentinelError},
    {"0x1p-1074", 0x0000000000000001ull, 9, SentinelError},
    {"1e-330", 0x0000000000000000ull, 6, ERANGE},
    {"1e400", 0x7ff0000000000000ull, 5, ERANGE},
    {"-1e400", 0xfff0000000000000ull, 6, ERANGE},
    {"nan(1)", 0x7ff8000000000001ull, 6, SentinelError},
    {"-nan(abc)", 0xfff8000000000abcull, 9, SentinelError},
    {"nanx", 0x7ff8000000000000ull, 3, SentinelError},
    {"  -nan", 0xfff8000000000000ull, 6, SentinelError},
};

constexpr FloatCase floatCases[] = {
    {"1.5", 0x3fc00000u, 3, SentinelError},
    {"-1.5tail", 0xbfc00000u, 4, SentinelError},
    {"1,5", 0x3f800000u, 1, SentinelError},
    {"1.5,", 0x3fc00000u, 3, SentinelError},
    {"0x1p3", 0x41000000u, 5, SentinelError},
    {"0x1.8p1", 0x40400000u, 7, SentinelError},
    {"words", 0x00000000u, 0, SentinelError},
    {"-.", 0x00000000u, 0, SentinelError, false},
    {"1e-40", 0x000116c2u, 5, SentinelError},
    {"1e-46", 0x00000000u, 5, ERANGE},
    {"1e40", 0x7f800000u, 4, ERANGE},
    {"nan(1)", 0x7fc00000u, 6, SentinelError},
    {"-nan(abc)", 0x7fc00000u, 9, SentinelError},
};

constexpr ExtendedCase extendedCases[] = {
    {"1.5", 0xc000000000000000ull, 0x3fff, 3, SentinelError, true},
    {"-1.5tail", 0xc000000000000000ull, 0xbfff, 4, SentinelError, true},
    {"+0.0", 0x0000000000000000ull, 0x0000, 4, SentinelError, true},
    {"-0.0", 0x0000000000000000ull, 0x8000, 4, SentinelError, true},
    {".5", 0x8000000000000000ull, 0x3ffe, 2, SentinelError, true},
    {"1e3", 0xfa00000000000000ull, 0x4008, 3, SentinelError, true},
    {"0x1p3", 0x8000000000000000ull, 0x4002, 5, SentinelError, true},
    {"0x1.8p1", 0xc000000000000000ull, 0x4000, 7, SentinelError, true},
    {"1,5", 0x8000000000000000ull, 0x3fff, 1, SentinelError, true},
    {"1.5,", 0xc000000000000000ull, 0x3fff, 3, SentinelError, true},
    {"words", 0x0000000000000000ull, 0x0000, 0, SentinelError, true},
    {"-.", 0x0000000000000000ull, 0x0000, 0, SentinelError, false},
    {"2.2250738585072014e-308", 0x0000000000000000ull, 0x0000, 23, SentinelError, false},
    {"1e-310", 0x0000000000000000ull, 0x0000, 6, SentinelError, false},
    {"0x1p-1074", 0x0000000000000000ull, 0x0000, 9, SentinelError, false},
    {"1e-330", 0x0000000000000000ull, 0x0000, 6, SentinelError, false},
    {"1e400", 0x0000000000000000ull, 0x0000, 5, SentinelError, false},
    {"1e-4940", 0x0000000000000000ull, 0x0000, 7, SentinelError, false},
    {"1e-5000", 0x0000000000000000ull, 0x0000, 7, ERANGE, false},
    {"1e5000", 0x0000000000000000ull, 0x0000, 6, ERANGE, false},
    {"nan(1)", 0xc000000000000000ull, 0x7fff, 6, SentinelError, true},
    {"-nan(abc)", 0xc000000000000000ull, 0x7fff, 9, SentinelError, true},
    {"nanx", 0xc000000000000000ull, 0x7fff, 3, SentinelError, true},
    {"  -nan", 0xc000000000000000ull, 0x7fff, 6, SentinelError, true},
};

constexpr std::size_t DoubleCount = sizeof(doubleCases) / sizeof(doubleCases[0]);
constexpr std::size_t FloatCount = sizeof(floatCases) / sizeof(floatCases[0]);
constexpr std::size_t ExtendedCount = sizeof(extendedCases) / sizeof(extendedCases[0]);

struct Snapshot {
    std::array<Result, DoubleCount> doubleText;
    std::array<Result, DoubleCount> doubleAtof;
    std::array<Result, DoubleCount> doubleWide;
    std::array<Result, FloatCount> floatText;
    std::array<Result, FloatCount> floatWide;
    std::array<Result, ExtendedCount> extendedText;
    std::array<Result, ExtendedCount> extendedWide;
};

struct EnvironmentGuard {
    std::fenv_t environment;
    EnvironmentGuard() { std::fegetenv(&environment); }
    ~EnvironmentGuard() { std::fesetenv(&environment); }
};

struct ModeGuard {
#ifdef _WIN32
    int previous;
    explicit ModeGuard(int mode = _DISABLE_PER_THREAD_LOCALE) : previous(_configthreadlocale(mode)) {}
    ~ModeGuard() {
        if (previous == _ENABLE_PER_THREAD_LOCALE || previous == _DISABLE_PER_THREAD_LOCALE) _configthreadlocale(previous);
    }
#else
    ModeGuard() = default;
#endif
};

struct LocaleGuard {
    std::string previous;
    LocaleGuard() {
        const char* name = std::setlocale(LC_NUMERIC, nullptr);
        if (name != nullptr) previous = name;
    }
    ~LocaleGuard() {
        if (!previous.empty()) std::setlocale(LC_NUMERIC, previous.c_str());
    }
};

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::string CurrentLocale() {
    const char* name = std::setlocale(LC_NUMERIC, nullptr);
    return name != nullptr ? name : "";
}

std::string CurrentAllLocale() {
    const char* name = std::setlocale(LC_ALL, nullptr);
    return name != nullptr ? name : "";
}

std::string DecimalPoint() {
    const std::lconv* value = std::localeconv();
    return value != nullptr && value->decimal_point != nullptr ? value->decimal_point : "";
}

int CurrentMode() {
#ifdef _WIN32
    return _configthreadlocale(0);
#else
    return 0;
#endif
}

std::array<char16_t, 64> ToWide(const char* text) {
    std::array<char16_t, 64> wide{};
    for (std::size_t index = 0; text[index] != 0 && index + 1 < wide.size(); ++index) {
        wide[index] = static_cast<unsigned char>(text[index]);
    }
    return wide;
}

void Seed() {
    std::feclearexcept(FE_ALL_EXCEPT);
    std::feraiseexcept(FE_DIVBYZERO);
    *__error_nid_postfix() = SentinelError;
}

Result ParseDouble(const char* text) {
    char* end = nullptr;
    Seed();
    const double value = strtod_nid_postfix(text, &end);
    return {std::bit_cast<std::uint64_t>(value), 0, static_cast<std::size_t>(end - text), *__error_nid_postfix(),
        std::fetestexcept(FE_ALL_EXCEPT)};
}

Result ParseAtof(const char* text) {
    Seed();
    const double value = atof_nid_postfix(text);
    return {std::bit_cast<std::uint64_t>(value), 0, 0, *__error_nid_postfix(), std::fetestexcept(FE_ALL_EXCEPT)};
}

Result ParseFloat(const char* text) {
    char* end = nullptr;
    Seed();
    const float value = strtof_nid_postfix(text, &end);
    return {std::bit_cast<std::uint32_t>(value), 0, static_cast<std::size_t>(end - text), *__error_nid_postfix(),
        std::fetestexcept(FE_ALL_EXCEPT)};
}

Result ParseExtended(const char* text) {
    char* end = nullptr;
    Seed();
    const long double value = strtold_nid_postfix(text, &end);
    Result result;
    std::memcpy(&result.bits, &value, sizeof(result.bits));
    std::memcpy(&result.exponent, reinterpret_cast<const unsigned char*>(&value) + sizeof(result.bits), sizeof(result.exponent));
    result.used = static_cast<std::size_t>(end - text);
    result.error = *__error_nid_postfix();
    result.exceptions = std::fetestexcept(FE_ALL_EXCEPT);
    return result;
}

Result ParseWideDouble(const char* text) {
    std::array<char16_t, 64> wide = ToWide(text);
    char16_t* end = nullptr;
    Seed();
    const double value = wcstod_nid_postfix(wide.data(), &end);
    return {std::bit_cast<std::uint64_t>(value), 0, static_cast<std::size_t>(end - wide.data()), *__error_nid_postfix(),
        std::fetestexcept(FE_ALL_EXCEPT)};
}

Result ParseWideFloat(const char* text) {
    std::array<char16_t, 64> wide = ToWide(text);
    char16_t* end = nullptr;
    Seed();
    const float value = wcstof_nid_postfix(wide.data(), &end);
    return {std::bit_cast<std::uint32_t>(value), 0, static_cast<std::size_t>(end - wide.data()), *__error_nid_postfix(),
        std::fetestexcept(FE_ALL_EXCEPT)};
}

Result ParseWideExtended(const char* text) {
    std::array<char16_t, 64> wide = ToWide(text);
    char16_t* end = nullptr;
    Seed();
    const long double value = wcstold_nid_postfix(wide.data(), &end);
    Result result;
    std::memcpy(&result.bits, &value, sizeof(result.bits));
    std::memcpy(&result.exponent, reinterpret_cast<const unsigned char*>(&value) + sizeof(result.bits), sizeof(result.exponent));
    result.used = static_cast<std::size_t>(end - wide.data());
    result.error = *__error_nid_postfix();
    result.exceptions = std::fetestexcept(FE_ALL_EXCEPT);
    return result;
}

Snapshot RunSnapshot() {
    const std::string previousLocale = CurrentAllLocale();
    Snapshot snapshot;
    for (std::size_t index = 0; index < DoubleCount; ++index) {
        snapshot.doubleText[index] = ParseDouble(doubleCases[index].text);
        snapshot.doubleAtof[index] = ParseAtof(doubleCases[index].text);
        snapshot.doubleWide[index] = ParseWideDouble(doubleCases[index].text);
    }
    for (std::size_t index = 0; index < FloatCount; ++index) {
        snapshot.floatText[index] = ParseFloat(floatCases[index].text);
        snapshot.floatWide[index] = ParseWideFloat(floatCases[index].text);
    }
    for (std::size_t index = 0; index < ExtendedCount; ++index) {
        snapshot.extendedText[index] = ParseExtended(extendedCases[index].text);
        snapshot.extendedWide[index] = ParseWideExtended(extendedCases[index].text);
    }
    Require(CurrentAllLocale() == previousLocale, "guest numeric parsing changed a host locale category");
    return snapshot;
}

bool Same(const Result& left, const Result& right) {
    return left.bits == right.bits && left.exponent == right.exponent && left.used == right.used &&
        left.error == right.error && left.exceptions == right.exceptions;
}

void CheckDoubleFixed(const Result& actual, const char* text, std::uint64_t bits, std::size_t used, int error, const char* locale, const char* api, bool exact) {
    if ((exact && actual.bits != bits) || actual.used != used || actual.error != error || (actual.exceptions & FE_DIVBYZERO) == 0) {
        char message[512];
        std::snprintf(message, sizeof(message),
            "host locale \"%s\": %s(\"%s\") gave 0x%016llx used %zu errno %d flags 0x%x, expected 0x%016llx used %zu errno %d with FE_DIVBYZERO",
            locale, api, text, static_cast<unsigned long long>(actual.bits), actual.used, actual.error, actual.exceptions,
            static_cast<unsigned long long>(bits), used, error);
        throw std::runtime_error(message);
    }
}

void CheckFloatFixed(const Result& actual, const char* text, std::uint32_t bits, std::size_t used, int error, const char* locale, const char* api, bool exact) {
    if ((exact && actual.bits != bits) || actual.used != used || actual.error != error || (actual.exceptions & FE_DIVBYZERO) == 0) {
        char message[512];
        std::snprintf(message, sizeof(message),
            "host locale \"%s\": %s(\"%s\") gave 0x%08x used %zu errno %d flags 0x%x, expected 0x%08x used %zu errno %d with FE_DIVBYZERO",
            locale, api, text, static_cast<unsigned>(actual.bits), actual.used, actual.error, actual.exceptions,
            static_cast<unsigned>(bits), used, error);
        throw std::runtime_error(message);
    }
}

void CheckExtendedFixed(const Result& actual, const ExtendedCase& test, const char* locale, const char* api) {
    const bool bitsMismatch = test.exact && (actual.bits != test.mantissa || actual.exponent != test.exponent);
    if (bitsMismatch || actual.used != test.used || actual.error != test.error || (actual.exceptions & FE_DIVBYZERO) == 0) {
        char message[512];
        std::snprintf(message, sizeof(message),
            "host locale \"%s\": %s(\"%s\") gave mantissa 0x%016llx exponent 0x%04x used %zu errno %d flags 0x%x, expected mantissa 0x%016llx exponent 0x%04x used %zu errno %d with FE_DIVBYZERO",
            locale, api, test.text, static_cast<unsigned long long>(actual.bits), actual.exponent, actual.used, actual.error,
            actual.exceptions, static_cast<unsigned long long>(test.mantissa), test.exponent, test.used, test.error);
        throw std::runtime_error(message);
    }
}

void CheckFixed(const Snapshot& snapshot, const char* locale) {
    for (std::size_t index = 0; index < DoubleCount; ++index) {
        const DoubleCase& test = doubleCases[index];
        CheckDoubleFixed(snapshot.doubleText[index], test.text, test.bits, test.used, test.error, locale, "strtod", test.exact);
        CheckDoubleFixed(snapshot.doubleAtof[index], test.text, test.bits, 0, test.error, locale, "atof", test.exact);
        CheckDoubleFixed(snapshot.doubleWide[index], test.text, test.bits, test.used, test.error, locale, "wcstod", test.exact);
    }
    for (std::size_t index = 0; index < FloatCount; ++index) {
        const FloatCase& test = floatCases[index];
        CheckFloatFixed(snapshot.floatText[index], test.text, test.bits, test.used, test.error, locale, "strtof", test.exact);
        CheckFloatFixed(snapshot.floatWide[index], test.text, test.bits, test.used, test.error, locale, "wcstof", test.exact);
    }
    for (std::size_t index = 0; index < ExtendedCount; ++index) {
        CheckExtendedFixed(snapshot.extendedText[index], extendedCases[index], locale, "strtold");
        CheckExtendedFixed(snapshot.extendedWide[index], extendedCases[index], locale, "wcstold");
    }
}

void CheckResult(const Result& isolated, const Result& reference, const char* locale, const char* api, const char* text) {
    if (!Same(isolated, reference)) {
        char message[512];
        std::snprintf(message, sizeof(message),
            "host locale \"%s\": %s(\"%s\") gave bits 0x%016llx exponent 0x%04x used %zu errno %d flags 0x%x under the comma locale; the C locale gave bits 0x%016llx exponent 0x%04x used %zu errno %d flags 0x%x",
            locale, api, text, static_cast<unsigned long long>(isolated.bits), isolated.exponent, isolated.used,
            isolated.error, isolated.exceptions, static_cast<unsigned long long>(reference.bits), reference.exponent,
            reference.used, reference.error, reference.exceptions);
        throw std::runtime_error(message);
    }
}

void CheckSame(const Snapshot& isolated, const Snapshot& reference, const char* locale) {
    for (std::size_t index = 0; index < DoubleCount; ++index) {
        const char* text = doubleCases[index].text;
        CheckResult(isolated.doubleText[index], reference.doubleText[index], locale, "strtod", text);
        CheckResult(isolated.doubleAtof[index], reference.doubleAtof[index], locale, "atof", text);
        CheckResult(isolated.doubleWide[index], reference.doubleWide[index], locale, "wcstod", text);
    }
    for (std::size_t index = 0; index < FloatCount; ++index) {
        const char* text = floatCases[index].text;
        CheckResult(isolated.floatText[index], reference.floatText[index], locale, "strtof", text);
        CheckResult(isolated.floatWide[index], reference.floatWide[index], locale, "wcstof", text);
    }
    for (std::size_t index = 0; index < ExtendedCount; ++index) {
        const char* text = extendedCases[index].text;
        CheckResult(isolated.extendedText[index], reference.extendedText[index], locale, "strtold", text);
        CheckResult(isolated.extendedWide[index], reference.extendedWide[index], locale, "wcstold", text);
    }
}

void CheckHostState(const std::string& locale, const std::string& point, int mode) {
    if (CurrentLocale() != locale || DecimalPoint() != point || CurrentMode() != mode) {
        char message[512];
        std::snprintf(message, sizeof(message),
            "guest numeric parsing left the host locale as \"%s\" with decimal point \"%s\" and per-thread mode %d; expected \"%s\", \"%s\" and %d",
            CurrentLocale().c_str(), DecimalPoint().c_str(), CurrentMode(), locale.c_str(), point.c_str(), mode);
        throw std::runtime_error(message);
    }
}

const char* FindCommaLocale() {
    for (const char* name : {"French", "French_France.1252", "fr_FR.UTF-8", "de_DE.UTF-8", "fr_FR", "de_DE", "German_Germany.1252"}) {
        if (std::setlocale(LC_NUMERIC, name) != nullptr && DecimalPoint() == ",") return name;
    }
    return nullptr;
}

void CheckThrowRestoration() {
    const std::string locale = CurrentAllLocale();
    const int mode = CurrentMode();
    const int rounding = std::fegetround();
    const auto check = [&](auto parse) {
        Seed();
        bool threw = false;
        try {
            parse();
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        Require(threw, "the existing unsupported NaN payload case did not throw");
        Require(CurrentAllLocale() == locale && CurrentMode() == mode,
            "a throwing numeric conversion changed the host locale or mode");
        Require(std::fegetround() == rounding && (std::fetestexcept(FE_DIVBYZERO) != 0),
            "a throwing numeric conversion changed the host floating environment");
    };
    check([] { (void)strtod_nid_postfix("nan(1 ", nullptr); });
    check([] { (void)atof_nid_postfix("nan(1 "); });
    check([] { (void)wcstod_nid_postfix(u"nan(1 ", nullptr); });
}

void CheckOtherThread(const std::string& locale, int mode) {
    std::atomic<bool> ready{false};
    std::atomic<bool> changed{false};
    std::jthread observer([&](std::stop_token stop) {
        ready.store(true, std::memory_order_release);
        while (!stop.stop_requested()) {
            if (DecimalPoint() != ",") {
                changed.store(true, std::memory_order_relaxed);
                break;
            }
        }
    });
    while (!ready.load(std::memory_order_acquire)) std::this_thread::yield();
    for (int iteration = 0; iteration < 200; ++iteration) {
        char* end = nullptr;
        (void)strtod_nid_postfix("1.5", &end);
        (void)strtof_nid_postfix("1.5", &end);
        (void)strtold_nid_postfix("1.5", &end);
        (void)atof_nid_postfix("1.5");
        const char16_t wide[] = u"1.5";
        char16_t* wideEnd = nullptr;
        (void)wcstod_nid_postfix(wide, &wideEnd);
        (void)wcstof_nid_postfix(wide, &wideEnd);
        (void)wcstold_nid_postfix(wide, &wideEnd);
    }
    observer.request_stop();
    observer.join();
    Require(!changed.load(std::memory_order_relaxed), "another thread did not keep the host comma locale during guest numeric parsing");
    CheckHostState(locale, ",", mode);
}

}

int main() {
    try {
        const EnvironmentGuard environment;
        const ModeGuard mode;
        const LocaleGuard locale;
        Require(std::setlocale(LC_NUMERIC, "C") != nullptr, "cannot select the host C locale");
        Require(DecimalPoint() == ".", "the host C locale does not use a dot decimal point");
        const std::string cLocale = CurrentLocale();
        if (FindCommaLocale() == nullptr) {
            std::fprintf(stderr, "guest numeric parsing locale: skip %d, no installed comma-decimal host LC_NUMERIC\n", SkipCode);
            return SkipCode;
        }
        const std::string commaLocale = CurrentLocale();
        Require(DecimalPoint() == ",", "the host comma locale does not use a comma decimal point");
        for (const bool threadLocal : {false, true}) {
#ifdef _WIN32
            const ModeGuard selectedMode(threadLocal ? _ENABLE_PER_THREAD_LOCALE : _DISABLE_PER_THREAD_LOCALE);
            Require(CurrentMode() == (threadLocal ? _ENABLE_PER_THREAD_LOCALE : _DISABLE_PER_THREAD_LOCALE),
                "cannot select the host per-thread locale mode");
#else
            if (threadLocal) continue;
#endif
            const int activeMode = CurrentMode();
            for (const int rounding : {FE_TONEAREST, FE_UPWARD, FE_DOWNWARD, FE_TOWARDZERO}) {
                Require(std::fesetround(rounding) == 0, "cannot select a host rounding mode");
                Require(std::setlocale(LC_NUMERIC, cLocale.c_str()) != nullptr, "cannot select the host C locale");
                const Snapshot reference = RunSnapshot();
                Require(std::fegetround() == rounding, "guest numeric parsing changed the host rounding mode");
                CheckHostState(cLocale, ".", activeMode);
                CheckThrowRestoration();
                Require(std::setlocale(LC_NUMERIC, commaLocale.c_str()) != nullptr, "cannot select the host comma locale");
                const Snapshot isolated = RunSnapshot();
                Require(std::fegetround() == rounding, "guest numeric parsing changed the host rounding mode");
                CheckHostState(commaLocale, ",", activeMode);
                CheckThrowRestoration();
                if (rounding == FE_TONEAREST) {
                    CheckFixed(reference, "C");
                    CheckFixed(isolated, commaLocale.c_str());
                }
                CheckSame(isolated, reference, commaLocale.c_str());
            }
            CheckOtherThread(commaLocale, activeMode);
            std::printf("checked host locale %s, per-thread mode %d, all four rounding modes\n", commaLocale.c_str(), activeMode);
        }
        std::puts("guest numeric parsing locale checks passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
