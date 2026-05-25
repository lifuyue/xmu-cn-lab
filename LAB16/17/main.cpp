#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

struct UserRecord {
    std::string password_hash;
    std::string email;
    std::string code;
    std::time_t expires_at = 0;
    bool active = false;
};

std::unordered_map<std::string, UserRecord> users;

std::string fnv1a_hex(const std::string &text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }

    std::ostringstream out;
    out << std::hex << std::uppercase << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

std::string html_escape(const std::string &text) {
    std::string escaped;
    for (char c : text) {
        switch (c) {
            case '&':
                escaped += "&amp;";
                break;
            case '<':
                escaped += "&lt;";
                break;
            case '>':
                escaped += "&gt;";
                break;
            case '"':
                escaped += "&quot;";
                break;
            default:
                escaped += c;
        }
    }
    return escaped;
}

int from_hex(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string url_decode(const std::string &text) {
    std::string decoded;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '+') {
            decoded += ' ';
        } else if (text[i] == '%' && i + 2 < text.size()) {
            int hi = from_hex(text[i + 1]);
            int lo = from_hex(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                decoded += static_cast<char>((hi << 4) | lo);
                i += 2;
            } else {
                decoded += text[i];
            }
        } else {
            decoded += text[i];
        }
    }
    return decoded;
}

std::unordered_map<std::string, std::string> parse_kv(const std::string &text) {
    std::unordered_map<std::string, std::string> values;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        std::size_t end = text.find('&', begin);
        std::string pair = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        std::size_t equal = pair.find('=');
        if (equal != std::string::npos) {
            values[url_decode(pair.substr(0, equal))] = url_decode(pair.substr(equal + 1));
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return values;
}

std::string status_text(int status) {
    switch (status) {
        case 200:
            return "OK";
        case 302:
            return "Found";
        case 400:
            return "Bad Request";
        case 404:
            return "Not Found";
        default:
            return "OK";
    }
}

std::string http_response(int status, const std::string &body, const std::string &content_type = "text/html; charset=utf-8") {
    std::ostringstream out;
    out << "HTTP/1.1 " << status << ' ' << status_text(status) << "\r\n"
        << "Content-Type: " << content_type << "\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return out.str();
}

std::string page(const std::string &title, const std::string &body) {
    std::ostringstream html;
    html << "<!doctype html><html><head><meta charset=\"utf-8\">"
         << "<title>" << html_escape(title) << "</title>"
         << "<style>body{font-family:Arial,sans-serif;max-width:760px;margin:40px auto;line-height:1.6}"
         << "label{display:block;margin:12px 0}input{padding:8px;width:280px}"
         << "button{padding:8px 14px}code{background:#f2f2f2;padding:2px 4px}</style>"
         << "</head><body><h1>" << html_escape(title) << "</h1>"
         << body << "</body></html>";
    return html.str();
}

void cleanup_expired() {
    std::time_t now = std::time(nullptr);
    for (auto it = users.begin(); it != users.end();) {
        if (!it->second.active && it->second.expires_at < now) {
            it = users.erase(it);
        } else {
            ++it;
        }
    }
}

std::string safe_filename(std::string name) {
    for (char &c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-') {
            c = '_';
        }
    }
    return name.empty() ? "user" : name;
}

void ensure_outbox() {
    if (mkdir("outbox", 0755) != 0 && errno != EEXIST) {
        throw std::runtime_error(std::string("mkdir outbox failed: ") + std::strerror(errno));
    }
}

void write_mail(const std::string &user, const UserRecord &record, int port) {
    ensure_outbox();
    std::string link = "http://127.0.0.1:" + std::to_string(port) + "/verify?user=" + user + "&code=" + record.code;
    std::string filename = "outbox/" + safe_filename(user) + "_" + record.code + ".eml";

    std::ofstream mail(filename);
    if (!mail) {
        throw std::runtime_error("cannot write " + filename);
    }

    mail << "From: noreply@example.local\n"
         << "To: " << record.email << "\n"
         << "Subject: Account activation\n"
         << "MIME-Version: 1.0\n"
         << "Content-Type: text/plain; charset=UTF-8\n\n"
         << "Hello " << user << ",\n\n"
         << "Open this link within one hour to activate your account:\n"
         << link << "\n";
}

