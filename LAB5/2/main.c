#include <stdint.h>
#include <stdio.h>

#define MAC_ADDRESS_LENGTH 6

struct ethernet_frame_header {
    uint8_t destination[MAC_ADDRESS_LENGTH];
    uint8_t source[MAC_ADDRESS_LENGTH];
    uint16_t type_or_length;
};

static void print_mac(const uint8_t mac[MAC_ADDRESS_LENGTH]) {
    for (int i = 0; i < MAC_ADDRESS_LENGTH; ++i) {
        printf("%02X", mac[i]);
        if (i + 1 != MAC_ADDRESS_LENGTH) {
            printf(":");
        }
    }
}

int main(void) {
    struct ethernet_frame_header header = {
        .destination = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        .source = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E},
        .type_or_length = 0x0800,
    };

    printf("传统以太网帧头部长度: %zu 字节\n", sizeof(struct ethernet_frame_header));
    printf("目的地址: ");
    print_mac(header.destination);
    printf("\n源地址: ");
    print_mac(header.source);
    printf("\n类型字段: 0x%04X\n", header.type_or_length);

    return 0;
}
