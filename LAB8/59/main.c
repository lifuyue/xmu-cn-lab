#include <stdio.h>
#include <stdlib.h>

int is_in_net(unsigned char *ip, unsigned char *netip, unsigned char *mask) {
    for (int i = 0; i < 4; ++i) {
        if ((ip[i] & mask[i]) != (netip[i] & mask[i])) {
            return 0;
        }
    }
    return 1;
}

static int parse_ip(const char *text, unsigned char out[4]) {
    unsigned int a = 0;
    unsigned int b = 0;
    unsigned int c = 0;
    unsigned int d = 0;

    if (sscanf(text, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return 0;
    }
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        return 0;
    }

    out[0] = (unsigned char)a;
    out[1] = (unsigned char)b;
    out[2] = (unsigned char)c;
    out[3] = (unsigned char)d;
    return 1;
}

int main(int argc, char **argv) {
    unsigned char ip[4];
    unsigned char netip[4];
    unsigned char mask[4];

    if (argc != 4) {
        fprintf(stderr, "Usage: %s <ip> <network> <mask>\n", argv[0]);
        return 1;
    }

    if (!parse_ip(argv[1], ip) || !parse_ip(argv[2], netip) || !parse_ip(argv[3], mask)) {
        fprintf(stderr, "Invalid IPv4 address.\n");
        return 1;
    }

    printf("%d\n", is_in_net(ip, netip, mask));
    return 0;
}