std::string home_page(const std::string &message = "") {
    std::ostringstream body;
    if (!message.empty()) {
        body << "<p><strong>" << html_escape(message) << "</strong></p>";
    }
    body << "<form method=\"post\" action=\"/register\">"
         << "<label>昵称<br><input name=\"user\" required></label>"
         << "<label>口令<br><input name=\"password\" type=\"password\" required></label>"
         << "<label>邮箱<br><input name=\"email\" type=\"email\" required></label>"
         << "<button type=\"submit\">提交注册</button>"
         << "</form><p><a href=\"/users\">查看用户状态</a></p>";
    return page("邮件验证注册系统", body.str());
}

std::string register_user(const std::string &body, int port) {
    cleanup_expired();
    auto values = parse_kv(body);
    std::string user = values["user"];
    std::string password = values["password"];
    std::string email = values["email"];
    if (user.empty() || password.empty() || email.empty()) {
        return http_response(400, page("注册失败", "<p>昵称、口令和邮箱均不能为空。</p><p><a href=\"/\">返回</a></p>"));
    }

    auto existing = users.find(user);
    if (existing != users.end()) {
        std::string reason = existing->second.active ? "昵称已激活，不能重复注册。" : "昵称已被占用，请等待过期释放或使用激活链接。";
        return http_response(400, page("注册失败", "<p>" + html_escape(reason) + "</p><p><a href=\"/\">返回</a></p>"));
    }

    UserRecord record;
    record.email = email;
    record.password_hash = fnv1a_hex("password:" + password);
    record.code = fnv1a_hex("verify:" + user + ":" + email).substr(0, 12);
    record.expires_at = std::time(nullptr) + 60 * 60;
    users[user] = record;
    write_mail(user, users[user], port);

    std::ostringstream msg;
    msg << "<p>注册信息已提交，激活邮件已生成到 <code>outbox/</code> 目录。</p>"
        << "<p>请在一小时内打开邮件中的链接完成激活。</p>"
        << "<p><a href=\"/users\">查看用户状态</a></p>";
    return http_response(200, page("等待邮箱验证", msg.str()));
}

std::string verify_user(const std::string &query) {
    cleanup_expired();
    auto values = parse_kv(query);
    std::string user = values["user"];
    std::string code = values["code"];
    auto it = users.find(user);
    if (it == users.end()) {
        return http_response(400, page("验证失败", "<p>用户不存在，可能已经过期释放。</p><p><a href=\"/\">返回注册</a></p>"));
    }
    if (it->second.active) {
        return http_response(200, page("已经激活", "<p>该用户已经是激活会员。</p><p><a href=\"/users\">查看用户状态</a></p>"));
    }
    if (it->second.code != code) {
        return http_response(400, page("验证失败", "<p>验证码不正确。</p><p><a href=\"/\">返回注册</a></p>"));
    }
    if (it->second.expires_at < std::time(nullptr)) {
        users.erase(it);
        return http_response(400, page("验证失败", "<p>验证码超过一小时，昵称已释放。</p><p><a href=\"/\">重新注册</a></p>"));
    }

    it->second.active = true;
    return http_response(200, page("激活成功", "<p>会员已激活。</p><p><a href=\"/users\">查看用户状态</a></p>"));
}

