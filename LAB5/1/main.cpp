#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>

#define MAC_ADDRESS_LENGTH 6

typedef std::array<std::uint8_t, MAC_ADDRESS_LENGTH> MAC_address;

struct EthernetFrame {
    MAC_address destination;
    MAC_address source;
    std::uint16_t ether_type;
};

MAC_address this_mac_address = {0x00, 0x1A, 0x2B, 0x3C, 0x4D, 0x5E};

int mac_address_match(const struct EthernetFrame *frame) {
    if (frame == nullptr) {
        return 0;
    }

    const MAC_address &destination = frame->destination;

    if (destination == this_mac_address) {
        return 1;
    }

    if (destination[0] & 0x01U) {
        return 1;
    }

    const MAC_address broadcast = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return destination == broadcast ? 1 : 0;
}

std::ostream &operator<<(std::ostream &os, const MAC_address &address) {
    os << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < address.size(); ++i) {
        os << std::setw(2) << static_cast<int>(address[i]);
        if (i + 1 != address.size()) {
            os << ":";
        }
    }
    os << std::dec;
    return os;
}

void print_result(const char *label, const EthernetFrame &frame) {
    std::cout << label << "\n";
    std::cout << "  目的 MAC: " << frame.destination << "\n";
    std::cout << "  是否接收: " << mac_address_match(&frame) << "\n\n";
}

int main() {
    EthernetFrame unicast_to_me = {
        this_mac_address,
        {0x10, 0x20, 0x30, 0x40, 0x50, 0x60},
        0x0800,
    };

    EthernetFrame multicast = {
        {0x01, 0x00, 0x5E, 0x00, 0x00, 0x01},
        {0x10, 0x20, 0x30, 0x40, 0x50, 0x60},
        0x0800,
    };

    EthernetFrame broadcast = {
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
        {0x10, 0x20, 0x30, 0x40, 0x50, 0x60},
        0x0806,
    };

    EthernetFrame other_host = {
        {0x02, 0x11, 0x22, 0x33, 0x44, 0x55},
        {0x10, 0x20, 0x30, 0x40, 0x50, 0x60},
        0x86DD,
    };

    std::cout << "本机 MAC 地址: " << this_mac_address << "\n\n";
    print_result("1. 发给本机的单播帧", unicast_to_me);
    print_result("2. 多播帧", multicast);
    print_result("3. 广播帧", broadcast);
    print_result("4. 发给其他主机的单播帧", other_host);

    return 0;
}
