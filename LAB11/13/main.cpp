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

struct Frame {
    int id = 0;
    int seq = 0;
    bool stop = false;
};

struct Ack {
    int frame_id = 0;
    int seq = 0;
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
    int frame_count = 8;
    double loss_probability = 0.25;
    unsigned int seed = 7;
};

std::mutex output_mutex;

void log_event(const std::string &role, const std::string &message) {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << "[" << role << "] " << message << "\n";
}

Options parse_options(int argc, char **argv) {
    Options options;
    if (argc >= 2) {
        options.frame_count = std::stoi(argv[1]);
    }
    if (argc >= 3) {
        options.loss_probability = std::stod(argv[2]);
    }
    if (argc >= 4) {
        options.seed = static_cast<unsigned int>(std::stoul(argv[3]));
    }
    if (argc > 4) {
        throw std::invalid_argument("Usage: stop_and_wait_demo [frame_count] [loss_probability] [seed]");
    }
    if (options.frame_count <= 0) {
        throw std::invalid_argument("frame_count must be positive");
    }
    if (options.loss_probability < 0.0 || options.loss_probability >= 1.0) {
        throw std::invalid_argument("loss_probability must be within [0, 1)");
    }
    return options;
}

void sender(Channel<Frame> &data_channel,
            Channel<Ack> &ack_channel,
            const Options &options) {
    constexpr auto timeout = std::chrono::milliseconds(500);
    int next_seq = 0;

    for (int frame_id = 1; frame_id <= options.frame_count; ++frame_id) {
        bool acknowledged = false;
        int attempts = 0;

        while (!acknowledged) {
            ++attempts;
            Frame frame{frame_id, next_seq, false};
            data_channel.push(frame);

            std::ostringstream oss;
            oss << "发送帧 " << frame_id << "，seq=" << next_seq
                << "，第 " << attempts << " 次尝试";
            log_event("甲方", oss.str());

            Ack ack;
            if (!ack_channel.pop_wait(ack, timeout)) {
                log_event("甲方", "等待 ACK 超时，重传当前帧");
                continue;
            }

            if (ack.frame_id == frame_id && ack.seq == next_seq) {
                log_event("甲方", "收到 ACK，帧 " + std::to_string(frame_id) + " 发送完成");
                acknowledged = true;
                next_seq ^= 1;
            } else {
                log_event("甲方", "收到过期 ACK，忽略并继续等待");
            }
        }
    }

    data_channel.push(Frame{0, 0, true});
    log_event("甲方", "全部帧发送完成");
}

void receiver(Channel<Frame> &data_channel,
              Channel<Ack> &ack_channel,
              const Options &options) {
    std::mt19937 rng(options.seed);
    std::bernoulli_distribution lost(options.loss_probability);
    int expected_seq = 0;
    int delivered = 0;

    while (true) {
        Frame frame;
        if (!data_channel.pop_wait(frame, std::chrono::milliseconds(100))) {
            continue;
        }
        if (frame.stop) {
            break;
        }

        if (lost(rng)) {
            log_event("信道", "帧 " + std::to_string(frame.id) + " 丢失");
            continue;
        }

        if (frame.seq == expected_seq) {
            ++delivered;
            log_event("乙方", "收到新帧 " + std::to_string(frame.id) +
                                   "，交付主机，seq=" + std::to_string(frame.seq));
            expected_seq ^= 1;
        } else {
            log_event("乙方", "收到重复帧 " + std::to_string(frame.id) +
                                   "，丢弃数据但重新确认");
        }

        if (lost(rng)) {
            log_event("信道", "ACK(seq=" + std::to_string(frame.seq) + ") 丢失");
            continue;
        }

        ack_channel.push(Ack{frame.id, frame.seq});
        log_event("乙方", "返回 ACK，seq=" + std::to_string(frame.seq));
    }

    log_event("乙方", "接收结束，共交付 " + std::to_string(delivered) + " 帧");
}

int main(int argc, char **argv) {
    try {
        const Options options = parse_options(argc, argv);
        Channel<Frame> data_channel;
        Channel<Ack> ack_channel;

        std::cout << "停等协议模拟开始\n";
        std::cout << "帧数量: " << options.frame_count
                  << "，丢包概率: " << std::fixed << std::setprecision(2)
                  << options.loss_probability << "，随机种子: " << options.seed << "\n\n";

        std::thread receiver_thread(receiver, std::ref(data_channel), std::ref(ack_channel), std::ref(options));
        std::thread sender_thread(sender, std::ref(data_channel), std::ref(ack_channel), std::ref(options));

        sender_thread.join();
        receiver_thread.join();

        std::cout << "\n模拟结束。\n";
    } catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