std::string users_page() {
    cleanup_expired();
    std::ostringstream body;
    body << "<table border=\"1\" cellpadding=\"6\" cellspacing=\"0\"><tr><th>昵称</th><th>邮箱</th><th>状态</th><th>过期时间</th></tr>";
    for (const auto &entry : users) {
        const UserRecord &record = entry.second;
        body << "<tr><td>" << html_escape(entry.first) << "</td><td>" << html_escape(record.email)
             << "</td><td>" << (record.active ? "已激活" : "待验证") << "</td><td>";
        if (record.active) {
            body << "-";
        } else {
            body << record.expires_at;
        }
        body << "</td></tr>";
    }
    body << "</table><p><a href=\"/\">返回注册</a></p>";
    return http_response(200, page("用户状态", body.str()));
}

std::string handle_request(const std::string &request, int port) {
    std::size_t first_line_end = request.find("\r\n");
    if (first_line_end == std::string::npos) {
        return http_response(400, page("请求错误", "<p>HTTP 请求格式不正确。</p>"));
    }
    std::istringstream first_line(request.substr(0, first_line_end));
    std::string method;
    std::string target;
    std::string version;
    first_line >> method >> target >> version;

    std::size_t header_end = request.find("\r\n\r\n");
    std::string body = header_end == std::string::npos ? "" : request.substr(header_end + 4);
    std::string path = target;
    std::string query;
    std::size_t question = target.find('?');
    if (question != std::string::npos) {
        path = target.substr(0, question);
        query = target.substr(question + 1);
    }

    if (method == "GET" && path == "/") {
        return http_response(200, home_page());
    }
    if (method == "POST" && path == "/register") {
        return register_user(body, port);
    }
    if (method == "GET" && path == "/verify") {
        return verify_user(query);
    }
    if (method == "GET" && path == "/users") {
        return users_page();
    }
    return http_response(404, page("未找到", "<p>没有这个页面。</p><p><a href=\"/\">返回</a></p>"));
}

std::string read_request(int client) {
    std::string request;
    char buffer[4096];
    while (request.find("\r\n\r\n") == std::string::npos) {
        ssize_t n = recv(client, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            return request;
        }
        request.append(buffer, buffer + n);
        if (request.size() > 1024 * 1024) {
            throw std::runtime_error("request too large");
        }
    }

    std::size_t header_end = request.find("\r\n\r\n");
    std::size_t content_length = 0;
    std::istringstream headers(request.substr(0, header_end));
    std::string line;
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lower.rfind("content-length:", 0) == 0) {
            content_length = static_cast<std::size_t>(std::stoul(line.substr(15)));
        }
    }

    std::size_t total_needed = header_end + 4 + content_length;
    while (request.size() < total_needed) {
        ssize_t n = recv(client, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            break;
        }
        request.append(buffer, buffer + n);
    }
    return request;
}

void send_all(int client, const std::string &response) {
    const char *data = response.data();
    std::size_t remaining = response.size();
    while (remaining > 0) {
        ssize_t sent = send(client, data, remaining, 0);
        if (sent <= 0) {
            return;
        }
        data += sent;
        remaining -= static_cast<std::size_t>(sent);
    }
}

int create_server(int port) {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        throw std::runtime_error("socket failed");
    }
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (bind(server, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        close(server);
        throw std::runtime_error(std::string("bind failed: ") + std::strerror(errno));
    }
    if (listen(server, 16) != 0) {
        close(server);
        throw std::runtime_error("listen failed");
    }
    return server;
}

}  // namespace

int main(int argc, char **argv) {
    int port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    try {
        int server = create_server(port);
        std::cout << "Listening on http://127.0.0.1:" << port << "/\n";
        while (true) {
            int client = accept(server, nullptr, nullptr);
            if (client < 0) {
                continue;
            }
            try {
                std::string request = read_request(client);
                std::string response = handle_request(request, port);
                send_all(client, response);
            } catch (const std::exception &e) {
                send_all(client, http_response(400, page("请求错误", std::string("<p>") + html_escape(e.what()) + "</p>")));
            }
            close(client);
        }
        close(server);
    } catch (const std::exception &e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
