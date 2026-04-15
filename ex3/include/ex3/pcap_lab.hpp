#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include <sys/time.h>

namespace ex3 {

struct CaptureOptions {
    std::string read_file;
    std::string interface_name;
    std::string filter_expr;
    int snaplen = 65535;
    int timeout_ms = 1000;
    bool promiscuous = true;
};

struct PacketView {
    timeval timestamp {};
    std::uint32_t frame_length = 0;
    std::string src_mac = "00-00-00-00-00-00";
    std::string dst_mac = "00-00-00-00-00-00";
    std::string src_ip;
    std::string dst_ip;
    std::uint16_t src_port = 0;
    std::uint16_t dst_port = 0;
    bool has_tcp = false;
    std::vector<std::uint8_t> payload;
};

struct HttpMessage {
    std::string start_line;
    std::map<std::string, std::string> headers;
    std::string body;
};

std::optional<PacketView> decode_packet(
    const timeval& timestamp,
    std::uint32_t frame_length,
    const std::uint8_t* bytes,
    std::size_t captured_length,
    int datalink);

bool run_capture(
    const CaptureOptions& options,
    const std::function<bool(const PacketView&)>& callback,
    std::string* error);

std::string format_timestamp(const timeval& timestamp);
std::string trim(std::string_view value);
std::string to_lower_copy(std::string_view value);
bool contains_case_insensitive(std::string_view text, std::string_view needle);
bool starts_with_code(std::string_view line, std::string_view code);
std::map<std::string, std::string> parse_form_urlencoded(std::string_view body);
std::vector<std::string> drain_lines(std::string& buffer);
std::vector<HttpMessage> drain_http_messages(std::string& buffer);
void write_csv_row(std::ostream& out, const std::vector<std::string>& fields);

}  // namespace ex3
