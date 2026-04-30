#include "icmp_time_common.h"

#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_request_and_reply(void) {
    uint8_t request[ICMP_TIMESTAMP_SIZE];
    uint8_t reply[ICMP_TIMESTAMP_SIZE];
    struct icmp_timestamp_message message;

    build_timestamp_request(0x1234, 7, 12345678U, request);
    assert(icmp_packet_checksum_valid(request, sizeof(request)));
    assert(parse_timestamp_message(request, sizeof(request), &message));
    assert(message.type == ICMP_TIMESTAMP_REQUEST);
    assert(message.code == 0);
    assert(message.identifier == 0x1234);
    assert(message.sequence == 7);
    assert(message.originate_ms == 12345678U);
    assert(message.receive_ms == 0);
    assert(message.transmit_ms == 0);

    assert(build_timestamp_reply(request, sizeof(request), 1200U, 1210U, reply));
    assert(icmp_packet_checksum_valid(reply, sizeof(reply)));
    assert(parse_timestamp_message(reply, sizeof(reply), &message));
    assert(message.type == ICMP_TIMESTAMP_REPLY);
    assert(message.identifier == 0x1234);
    assert(message.sequence == 7);
    assert(message.originate_ms == 12345678U);
    assert(message.receive_ms == 1200U);
    assert(message.transmit_ms == 1210U);
}

static void test_time_helpers(void) {
    char text[32];

    assert(elapsed_milliseconds(86399990U, 5U) == 15U);
    format_utc_day_milliseconds(3661007U, text, sizeof(text));
    assert(strcmp(text, "01:01:01.007 UTC") == 0);
}

static void test_ipv4_parse(void) {
    uint8_t request[ICMP_TIMESTAMP_SIZE];
    uint8_t packet[20 + ICMP_TIMESTAMP_SIZE];
    struct ipv4_icmp_view view;

    build_timestamp_request(1, 2, 3000U, request);
    memset(packet, 0, sizeof(packet));
    packet[0] = 0x45;
    packet[2] = 0;
    packet[3] = (uint8_t)sizeof(packet);
    packet[8] = 64;
    packet[9] = 1;
    inet_pton(AF_INET, "192.0.2.10", packet + 12);
    inet_pton(AF_INET, "198.51.100.20", packet + 16);
    memcpy(packet + 20, request, sizeof(request));

    assert(parse_ipv4_icmp(packet, sizeof(packet), &view));
    assert(view.has_ipv4_header);
    assert(strcmp(view.source_ip, "192.0.2.10") == 0);
    assert(strcmp(view.destination_ip, "198.51.100.20") == 0);
    assert(view.ttl == 64);
    assert(view.icmp_len == sizeof(request));
    assert(memcmp(view.icmp_data, request, sizeof(request)) == 0);
}

int main(void) {
    test_request_and_reply();
    test_time_helpers();
    test_ipv4_parse();
    printf("All tests passed.\n");
    return 0;
}
