#include "icmp_time_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

static double now_milliseconds(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

static int resolve_ipv4(const char *host, struct sockaddr_in *out, char *ip_text, size_t ip_text_size) {
    struct addrinfo hints;
    struct addrinfo *result = NULL;
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_RAW;

    rc = getaddrinfo(host, NULL, &hints, &result);
    if (rc != 0 || result == NULL) {
        fprintf(stderr, "failed to resolve %s: %s\n", host, gai_strerror(rc));
        return 0;
    }

    memcpy(out, result->ai_addr, sizeof(*out));
    inet_ntop(AF_INET, &out->sin_addr, ip_text, ip_text_size);
    freeaddrinfo(result);
    return 1;
}

static int receive_reply(int sock,
                         uint16_t identifier,
                         uint16_t sequence,
                         double timeout_seconds,
                         char *source_ip,
                         size_t source_ip_size,
                         uint8_t *ttl,
                         int *has_ttl,
                         size_t *packet_size,
                         struct icmp_timestamp_message *reply) {
    double deadline = now_milliseconds() + timeout_seconds * 1000.0;

    for (;;) {
        uint8_t buffer[65535];
        struct sockaddr_in peer;
        socklen_t peer_len = sizeof(peer);
        struct timeval tv;
        fd_set readfds;
        ssize_t nread;
        double remaining_ms = deadline - now_milliseconds();
        struct ipv4_icmp_view view;
        struct icmp_timestamp_message message;

        if (remaining_ms <= 0.0) {
            return 0;
        }

        tv.tv_sec = (time_t)(remaining_ms / 1000.0);
        tv.tv_usec = (suseconds_t)((remaining_ms - (double)tv.tv_sec * 1000.0) * 1000.0);
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);

        if (select(sock + 1, &readfds, NULL, NULL, &tv) <= 0) {
            return 0;
        }

        nread = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&peer, &peer_len);
        if (nread <= 0) {
            continue;
        }
        if (!parse_ipv4_icmp(buffer, (size_t)nread, &view)) {
            continue;
        }
        if (!parse_timestamp_message(view.icmp_data, view.icmp_len, &message)) {
            continue;
        }
        if (message.type != ICMP_TIMESTAMP_REPLY ||
            message.code != ICMP_TIMESTAMP_CODE ||
            message.identifier != identifier ||
            message.sequence != sequence) {
            continue;
        }
        if (!icmp_packet_checksum_valid(view.icmp_data, view.icmp_len)) {
            continue;
        }

        if (view.has_ipv4_header) {
            snprintf(source_ip, source_ip_size, "%s", view.source_ip);
            *ttl = view.ttl;
            *has_ttl = 1;
            *packet_size = view.icmp_len;
        } else {
            inet_ntop(AF_INET, &peer.sin_addr, source_ip, source_ip_size);
            *has_ttl = 0;
            *packet_size = view.icmp_len;
        }
        *reply = message;
        return 1;
    }
}

int main(int argc, char **argv) {
    const char *host;
    int count = 4;
    double interval = 1.0;
    double timeout = 1.0;
    int ttl_value = 64;
    uint16_t identifier = (uint16_t)(getpid() & 0xFFFF);
    struct sockaddr_in target;
    char target_ip[INET_ADDRSTRLEN];
    int sock;
    int transmitted = 0;
    int received = 0;
    double min_rtt = 0.0;
    double max_rtt = 0.0;
    double sum_rtt = 0.0;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <host> [count]\n", argv[0]);
        return 1;
    }
    host = argv[1];
    if (argc >= 3) {
        count = atoi(argv[2]);
        if (count <= 0) {
            fprintf(stderr, "count must be positive.\n");
            return 1;
        }
    }

    if (!resolve_ipv4(host, &target, target_ip, sizeof(target_ip))) {
        return 1;
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

    setsockopt(sock, IPPROTO_IP, IP_TTL, &ttl_value, sizeof(ttl_value));
    printf("ICMP TIMESTAMP %s (%s)\n", host, target_ip);

    for (int sequence = 1; sequence <= count; ++sequence) {
        uint8_t request[ICMP_TIMESTAMP_SIZE];
        struct icmp_timestamp_message request_message;
        struct icmp_timestamp_message reply_message;
        char source_ip[INET_ADDRSTRLEN];
        char originate_text[32];
        char receive_text[32];
        char transmit_text[32];
        uint8_t reply_ttl = 0;
        int has_ttl = 0;
        size_t packet_size = 0;
        uint32_t originate_ms = utc_day_milliseconds();
        double start_ms;

        build_timestamp_request(identifier, (uint16_t)sequence, originate_ms, request);
        parse_timestamp_message(request, sizeof(request), &request_message);

        start_ms = now_milliseconds();
        if (sendto(sock, request, sizeof(request), 0, (struct sockaddr *)&target, sizeof(target)) < 0) {
            perror("sendto");
            close(sock);
            return 1;
        }
        ++transmitted;

        if (!receive_reply(sock,
                           identifier,
                           (uint16_t)sequence,
                           timeout,
                           source_ip,
                           sizeof(source_ip),
                           &reply_ttl,
                           &has_ttl,
                           &packet_size,
                           &reply_message)) {
            printf("Request timeout for icmp_seq %d\n", sequence);
        } else {
            double rtt = now_milliseconds() - start_ms;
            uint32_t server_wait = elapsed_milliseconds(reply_message.receive_ms, reply_message.transmit_ms);

            if (received == 0 || rtt < min_rtt) {
                min_rtt = rtt;
            }
            if (received == 0 || rtt > max_rtt) {
                max_rtt = rtt;
            }
            sum_rtt += rtt;
            ++received;

            format_utc_day_milliseconds(request_message.originate_ms, originate_text, sizeof(originate_text));
            format_utc_day_milliseconds(reply_message.receive_ms, receive_text, sizeof(receive_text));
            format_utc_day_milliseconds(reply_message.transmit_ms, transmit_text, sizeof(transmit_text));

            printf("%zu bytes from %s: icmp_seq=%u", packet_size, source_ip, reply_message.sequence);
            if (has_ttl) {
                printf(" ttl=%u", reply_ttl);
            }
            printf(" time=%.3f ms originate=%s receive=%s transmit=%s server_wait=%u ms\n",
                   rtt,
                   originate_text,
                   receive_text,
                   transmit_text,
                   server_wait);
        }

        if (sequence != count) {
            usleep((useconds_t)(interval * 1000000.0));
        }
    }

    printf("\n--- %s icmp timestamp statistics ---\n", host);
    printf("%d packets transmitted, %d packets received, %.1f%% packet loss\n",
           transmitted,
           received,
           transmitted == 0 ? 0.0 : (double)(transmitted - received) * 100.0 / (double)transmitted);
    if (received > 0) {
        printf("round-trip min/avg/max = %.3f/%.3f/%.3f ms\n",
               min_rtt,
               sum_rtt / (double)received,
               max_rtt);
    }

    close(sock);
    return received > 0 ? 0 : 1;
}
