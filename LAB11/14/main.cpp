#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

struct Packet {
    int seq = 0;
    bool stop = false;
};

template <typename T>
class Channel {
public:
    void push(const T &value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(value);
        }
        condition_.notify_one();
    }

    bool pop_wait(T &value, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!condition_.wait_for(lock, timeout, [&] { return !queue_.empty(); })) {
            return false;
        }
        value = queue_.front();
        queue_.pop();
        return true;
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<T> queue_;
};

struct Options {
    int total_packets = 12;
    int send_window = 5;
    int receive_window = 5;
    double loss_probability = 0.15;
    unsigned int seed = 11;
};

struct SharedState {
    explicit SharedState(int n)
        : sent(n, false), acked(n, false), received(n, false), delivered(n, false), last_sent(n) {}

    std::mutex mutex;
    int send_base = 0;
    int next_seq = 0;
    int receive_base = 0;
    std::vector<bool> sent;
    std::vector<bool> acked;
    std::vector<bool> received;
    std::vector<bool> delivered;
    std::vector<std::chrono::steady_clock::time_point> last_sent;
};

std::mutex output_mutex;

void log_event(const std::string &role, const std::string &message) {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << "[" << role << "] " << message << "\n";
}

Options parse_options(int argc, char **argv) {
    Options options;
    if (argc >= 2) {
        options.total_packets = std::stoi(argv[1]);
    }
    if (argc >= 3) {
        options.send_window = std::stoi(argv[2]);
    }
    if (argc >= 4) {
        options.receive_window = std::stoi(argv[3]);
    }
    if (argc >= 5) {
        options.loss_probability = std::stod(argv[4]);
    }
    if (argc >= 6) {
        options.seed = static_cast<unsigned int>(std::stoul(argv[5]));
    }
    if (argc > 6) {
        throw std::invalid_argument(
            "Usage: sliding_window_demo [total_packets] [send_window] [receive_window] [loss_probability] [seed]");
    }
    if (options.total_packets <= 0 || options.send_window <= 0 || options.receive_window <= 0) {
        throw std::invalid_argument("packet count and window sizes must be positive");
    }
    if (options.loss_probability < 0.0 || options.loss_probability >= 1.0) {
        throw std::invalid_argument("loss_probability must be within [0, 1)");
    }
    return options;
}

std::string packet_list(const std::vector<int> &values) {
    if (values.empty()) {
        return "空";
    }

    std::ostringstream oss;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            oss << ",";
        }
        oss << (values[i] + 1);
    }
    return oss.str();
}

std::vector<int> range_values(int begin, int end, int total) {
    std::vector<int> result;
    const int clamped_begin = std::max(0, begin);
    const int clamped_end = std::min(total, end);
    for (int seq = clamped_begin; seq < clamped_end; ++seq) {
        result.push_back(seq);
    }
    return result;
}

void send_packet(Channel<Packet> &data_channel, SharedState &state, int seq, bool retransmit) {
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.sent[seq] = true;
        state.last_sent[seq] = std::chrono::steady_clock::now();
    }
    data_channel.push(Packet{seq, false});
    log_event("甲方", std::string(retransmit ? "重传分组 " : "发送分组 ") + std::to_string(seq + 1));
}

void sender(Channel<Packet> &data_channel,
            Channel<int> &ack_channel,
            SharedState &state,
            const Options &options,
            std::atomic<bool> &done) {
    constexpr auto retransmit_timeout = std::chrono::milliseconds(700);

    while (true) {
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.send_base >= options.total_packets) {
                break;
            }
        }

        while (true) {
            int seq_to_send = -1;
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                const int window_end = std::min(options.total_packets, state.send_base + options.send_window);
                if (state.next_seq < window_end) {
                    seq_to_send = state.next_seq++;
                }
            }
            if (seq_to_send < 0) {
                break;
            }
            send_packet(data_channel, state, seq_to_send, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
        }

        int ack_seq = -1;
        while (ack_channel.pop_wait(ack_seq, std::chrono::milliseconds(80))) {
            bool accepted = false;
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                if (ack_seq >= 0 && ack_seq < options.total_packets && !state.acked[ack_seq]) {
                    state.acked[ack_seq] = true;
                    accepted = true;
                    while (state.send_base < options.total_packets && state.acked[state.send_base]) {
                        ++state.send_base;
                    }
                }
            }
            if (accepted) {
                log_event("甲方", "收到 ACK " + std::to_string(ack_seq + 1));
            }
        }

        const auto now = std::chrono::steady_clock::now();
        std::vector<int> timeout_packets;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            for (int seq = state.send_base; seq < state.next_seq; ++seq) {
                if (state.sent[seq] && !state.acked[seq] &&
                    now - state.last_sent[seq] >= retransmit_timeout) {
                    timeout_packets.push_back(seq);
                }
            }
        }
        for (const int seq : timeout_packets) {
            send_packet(data_channel, state, seq, true);
        }
    }

    done = true;
    data_channel.push(Packet{0, true});
    log_event("甲方", "全部分组均已确认");
}

