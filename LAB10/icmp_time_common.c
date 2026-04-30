#include "icmp_time_common.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static uint16_t read_u16(const uint8_t *data) {
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t read_u32(const uint8_t *data) {
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static void write_u16(uint8_t *data, uint16_t value) {
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)(value & 0xFF);
}

static void write_u32(uint8_t *data, uint32_t value) {
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)((value >> 16) & 0xFF);
    data[2] = (uint8_t)((value >> 8) & 0xFF);
    data[3] = (uint8_t)(value & 0xFF);
}

uint16_t icmp_checksum(const uint8_t *data, size_t length) {
    uint32_t sum = 0;

    while (length > 1) {
        sum += read_u16(data);
        data += 2;
        length -= 2;
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    if (length == 1) {
        sum += (uint16_t)(data[0] << 8);
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFFU) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFFU);
}

uint32_t utc_day_milliseconds(void) {
    struct timeval tv;
    struct tm utc_tm;
    time_t seconds;
    uint32_t day_seconds;

    gettimeofday(&tv, NULL);
    seconds = tv.tv_sec;
    gmtime_r(&seconds, &utc_tm);
    day_seconds = (uint32_t)(utc_tm.tm_hour * 3600 + utc_tm.tm_min * 60 + utc_tm.tm_sec);
    return day_seconds * 1000U + (uint32_t)(tv.tv_usec / 1000);
}

uint32_t elapsed_milliseconds(uint32_t start_ms, uint32_t end_ms) {
    return (end_ms + ICMP_DAY_MILLISECONDS - start_ms) % ICMP_DAY_MILLISECONDS;
}

void format_utc_day_milliseconds(uint32_t value, char *out, size_t out_size) {
    uint32_t hours;
    uint32_t minutes;
    uint32_t seconds;
    uint32_t milliseconds;

    value %= ICMP_DAY_MILLISECONDS;
    hours = value / 3600000U;
    value %= 3600000U;
    minutes = value / 60000U;
    value %= 60000U;
    seconds = value / 1000U;
    milliseconds = value % 1000U;
    snprintf(out, out_size, "%02u:%02u:%02u.%03u UTC", hours, minutes, seconds, milliseconds);
}

static void write_timestamp_packet(uint8_t type,
                                   uint16_t identifier,
                                   uint16_t sequence,
                                   uint32_t originate_ms,
                                   uint32_t receive_ms,
                                   uint32_t transmit_ms,
                                   uint8_t out[ICMP_TIMESTAMP_SIZE]) {
    memset(out, 0, ICMP_TIMESTAMP_SIZE);
    out[0] = type;
    out[1] = ICMP_TIMESTAMP_CODE;
    write_u16(out + 4, identifier);
    write_u16(out + 6, sequence);
    write_u32(out + 8, originate_ms);
    write_u32(out + 12, receive_ms);
    write_u32(out + 16, transmit_ms);
    write_u16(out + 2, icmp_checksum(out, ICMP_TIMESTAMP_SIZE));
}

void build_timestamp_request(uint16_t identifier,
                             uint16_t sequence,
                             uint32_t originate_ms,
                             uint8_t out[ICMP_TIMESTAMP_SIZE]) {
    write_timestamp_packet(ICMP_TIMESTAMP_REQUEST, identifier, sequence, originate_ms, 0, 0, out);
}

int build_timestamp_reply(const uint8_t *request,
                          size_t request_len,
                          uint32_t receive_ms,
                          uint32_t transmit_ms,
                          uint8_t out[ICMP_TIMESTAMP_SIZE]) {
    struct icmp_timestamp_message message;

    if (!parse_timestamp_message(request, request_len, &message)) {
        return 0;
    }
    if (message.type != ICMP_TIMESTAMP_REQUEST || message.code != ICMP_TIMESTAMP_CODE) {
        return 0;
    }

    write_timestamp_packet(ICMP_TIMESTAMP_REPLY,
                           message.identifier,
                           message.sequence,
                           message.originate_ms,
                           receive_ms,
                           transmit_ms,
                           out);
    return 1;
}

int parse_timestamp_message(const uint8_t *packet,
                            size_t packet_len,
                            struct icmp_timestamp_message *out) {
    if (packet_len < ICMP_TIMESTAMP_SIZE) {
        return 0;
    }

    out->type = packet[0];
    out->code = packet[1];
    out->checksum = read_u16(packet + 2);
    out->identifier = read_u16(packet + 4);
    out->sequence = read_u16(packet + 6);
    out->originate_ms = read_u32(packet + 8);
    out->receive_ms = read_u32(packet + 12);
    out->transmit_ms = read_u32(packet + 16);
    return 1;
}

int parse_ipv4_icmp(const uint8_t *packet, size_t packet_len, struct ipv4_icmp_view *out) {
    memset(out, 0, sizeof(*out));
    if (packet_len >= 20 && (packet[0] >> 4) == 4) {
        size_t header_length = (size_t)(packet[0] & 0x0F) * 4U;

        if (header_length < 20 || packet_len < header_length + 8) {
            return 0;
        }
        if (inet_ntop(AF_INET, packet + 12, out->source_ip, sizeof(out->source_ip)) == NULL) {
            return 0;
        }
        if (inet_ntop(AF_INET, packet + 16, out->destination_ip, sizeof(out->destination_ip)) == NULL) {
            return 0;
        }

        out->ttl = packet[8];
        out->icmp_data = packet + header_length;
        out->icmp_len = packet_len - header_length;
        out->ip_header_length = header_length;
        out->has_ipv4_header = 1;
        return 1;
    }

    if (packet_len >= 8) {
        out->icmp_data = packet;
        out->icmp_len = packet_len;
        return 1;
    }
    return 0;
}

int icmp_packet_checksum_valid(const uint8_t *packet, size_t packet_len) {
    return icmp_checksum(packet, packet_len) == 0;
}
