#include "modulation.hpp"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 1024.0;
constexpr double kCarrierFrequency = 64.0;
constexpr double kMessageFrequency = 4.0;
constexpr double kAskAmplitudeZero = 0.35;
constexpr double kAskAmplitudeOne = 1.0;
constexpr double kBfskFrequencyOffset = 16.0;
constexpr double kAmDepth = 0.5;
constexpr double kFmDeviation = 24.0;
constexpr double kPmPhaseScale = kPi / 2.0;
constexpr double kTolerance = 1e-9;

bool almostEqual(double lhs, double rhs, double tolerance = kTolerance) {
    return std::fabs(lhs - rhs) <= tolerance;
}

double carrierAt(int index) {
    return std::sin(2.0 * kPi * kCarrierFrequency * static_cast<double>(index) /
                    kSampleRate);
}

double analogMessageAtIndex(int index) {
    return std::sin(2.0 * kPi * kMessageFrequency * static_cast<double>(index) /
                    kSampleRate);
}

void testGenerateCoverSignal() {
    std::vector<double> cover(17, 0.0);
    const int written = generate_cover_signal(cover.data(), static_cast<int>(cover.size()));
    assert(written == static_cast<int>(cover.size()));
    assert(almostEqual(cover[0], 0.0));
    assert(almostEqual(cover[1], carrierAt(1)));
    assert(almostEqual(cover[4], 1.0));
    assert(almostEqual(cover[8], 0.0));
}

void testGenerateDigitalMessage() {
    std::vector<unsigned char> message(10, 0U);
    const int written = simulate_digital_modulation_signal(message.data(),
                                                           static_cast<int>(message.size()));
    assert(written == static_cast<int>(message.size()));
    const unsigned char expected[10] = {1U, 1U, 0U, 0U, 1U, 0U, 1U, 0U, 1U, 1U};
    for (int index = 0; index < 10; ++index) {
        assert(message[index] == expected[index]);
    }
}

void testGenerateAnalogMessage() {
    std::vector<double> message(32, 0.0);
    const int written = simulate_analog_modulation_signal(message.data(),
                                                          static_cast<int>(message.size()));
    assert(written == static_cast<int>(message.size()));
    assert(almostEqual(message[0], 0.0));
    assert(almostEqual(message[1], analogMessageAtIndex(1)));
    for (double sample : message) {
        assert(std::fabs(sample) <= 1.0 + kTolerance);
    }
}

void testDigitalAmplitudeModulation() {
    const unsigned char message[] = {1U, 0U};
    std::vector<double> cover(8, 0.0);
    const int written = modulate_digital_amplitude(cover.data(),
                                                   static_cast<int>(cover.size()),
                                                   message, 2);
    assert(written == static_cast<int>(cover.size()));
    assert(almostEqual(cover[1], kAskAmplitudeOne * carrierAt(1)));
    assert(almostEqual(cover[5], kAskAmplitudeZero * carrierAt(5)));
}

void testDigitalFrequencyModulation() {
    const unsigned char message[] = {1U, 0U};
    std::vector<double> cover(8, 0.0);
    const int written = modulate_digital_frequency(cover.data(),
                                                   static_cast<int>(cover.size()),
                                                   message, 2);
    assert(written == static_cast<int>(cover.size()));
    const double expected_first_half = std::sin(2.0 * kPi * (kCarrierFrequency + kBfskFrequencyOffset) /
                                                kSampleRate);
    const double expected_second_half = std::sin(2.0 * kPi * (kCarrierFrequency - kBfskFrequencyOffset) *
                                                 5.0 / kSampleRate);
    assert(almostEqual(cover[1], expected_first_half));
    assert(almostEqual(cover[5], expected_second_half));
}

void testDigitalPhaseModulation() {
    const unsigned char message[] = {1U, 0U};
    std::vector<double> cover(8, 0.0);
    const int written = modulate_digital_phase(cover.data(),
                                               static_cast<int>(cover.size()),
                                               message, 2);
    assert(written == static_cast<int>(cover.size()));
    assert(almostEqual(cover[1], std::sin(2.0 * kPi * kCarrierFrequency / kSampleRate + kPi)));
    assert(almostEqual(cover[5], std::sin(2.0 * kPi * kCarrierFrequency * 5.0 / kSampleRate)));
}

void testAnalogAmplitudeModulation() {
    const double message[] = {0.0, 1.0};
    std::vector<double> cover(8, 0.0);
    const int written = modulate_analog_amplitude(cover.data(),
                                                  static_cast<int>(cover.size()),
                                                  message, 2);
    assert(written == static_cast<int>(cover.size()));
    assert(almostEqual(cover[1], carrierAt(1)));
    assert(almostEqual(cover[5], (1.0 + kAmDepth) * carrierAt(5)));
}

