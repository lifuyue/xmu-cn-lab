#ifndef ICMP_TIME_COMMON_H
#define ICMP_TIME_COMMON_H

#include <stddef.h>
#include <stdint.h>

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif

#define ICMP_TIMESTAMP_REQUEST 13
#define ICMP_TIMESTAMP_REPLY 14
#define ICMP_TIMESTAMP_CODE 0
#define ICMP_TIMESTAMP_SIZE 20
#define ICMP_DAY_MILLISECONDS 86400000U

struct icmp_timestamp_message {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
    uint32_t originate_ms;
    uint32_t receive_ms;
    uint32_t transmit_ms;
};

struct ipv4_icmp_view {
    char source_ip[INET_ADDRSTRLEN];
    char destination_ip[INET_ADDRSTRLEN];
    uint8_t ttl;
    const uint8_t *icmp_data;
    size_t icmp_len;
    size_t ip_header_length;
    int has_ipv4_header;
};

uint16_t icmp_checksum(const uint8_t *data, size_t length);
uint32_t utc_day_milliseconds(void);
uint32_t elapsed_milliseconds(uint32_t start_ms, uint32_t end_ms);
void format_utc_day_milliseconds(uint32_t value, char *out, size_t out_size);

void build_timestamp_request(uint16_t identifier,
                             uint16_t sequence,
                             uint32_t originate_ms,
                             uint8_t out[ICMP_TIMESTAMP_SIZE]);
int build_timestamp_reply(const uint8_t *request,
                          size_t request_len,
                          uint32_t receive_ms,
                          uint32_t transmit_ms,
                          uint8_t out[ICMP_TIMESTAMP_SIZE]);
int parse_timestamp_message(const uint8_t *packet,
                            size_t packet_len,
                            struct icmp_timestamp_message *out);
int parse_ipv4_icmp(const uint8_t *packet, size_t packet_len, struct ipv4_icmp_view *out);
int icmp_packet_checksum_valid(const uint8_t *packet, size_t packet_len);

#endif
