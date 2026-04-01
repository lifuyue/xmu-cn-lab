#include <chrono>
#include <iomanip>
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

class CSMACDMedium {
public:
    explicit CSMACDMedium(int station_count)
        : transmitting_(station_count, false) {}

    bool sense_busy() {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_count_ > 0;
    }

    bool begin_transmission(int station_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        transmitting_[station_id] = true;
        ++active_count_;
        return active_count_ == 1;
    }

    bool collision_detected() {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_count_ > 1;
    }

    void end_transmission(int station_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (transmitting_[station_id]) {
            transmitting_[station_id] = false;
            --active_count_;
        }
    }

private:
    std::mutex mutex_;
    std::vector<bool> transmitting_;
    int active_count_ = 0;
};

std::mutex output_mutex;

class StartGate {
public:
    explicit StartGate(int participants)
        : participants_(participants) {}

    void arrive_and_wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        ++arrived_;
        if (arrived_ == participants_) {
            released_ = true;
            condition_.notify_all();
            return;
        }
        condition_.wait(lock, [&] { return released_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    int participants_;
    int arrived_ = 0;
    bool released_ = false;
};

void log_event(int station_id, const std::string &message) {
    std::lock_guard<std::mutex> lock(output_mutex);
    std::cout << "[站点 " << station_id << "] " << message << "\n";
}

void station_worker(int station_id,
                    CSMACDMedium &medium,
                    int frames_to_send,
                    StartGate &first_frame_ready_gate,
                    StartGate &first_frame_send_gate) {
    std::mt19937 rng(static_cast<unsigned int>(
        std::chrono::steady_clock::now().time_since_epoch().count() + station_id * 97));
    std::uniform_int_distribution<int> initial_gap(20, 80);
    std::uniform_int_distribution<int> frame_time(60, 120);

    for (int frame = 1; frame <= frames_to_send; ++frame) {
        int collision_count = 0;
        bool delivered = false;

        if (frame == 1) {
            log_event(station_id, "进入首帧竞争阶段，等待所有站点同时开始");
            first_frame_ready_gate.arrive_and_wait();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(initial_gap(rng)));
        }
        log_event(station_id, "准备发送第 " + std::to_string(frame) + " 帧");

        while (!delivered) {
            bool synchronized_first_attempt = false;
            if (frame == 1 && collision_count == 0) {
                while (medium.sense_busy()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                log_event(station_id, "首帧检测到信道空闲，等待同步发射");
                first_frame_send_gate.arrive_and_wait();
                synchronized_first_attempt = true;
            }

            if (!synchronized_first_attempt) {
                while (medium.sense_busy()) {
                    log_event(station_id, "监听到信道忙，继续等待");
                    std::this_thread::sleep_for(std::chrono::milliseconds(30));
                }
            }

            log_event(station_id, "信道空闲，开始发送");
            medium.begin_transmission(station_id);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));

            if (medium.collision_detected()) {
                medium.end_transmission(station_id);
                ++collision_count;
                log_event(station_id, "检测到冲突，发送 JAM 信号并停止发送");

                int k = std::min(collision_count, 10);
                int max_backoff_slots = (1 << k) - 1;
                std::uniform_int_distribution<int> backoff_slot(0, max_backoff_slots);
                int slots = backoff_slot(rng);
                int backoff_ms = slots * 40;
                log_event(station_id,
                          "执行二进制指数退避，等待 " + std::to_string(slots) +
                              " 个时隙（" + std::to_string(backoff_ms) + " ms）");
                std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
                continue;
            }

            int duration = frame_time(rng);
            log_event(station_id, "未检测到冲突，持续发送 " + std::to_string(duration) + " ms");
            std::this_thread::sleep_for(std::chrono::milliseconds(duration));
            medium.end_transmission(station_id);
            delivered = true;
            log_event(station_id, "第 " + std::to_string(frame) + " 帧发送成功");
        }
    }
}

int main() {
    constexpr int station_count = 3;
    constexpr int frames_per_station = 3;

    CSMACDMedium medium(station_count);
    StartGate first_frame_ready_gate(station_count);
    StartGate first_frame_send_gate(station_count);
    std::vector<std::thread> threads;

    std::cout << "CSMA/CD 多线程模拟开始\n";
    std::cout << "站点数: " << station_count << "，每个站点发送帧数: " << frames_per_station << "\n\n";

    for (int station_id = 0; station_id < station_count; ++station_id) {
        threads.emplace_back(
            station_worker,
            station_id,
            std::ref(medium),
            frames_per_station,
            std::ref(first_frame_ready_gate),
            std::ref(first_frame_send_gate));
    }

    for (auto &thread : threads) {
        thread.join();
    }

    std::cout << "\n所有站点发送完成。\n";
    return 0;
}
