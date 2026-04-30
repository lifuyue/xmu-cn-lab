#include "icmp_time_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char **argv) {
    const char *listen_ip = "";
    int max_count = 0;
    int replies_sent = 0;
    int sock;

    if (argc >= 2) {
        listen_ip = argv[1];
    }
    if (argc >= 3) {
        max_count = atoi(argv[2]);
        if (max_count < 0) {
            fprintf(stderr, "count must be non-negative.\n");
            return 1;
        }
    }

    sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        if (errno == EPERM || errno == EACCES) {
            fprintf(stderr, "raw socket requires administrator privileges, please rerun with sudo\n");
        } else {
            perror("socket");
        }
        return 1;
    }

    printf("ICMP timestamp server is listening\n");
    if (listen_ip[0] != '\0') {
        printf("reply filter enabled: only packets for %s\n", listen_ip);
    }

    while (max_count == 0 || replies_sent < max_count) {
        uint8_t buffer[65535];
        uint8_t reply[ICMP_TIMESTAMP_SIZE];
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        ssize_t nread;
        struct ipv4_icmp_view view;
        struct icmp_timestamp_message message;
        struct sockaddr_in target;
        uint32_t receive_ms;
        char receive_text[32];
        const char *source_ip;
        const char *destination_ip;

        nread = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&peer, &peer_len);
        if (nread <= 0) {
            continue;
        }
        receive_ms = utc_day_milliseconds();

        if (!parse_ipv4_icmp(buffer, (size_t)nread, &view)) {
            continue;
        }
        if (listen_ip[0] != '\0' && view.has_ipv4_header && strcmp(view.destination_ip, listen_ip) != 0) {
            continue;
        }
        if (!parse_timestamp_message(view.icmp_data, view.icmp_len, &message)) {
            continue;
        }
        if (message.type != ICMP_TIMESTAMP_REQUEST || message.code != ICMP_TIMESTAMP_CODE) {
            continue;
        }
        if (!icmp_packet_checksum_valid(view.icmp_data, view.icmp_len)) {
            continue;
        }
        if (!build_timestamp_reply(view.icmp_data, view.icmp_len, receive_ms, utc_day_milliseconds(), reply)) {
            continue;
        }

        memset(&target, 0, sizeof(target));
        target.sin_family = AF_INET;
        if (view.has_ipv4_header) {
            if (inet_pton(AF_INET, view.source_ip, &target.sin_addr) != 1) {
                continue;
            }
            source_ip = view.source_ip;
            destination_ip = view.destination_ip;
        } else {
            target.sin_addr = peer.sin_addr;
            source_ip = inet_ntoa(peer.sin_addr);
            destination_ip = listen_ip[0] == '\0' ? "<local>" : listen_ip;
        }

        if (sendto(sock, reply, sizeof(reply), 0, (struct sockaddr *)&target, sizeof(target)) < 0) {
            perror("sendto");
            continue;
        }
        ++replies_sent;

        format_utc_day_milliseconds(receive_ms, receive_text, sizeof(receive_text));
        printf("reply to %s: id=0x%04x seq=%u request_for=%s receive=%s\n",
               source_ip,
               message.identifier,
               message.sequence,
               destination_ip,
               receive_text);
    }

    close(sock);
    return 0;
}