void testAnalogFrequencyModulation() {
    const double message[] = {0.0, 1.0};
    std::vector<double> cover(4, 0.0);
    const int written = modulate_analog_frequency(cover.data(),
                                                  static_cast<int>(cover.size()),
                                                  message, 2);
    assert(written == static_cast<int>(cover.size()));
    assert(almostEqual(cover[0], 0.0));

    double phase = 0.0;
    for (int index = 0; index < 4; ++index) {
        assert(almostEqual(cover[index], std::sin(phase)));
        const double sample = index < 2 ? 0.0 : 1.0;
        phase += 2.0 * kPi * (kCarrierFrequency + kFmDeviation * sample) / kSampleRate;
    }
}

void testAnalogPhaseModulation() {
    const double message[] = {0.0, 1.0};
    std::vector<double> cover(8, 0.0);
    const int written = modulate_analog_phase(cover.data(),
                                              static_cast<int>(cover.size()),
                                              message, 2);
    assert(written == static_cast<int>(cover.size()));
    assert(almostEqual(cover[1], std::sin(2.0 * kPi * kCarrierFrequency / kSampleRate)));
    assert(almostEqual(cover[5], std::sin(2.0 * kPi * kCarrierFrequency * 5.0 / kSampleRate +
                                          kPmPhaseScale)));
}

void testSignalsAreFiniteAndDifferentFromCarrier() {
    std::vector<double> carrier(32, 0.0);
    std::vector<double> analog_msg(8, 0.0);
    assert(generate_cover_signal(carrier.data(), static_cast<int>(carrier.size())) == 32);
    assert(simulate_analog_modulation_signal(analog_msg.data(), static_cast<int>(analog_msg.size())) == 8);

    std::vector<double> modulated = carrier;
    assert(modulate_analog_amplitude(modulated.data(), static_cast<int>(modulated.size()),
                                     analog_msg.data(), static_cast<int>(analog_msg.size())) == 32);

    bool differs = false;
    for (std::size_t index = 0; index < modulated.size(); ++index) {
        assert(std::isfinite(modulated[index]));
        if (!almostEqual(modulated[index], carrier[index], 1e-6)) {
            differs = true;
        }
    }
    assert(differs);
}

void testArgumentValidation() {
    assert(generate_cover_signal(nullptr, -1) == -1);
    assert(generate_cover_signal(nullptr, 0) == 0);
    assert(simulate_digital_modulation_signal(nullptr, -1) == -1);
    assert(simulate_digital_modulation_signal(nullptr, 0) == 0);
    assert(simulate_analog_modulation_signal(nullptr, -1) == -1);
    assert(simulate_analog_modulation_signal(nullptr, 0) == 0);

    unsigned char digital_message[1] = {1U};
    double analog_message[1] = {0.0};
    double cover[4] = {};

    assert(modulate_digital_amplitude(nullptr, 4, digital_message, 1) == -1);
    assert(modulate_digital_amplitude(cover, 4, nullptr, 1) == -1);
    assert(modulate_digital_amplitude(cover, 4, digital_message, 0) == -1);
    assert(modulate_digital_amplitude(nullptr, 0, nullptr, 0) == 0);

    assert(modulate_analog_phase(nullptr, 4, analog_message, 1) == -1);
    assert(modulate_analog_phase(cover, 4, nullptr, 1) == -1);
    assert(modulate_analog_phase(cover, 4, analog_message, -1) == -1);
    assert(modulate_analog_phase(nullptr, 0, nullptr, 0) == 0);
}

void testCliSmoke() {
    assert(std::system("./bin/modulation_demo cover 8 >/dev/null") == 0);
    assert(std::system("./bin/modulation_demo digital_msg 8 >/dev/null") == 0);
    assert(std::system("./bin/modulation_demo analog_msg 8 >/dev/null") == 0);
    assert(std::system("./bin/modulation_demo modulate df 16 4 >/dev/null") == 0);
    assert(std::system("./bin/modulation_demo modulate aa 16 4 >/dev/null") == 0);
}

}  // namespace

int main() {
    testGenerateCoverSignal();
    testGenerateDigitalMessage();
    testGenerateAnalogMessage();
    testDigitalAmplitudeModulation();
    testDigitalFrequencyModulation();
    testDigitalPhaseModulation();
    testAnalogAmplitudeModulation();
    testAnalogFrequencyModulation();
    testAnalogPhaseModulation();
    testSignalsAreFiniteAndDifferentFromCarrier();
    testArgumentValidation();
    testCliSmoke();
    std::cout << "All modulation tests passed.\n";
    return 0;
}
