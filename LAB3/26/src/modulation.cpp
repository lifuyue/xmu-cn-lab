#include "modulation.hpp"

#include <cmath>

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
constexpr unsigned char kDigitalPattern[] = {1U, 1U, 0U, 0U, 1U, 0U, 1U, 0U};

bool hasValidBuffer(const void* ptr, int size) {
    return size == 0 || ptr != nullptr;
}

bool hasValidSize(int size) {
    return size >= 0;
}

bool hasValidModulationArgs(const void* cover, int cover_len,
                            const void* message, int msg_len) {
    if (cover_len < 0 || msg_len < 0) {
        return false;
    }
    if (cover_len == 0) {
        return true;
    }
    if (msg_len == 0) {
        return false;
    }
    return cover != nullptr && message != nullptr;
}

double carrierPhaseAt(int sample_index) {
    return 2.0 * kPi * kCarrierFrequency * static_cast<double>(sample_index) /
           kSampleRate;
}

double carrierAt(int sample_index) {
    return std::sin(carrierPhaseAt(sample_index));
}

double analogMessageAt(const double* message, int msg_len,
                       int cover_len, int sample_index) {
    const int message_index = sample_index * msg_len / cover_len;
    return message[message_index];
}

unsigned char digitalMessageAt(const unsigned char* message, int msg_len,
                               int cover_len, int sample_index) {
    const int message_index = sample_index * msg_len / cover_len;
    return message[message_index] == 0U ? 0U : 1U;
}

}  // namespace

int generate_cover_signal(double* cover, int size) {
    if (!hasValidSize(size)) {
        return -1;
    }
    if (!hasValidBuffer(cover, size)) {
        return -1;
    }
    if (size == 0) {
        return 0;
    }

    for (int index = 0; index < size; ++index) {
        cover[index] = carrierAt(index);
    }
    return size;
}

int simulate_digital_modulation_signal(unsigned char* message, int size) {
    if (!hasValidSize(size)) {
        return -1;
    }
    if (!hasValidBuffer(message, size)) {
        return -1;
    }
    if (size == 0) {
        return 0;
    }

    const int pattern_len = static_cast<int>(sizeof(kDigitalPattern) / sizeof(kDigitalPattern[0]));
    for (int index = 0; index < size; ++index) {
        message[index] = kDigitalPattern[index % pattern_len];
    }
    return size;
}

int simulate_analog_modulation_signal(double* message, int size) {
    if (!hasValidSize(size)) {
        return -1;
    }
    if (!hasValidBuffer(message, size)) {
        return -1;
    }
    if (size == 0) {
        return 0;
    }

    for (int index = 0; index < size; ++index) {
        const double phase = 2.0 * kPi * kMessageFrequency *
                             static_cast<double>(index) / kSampleRate;
        message[index] = std::sin(phase);
    }
    return size;
}

int modulate_digital_frequency(double* cover, int cover_len,
                               const unsigned char* message, int msg_len) {
    if (!hasValidModulationArgs(cover, cover_len, message, msg_len)) {
        return -1;
    }
    if (cover_len == 0) {
        return 0;
    }

    for (int index = 0; index < cover_len; ++index) {
        const unsigned char bit = digitalMessageAt(message, msg_len, cover_len, index);
        const double frequency = bit == 0U
                                     ? (kCarrierFrequency - kBfskFrequencyOffset)
                                     : (kCarrierFrequency + kBfskFrequencyOffset);
        const double phase = 2.0 * kPi * frequency * static_cast<double>(index) /
                             kSampleRate;
        cover[index] = std::sin(phase);
    }
    return cover_len;
}

int modulate_analog_frequency(double* cover, int cover_len,
                              const double* message, int msg_len) {
    if (!hasValidModulationArgs(cover, cover_len, message, msg_len)) {
        return -1;
    }
    if (cover_len == 0) {
        return 0;
    }

    double phase = 0.0;
    for (int index = 0; index < cover_len; ++index) {
        cover[index] = std::sin(phase);
        const double sample = analogMessageAt(message, msg_len, cover_len, index);
        const double frequency = kCarrierFrequency + kFmDeviation * sample;
        phase += 2.0 * kPi * frequency / kSampleRate;
    }
    return cover_len;
}

int modulate_digital_amplitude(double* cover, int cover_len,
                               const unsigned char* message, int msg_len) {
    if (!hasValidModulationArgs(cover, cover_len, message, msg_len)) {
        return -1;
    }
    if (cover_len == 0) {
        return 0;
    }

    for (int index = 0; index < cover_len; ++index) {
        const unsigned char bit = digitalMessageAt(message, msg_len, cover_len, index);
        const double amplitude = bit == 0U ? kAskAmplitudeZero : kAskAmplitudeOne;
        cover[index] = amplitude * carrierAt(index);
    }
    return cover_len;
}

int modulate_analog_amplitude(double* cover, int cover_len,
                              const double* message, int msg_len) {
    if (!hasValidModulationArgs(cover, cover_len, message, msg_len)) {
        return -1;
    }
    if (cover_len == 0) {
        return 0;
    }

    for (int index = 0; index < cover_len; ++index) {
        const double sample = analogMessageAt(message, msg_len, cover_len, index);
        cover[index] = (1.0 + kAmDepth * sample) * carrierAt(index);
    }
    return cover_len;
}

int modulate_digital_phase(double* cover, int cover_len,
                           const unsigned char* message, int msg_len) {
    if (!hasValidModulationArgs(cover, cover_len, message, msg_len)) {
        return -1;
    }
    if (cover_len == 0) {
        return 0;
    }

    for (int index = 0; index < cover_len; ++index) {
        const unsigned char bit = digitalMessageAt(message, msg_len, cover_len, index);
        const double phase = bit == 0U ? 0.0 : kPi;
        cover[index] = std::sin(carrierPhaseAt(index) + phase);
    }
    return cover_len;
}

int modulate_analog_phase(double* cover, int cover_len,
                          const double* message, int msg_len) {
    if (!hasValidModulationArgs(cover, cover_len, message, msg_len)) {
        return -1;
    }
    if (cover_len == 0) {
        return 0;
    }

    for (int index = 0; index < cover_len; ++index) {
        const double sample = analogMessageAt(message, msg_len, cover_len, index);
        cover[index] = std::sin(carrierPhaseAt(index) + kPmPhaseScale * sample);
    }
    return cover_len;
}
