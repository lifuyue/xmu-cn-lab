#ifndef MODULATION_HPP
#define MODULATION_HPP

int generate_cover_signal(double* cover, int size);
int simulate_digital_modulation_signal(unsigned char* message, int size);
int simulate_analog_modulation_signal(double* message, int size);

int modulate_digital_frequency(double* cover, int cover_len,
                               const unsigned char* message, int msg_len);
int modulate_analog_frequency(double* cover, int cover_len,
                              const double* message, int msg_len);

int modulate_digital_amplitude(double* cover, int cover_len,
                               const unsigned char* message, int msg_len);
int modulate_analog_amplitude(double* cover, int cover_len,
                              const double* message, int msg_len);

int modulate_digital_phase(double* cover, int cover_len,
                           const unsigned char* message, int msg_len);
int modulate_analog_phase(double* cover, int cover_len,
                          const double* message, int msg_len);

#endif  // MODULATION_HPP
