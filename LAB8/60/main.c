#include <stdio.h>
#include <stdlib.h>

int classwise(unsigned char *ip) {
    const unsigned char first = ip[0];

    if (first < 128) {
        return 0;
    }
    if (first < 192) {
        return 1;
    }
    if (first < 224) {
        return 2;
    }
    if (first < 240) {
        return 3;
    }
    return 4;
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
    static const char *labels[] = {"A", "B", "C", "D", "E"};

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <ip>\n", argv[0]);
        return 1;
    }

    if (!parse_ip(argv[1], ip)) {
        fprintf(stderr, "Invalid IPv4 address.\n");
        return 1;
    }

    const int kind = classwise(ip);
    printf("%d (%s)\n", kind, labels[kind]);
    return 0;
}