void receiver(Channel<Packet> &data_channel,
              Channel<int> &ack_channel,
              SharedState &state,
              const Options &options) {
    std::mt19937 rng(options.seed);
    std::bernoulli_distribution lost(options.loss_probability);

    while (true) {
        Packet packet;
        if (!data_channel.pop_wait(packet, std::chrono::milliseconds(100))) {
            continue;
        }
        if (packet.stop) {
            break;
        }

        if (lost(rng)) {
            log_event("信道", "分组 " + std::to_string(packet.seq + 1) + " 丢失");
            continue;
        }

        bool accepted = false;
        bool duplicate = false;
        bool out_of_window = false;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            const int window_end = std::min(options.total_packets, state.receive_base + options.receive_window);
            if (packet.seq < state.receive_base) {
                duplicate = true;
            } else if (packet.seq >= window_end) {
                out_of_window = true;
            } else {
                if (!state.received[packet.seq]) {
                    state.received[packet.seq] = true;
                    accepted = true;
                } else {
                    duplicate = true;
                }

                while (state.receive_base < options.total_packets && state.received[state.receive_base]) {
                    state.delivered[state.receive_base] = true;
                    ++state.receive_base;
                }
            }
        }

        if (out_of_window) {
            log_event("乙方", "分组 " + std::to_string(packet.seq + 1) + " 不在接收窗口内，丢弃");
            continue;
        }
        if (accepted) {
            log_event("乙方", "接收并缓存分组 " + std::to_string(packet.seq + 1));
        } else if (duplicate) {
            log_event("乙方", "收到重复分组 " + std::to_string(packet.seq + 1) + "，重新确认");
        }

        if (lost(rng)) {
            log_event("信道", "ACK " + std::to_string(packet.seq + 1) + " 丢失");
            continue;
        }
        ack_channel.push(packet.seq);
        log_event("乙方", "发送 ACK " + std::to_string(packet.seq + 1));
    }

    log_event("乙方", "接收线程结束");
}

void monitor(SharedState &state,
             const Options &options,
             const std::atomic<bool> &done) {
    int round = 1;

    while (!done.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        std::vector<int> acked;
        std::vector<int> sent_unacked;
        std::vector<int> allowed_unsent;
        std::vector<int> not_allowed_send;
        std::vector<int> delivered;
        std::vector<int> allowed_receive;
        std::vector<int> not_allowed_receive;
        int notify_window = 0;
        int available_window = 0;

        {
            std::lock_guard<std::mutex> lock(state.mutex);
            for (int seq = 0; seq < options.total_packets; ++seq) {
                if (state.sent[seq] && state.acked[seq]) {
                    acked.push_back(seq);
                } else if (state.sent[seq] && !state.acked[seq]) {
                    sent_unacked.push_back(seq);
                }
            }

            const int send_window_end = std::min(options.total_packets, state.send_base + options.send_window);
            allowed_unsent = range_values(state.next_seq, send_window_end, options.total_packets);
            not_allowed_send = range_values(send_window_end, options.total_packets, options.total_packets);

            for (int seq = 0; seq < state.receive_base; ++seq) {
                delivered.push_back(seq);
            }
            const int receive_window_end =
                std::min(options.total_packets, state.receive_base + options.receive_window);
            allowed_receive = range_values(state.receive_base, receive_window_end, options.total_packets);
            not_allowed_receive = range_values(receive_window_end, options.total_packets, options.total_packets);

            int buffered = 0;
            for (int seq = state.receive_base; seq < receive_window_end; ++seq) {
                if (state.received[seq] && !state.delivered[seq]) {
                    ++buffered;
                }
            }
            const int receive_capacity = receive_window_end - state.receive_base;
            notify_window = std::max(0, receive_capacity - buffered);
            const int in_flight = static_cast<int>(sent_unacked.size());
            const int send_capacity = send_window_end - state.send_base;
            available_window = std::max(0, send_capacity - in_flight);
        }

        std::lock_guard<std::mutex> lock(output_mutex);
        std::cout << "\n[监视线程] 第 " << round++ << " 秒窗口状态\n";
        std::cout << "  已发送并收到确认: " << packet_list(acked) << "\n";
        std::cout << "  已发送但未收到确认: " << packet_list(sent_unacked) << "\n";
        std::cout << "  允许发送但尚未发送: " << packet_list(allowed_unsent) << "\n";
        std::cout << "  不允许发送: " << packet_list(not_allowed_send) << "\n";
        std::cout << "  已发送确认并交付主机: " << packet_list(delivered) << "\n";
        std::cout << "  允许接收: " << packet_list(allowed_receive) << "\n";
        std::cout << "  不允许接收: " << packet_list(not_allowed_receive) << "\n";
        std::cout << "  通知窗口大小: " << notify_window
                  << "，可用窗口大小: " << available_window << "\n\n";
    }
}

int main(int argc, char **argv) {
    try {
        const Options options = parse_options(argc, argv);
        SharedState state(options.total_packets);
        Channel<Packet> data_channel;
        Channel<int> ack_channel;
        std::atomic<bool> done{false};

        std::cout << "滑动窗口模拟开始\n";
        std::cout << "分组数: " << options.total_packets
                  << "，发送窗口: " << options.send_window
                  << "，接收窗口: " << options.receive_window
                  << "，丢包概率: " << std::fixed << std::setprecision(2)
                  << options.loss_probability
                  << "，随机种子: " << options.seed << "\n\n";

        std::thread receiver_thread(receiver, std::ref(data_channel), std::ref(ack_channel),
                                    std::ref(state), std::ref(options));
        std::thread sender_thread(sender, std::ref(data_channel), std::ref(ack_channel),
                                  std::ref(state), std::ref(options), std::ref(done));
        std::thread monitor_thread(monitor, std::ref(state), std::cref(options), std::cref(done));

        sender_thread.join();
        receiver_thread.join();
        monitor_thread.join();

        std::cout << "\n模拟结束。\n";
    } catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
