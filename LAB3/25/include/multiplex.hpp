#ifndef MULTIPLEX_HPP
#define MULTIPLEX_HPP

namespace tdm_async {

int multiplex(unsigned char* c, int c_size,
              const unsigned char* a, int a_len,
              const unsigned char* b, int b_len);

int demultiplex(unsigned char* a, int a_size,
                unsigned char* b, int b_size,
                const unsigned char* c, int c_len);

}  // namespace tdm_async

namespace tdm_sync {

int multiplex(unsigned char* c, int c_size,
              const unsigned char* a, int a_len,
              const unsigned char* b, int b_len);

int demultiplex(unsigned char* a, int a_size,
                unsigned char* b, int b_size,
                const unsigned char* c, int c_len);

}  // namespace tdm_sync

namespace fdm {

int multiplex(unsigned char* c, int c_size,
              const unsigned char* a, int a_len,
              const unsigned char* b, int b_len);

int demultiplex(unsigned char* a, int a_size,
                unsigned char* b, int b_size,
                const unsigned char* c, int c_len);

}  // namespace fdm

namespace cdm {

int multiplex(unsigned char* c, int c_size,
              const unsigned char* a, int a_len,
              const unsigned char* b, int b_len);

int demultiplex(unsigned char* a, int a_size,
                unsigned char* b, int b_size,
                const unsigned char* c, int c_len);

}  // namespace cdm

#endif  // MULTIPLEX_HPP
