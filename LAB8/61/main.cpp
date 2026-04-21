#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

struct Fragment {
    int packetLength = 0;
    int offset = 0;
};

namespace {

constexpr int kHeaderLength = 20;

std::vector<Fragment> fragment_once(const Fragment& fragment, int mtu) {
    if (mtu <= kHeaderLength) {
        throw std::invalid_argument("MTU must be greater than 20 bytes.");
    }

    if (fragment.packetLength <= mtu) {
        return {fragment};
    }

    const int max_payload = ((mtu - kHeaderLength) / 8) * 8;
    if (max_payload <= 0) {
        throw std::invalid_argument("MTU leaves no room for payload.");
    }

    const int payload = fragment.packetLength - kHeaderLength;
    std::vector<Fragment> result;
    int remaining = payload;
    int current_offset = fragment.offset;

    while (remaining > max_payload) {
        result.push_back({kHeaderLength + max_payload, current_offset});
        remaining -= max_payload;
        current_offset += max_payload / 8;
    }

    result.push_back({kHeaderLength + remaining, current_offset});
    return result;
}

}  // namespace

std::vector<Fragment> fragmentPacket(int packetLength, const std::vector<int>& pathMTUs) {
    if (packetLength <= kHeaderLength) {
        throw std::invalid_argument("Packet length must be greater than 20 bytes.");
    }

    std::vector<Fragment> fragments = {{packetLength, 0}};

    for (const int mtu : pathMTUs) {
        std::vector<Fragment> next;
        for (const Fragment& fragment : fragments) {
            std::vector<Fragment> pieces = fragment_once(fragment, mtu);
            next.insert(next.end(), pieces.begin(), pieces.end());
        }
        fragments = std::move(next);
    }

    return fragments;
}

int main() {
    try {
        int packet_length = 0;
        int mtu_count = 0;
        if (!(std::cin >> packet_length >> mtu_count)) {
            std::cerr << "Expected: <packet_length> <mtu_count> followed by mtu values.\n";
            return 1;
        }

        if (mtu_count < 0) {
            std::cerr << "mtu_count must be non-negative.\n";
            return 1;
        }

        std::vector<int> path_mtus(mtu_count);
        for (int i = 0; i < mtu_count; ++i) {
            if (!(std::cin >> path_mtus[i])) {
                std::cerr << "Missing MTU value.\n";
                return 1;
            }
        }

        const std::vector<Fragment> fragments = fragmentPacket(packet_length, path_mtus);
        for (std::size_t i = 0; i < fragments.size(); ++i) {
            std::cout << "Fragment " << (i + 1) << ": length=" << fragments[i].packetLength
                      << ", offset=" << fragments[i].offset << "\n";
        }
    } catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
