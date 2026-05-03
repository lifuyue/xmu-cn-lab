#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>

namespace fs = std::filesystem;

struct CommandResult {
    int exit_code = 0;
    std::string output;
};

const std::string kImage = "lab6-frr-native:latest";
const std::vector<std::string> kStaticContainers = {
    "lab6-static-r1", "lab6-static-r2", "lab6-static-r3",
    "lab6-static-pc1", "lab6-static-pc2"};
const std::vector<std::string> kRipContainers = {
    "lab6-rip-r1", "lab6-rip-r2", "lab6-rip-r3", "lab6-rip-pc1", "lab6-rip-pc2"};
const std::vector<std::string> kVlanContainers = {"lab6-vlan-lab"};
const std::vector<std::string> kStaticNetworks = {
    "lab6_static_lan10", "lab6_static_link12", "lab6_static_link23", "lab6_static_lan30"};
const std::vector<std::string> kRipNetworks = {
    "lab6_rip_lan10", "lab6_rip_link12", "lab6_rip_link23", "lab6_rip_lan30"};

fs::path executable_dir(const char *argv0) {
    fs::path path = fs::absolute(argv0);
    if (!path.has_parent_path()) {
        return fs::current_path();
    }
    return path.parent_path();
}

std::string shell_quote(const std::string &value) {
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
}

CommandResult run_capture(const std::string &command) {
    std::array<char, 4096> buffer{};
    std::string full_command = command + " 2>&1";
    FILE *pipe = popen(full_command.c_str(), "r");
    if (pipe == nullptr) {
        throw std::runtime_error("failed to execute command: " + command);
    }

    std::string output;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

    const int status = pclose(pipe);
    int exit_code = status;
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    }
    return {exit_code, output};
}

void require_success(const std::string &command) {
    const CommandResult result = run_capture(command);
    if (result.exit_code != 0) {
        throw std::runtime_error("command failed: " + command + "\n" + result.output);
    }
}

void ensure_image() {
    const std::string command =
        "docker image inspect " + kImage + " >/dev/null 2>&1 || "
        "docker build -t " + kImage + " -<<'DOCKER'\n"
        "FROM alpine:3.20\n"
        "RUN apk add --no-cache frr iproute2 iputils bash\n"
        "CMD [\"sleep\", \"infinity\"]\n"
        "DOCKER";
    require_success(command);
}

void append_log(const fs::path &path, const std::string &command, const CommandResult &result) {
    std::ofstream log(path, std::ios::app);
    log << "$ " << command << "\n";
    log << result.output;
    if (!result.output.empty() && result.output.back() != '\n') {
        log << "\n";
    }
    log << "\n";
}

CommandResult run_logged(const fs::path &path, const std::string &command) {
    const CommandResult result = run_capture(command);
    append_log(path, command, result);
    if (result.exit_code != 0) {
        throw std::runtime_error("command failed: " + command + "\n" + result.output);
    }
    return result;
}

void reset_file(const fs::path &path) {
    std::ofstream file(path, std::ios::trunc);
}

std::string join_commands(const std::vector<std::string> &commands) {
    std::ostringstream oss;
    for (const auto &command : commands) {
        oss << " -c " << shell_quote(command);
    }
    return oss.str();
}

void docker_exec_vtysh(const std::string &container, const std::vector<std::string> &commands) {
    const std::string command = "docker exec " + container + " vtysh" + join_commands(commands);
    require_success(command);
}

void wait_for_frr(const std::string &container) {
    for (int attempt = 0; attempt < 90; ++attempt) {
        const std::string command =
            "docker exec " + container +
            " sh -lc 'test -S /var/run/frr/zebra.vty && vtysh -c \"show interface brief\"'";
        const CommandResult result = run_capture(command);
        if (result.exit_code == 0 && result.output.find("Interface") != std::string::npos) {
            run_capture("docker exec " + container +
                        " sh -lc 'touch /etc/frr/vtysh.conf && chown frr:frrvty /etc/frr/vtysh.conf'");
            return;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    throw std::runtime_error("FRR did not become ready in " + container);
}

void start_frr_daemons(const std::string &router) {
    const std::string script =
        "mkdir -p /var/run/frr && "
        "touch /etc/frr/vtysh.conf /etc/frr/mgmtd.conf /etc/frr/zebra.conf /etc/frr/staticd.conf && "
        "chown frr:frr /etc/frr/*.conf /var/run/frr && "
        "start_one() { socket=$1; pid=$2; daemon=$3; log=$4; "
        "for i in 1 2 3; do "
        "test -S $socket && return 0; "
        "rm -f $pid $socket; "
        "$daemon -d -F traditional -A 127.0.0.1 >$log 2>&1 || true; "
        "sleep 1; "
        "done; "
        "test -S $socket; "
        "}; "
        "start_one /var/run/frr/mgmtd.vty /var/run/frr/mgmtd.pid /usr/lib/frr/mgmtd /tmp/mgmtd.log && "
        "start_one /var/run/frr/zebra.vty /var/run/frr/zebra.pid /usr/lib/frr/zebra /tmp/zebra.log && "
        "start_one /var/run/frr/staticd.vty /var/run/frr/staticd.pid /usr/lib/frr/staticd /tmp/staticd.log";
    require_success("docker exec " + router + " sh -lc " + shell_quote(script));
}

void cleanup() {
    std::vector<std::string> containers;
    containers.insert(containers.end(), kStaticContainers.begin(), kStaticContainers.end());
    containers.insert(containers.end(), kRipContainers.begin(), kRipContainers.end());
    containers.insert(containers.end(), kVlanContainers.begin(), kVlanContainers.end());

    std::vector<std::string> networks;
    networks.insert(networks.end(), kStaticNetworks.begin(), kStaticNetworks.end());
    networks.insert(networks.end(), kRipNetworks.begin(), kRipNetworks.end());

    for (const auto &container : containers) {
        run_capture("docker rm -f " + container);
    }
    for (const auto &network : networks) {
        run_capture("docker network rm " + network);
    }
}

void start_router(const std::string &name, const std::string &network, const std::string &ip) {
    const std::string command =
        "docker run -d --name " + name +
        " --privileged --sysctl net.ipv4.ip_forward=1 --network " + network +
        " --ip " + ip + " " + kImage + " sleep infinity";
    require_success(command);
}

void start_host(const std::string &name, const std::string &network, const std::string &ip) {
    const std::string command =
        "docker run -d --name " + name +
        " --privileged --network " + network + " --ip " + ip + " " + kImage +
        " sleep infinity";
    require_success(command);
}

void configure_host_route(const std::string &host, const std::string &gateway) {
    require_success("docker exec " + host + " sh -lc " +
                    shell_quote("ip route replace default via " + gateway + " dev eth0"));
}

void save_config(const std::string &router, const fs::path &outfile) {
    const CommandResult result = run_capture("docker exec " + router + " vtysh -c 'show running-config'");
    if (result.exit_code != 0) {
        throw std::runtime_error("failed to save config for " + router + "\n" + result.output);
    }
    std::ofstream file(outfile);
    file << result.output;
}

void write_text_capture(const fs::path &path, const std::vector<std::pair<std::string, std::string>> &commands) {
    std::ofstream file(path, std::ios::trunc);
    for (const auto &[title, command] : commands) {
        file << "$ " << title << "\n";
        const CommandResult result = run_capture(command);
        file << result.output;
        if (!result.output.empty() && result.output.back() != '\n') {
            file << "\n";
        }
        file << "\n";
        if (result.exit_code != 0) {
            throw std::runtime_error("capture command failed: " + command + "\n" + result.output);
        }
    }
}

void record_environment(const fs::path &artifact_dir) {
    const fs::path log = artifact_dir / "00_environment.log";
    reset_file(log);
    run_logged(log, "docker version --format 'Client={{.Client.Version}} Server={{.Server.Version}}'");
    run_logged(log, "docker image inspect " + kImage + " --format '{{.RepoDigests}}'");
    run_logged(log, "docker info --format 'OS={{.OperatingSystem}} Arch={{.Architecture}}'");
}

void run_static_lab(const fs::path &artifact_dir, const fs::path &config_dir, const fs::path &text_dir) {
    const fs::path log = artifact_dir / "01_static_routes.log";
    reset_file(log);

    run_logged(log, "docker network create --driver bridge --subnet 192.168.10.0/24 lab6_static_lan10");
    run_logged(log, "docker network create --driver bridge --subnet 10.0.12.0/24 lab6_static_link12");
    run_logged(log, "docker network create --driver bridge --subnet 10.0.23.0/24 lab6_static_link23");
    run_logged(log, "docker network create --driver bridge --subnet 192.168.30.0/24 lab6_static_lan30");

    start_router("lab6-static-r1", "lab6_static_lan10", "192.168.10.254");
    start_router("lab6-static-r2", "lab6_static_link12", "10.0.12.3");
    start_router("lab6-static-r3", "lab6_static_link23", "10.0.23.3");
    start_host("lab6-static-pc1", "lab6_static_lan10", "192.168.10.10");
    start_host("lab6-static-pc2", "lab6_static_lan30", "192.168.30.10");

    require_success("docker network connect --ip 10.0.12.2 lab6_static_link12 lab6-static-r1");
    require_success("docker network connect --ip 10.0.23.2 lab6_static_link23 lab6-static-r2");
    require_success("docker network connect --ip 192.168.30.254 lab6_static_lan30 lab6-static-r3");

    start_frr_daemons("lab6-static-r1");
    start_frr_daemons("lab6-static-r2");
    start_frr_daemons("lab6-static-r3");
    wait_for_frr("lab6-static-r1");
    wait_for_frr("lab6-static-r2");
    wait_for_frr("lab6-static-r3");
    configure_host_route("lab6-static-pc1", "192.168.10.254");
    configure_host_route("lab6-static-pc2", "192.168.30.254");

    docker_exec_vtysh("lab6-static-r1", {
        "configure terminal", "hostname R1", "interface eth0",
        "description LAN_PC1_192.168.10.0/24", "ip address 192.168.10.254/24",
        "no shutdown", "interface eth1", "description LINK_R1_R2_10.0.12.0/24",
        "ip address 10.0.12.2/24", "no shutdown", "ip route 192.168.30.0/24 10.0.12.3",
        "end", "write memory"});

    docker_exec_vtysh("lab6-static-r2", {
        "configure terminal", "hostname R2", "interface eth0",
        "description LINK_R1_R2_10.0.12.0/24", "ip address 10.0.12.3/24",
        "no shutdown", "interface eth1", "description LINK_R2_R3_10.0.23.0/24",
        "ip address 10.0.23.2/24", "no shutdown", "ip route 192.168.10.0/24 10.0.12.2",
        "ip route 192.168.30.0/24 10.0.23.3", "end", "write memory"});

    docker_exec_vtysh("lab6-static-r3", {
        "configure terminal", "hostname R3", "interface eth0",
        "description LINK_R2_R3_10.0.23.0/24", "ip address 10.0.23.3/24",
        "no shutdown", "interface eth1", "description LAN_PC2_192.168.30.0/24",
        "ip address 192.168.30.254/24", "no shutdown", "ip route 192.168.10.0/24 10.0.23.2",
        "end", "write memory"});

    std::this_thread::sleep_for(std::chrono::seconds(2));
    save_config("lab6-static-r1", config_dir / "static_R1.conf");
    save_config("lab6-static-r2", config_dir / "static_R2.conf");
    save_config("lab6-static-r3", config_dir / "static_R3.conf");

    run_logged(log, "docker exec lab6-static-r1 vtysh -c 'show interface brief' -c 'show ip route'");
    run_logged(log, "docker exec lab6-static-r2 vtysh -c 'show interface brief' -c 'show ip route'");
    run_logged(log, "docker exec lab6-static-r3 vtysh -c 'show interface brief' -c 'show ip route'");
    run_logged(log, "docker exec lab6-static-pc1 ping -c 4 -W 2 192.168.30.10");

    write_text_capture(text_dir / "static_routes.txt", {
        {"docker exec lab6-static-r1 vtysh -c 'show ip route'",
         "docker exec lab6-static-r1 vtysh -c 'show ip route'"},
        {"docker exec lab6-static-r2 vtysh -c 'show ip route'",
         "docker exec lab6-static-r2 vtysh -c 'show ip route'"},
        {"docker exec lab6-static-pc1 ping -c 4 -W 2 192.168.30.10",
         "docker exec lab6-static-pc1 ping -c 4 -W 2 192.168.30.10"}});

    write_text_capture(text_dir / "basic_ios.txt", {
        {"docker exec lab6-static-r1 vtysh -c 'show running-config'",
         "docker exec lab6-static-r1 vtysh -c 'show running-config'"},
        {"docker exec lab6-static-r1 vtysh -c 'show interface brief'",
         "docker exec lab6-static-r1 vtysh -c 'show interface brief'"}});
}

void enable_ripd(const std::string &router) {
    const std::string script =
        "touch /etc/frr/ripd.conf && chown frr:frr /etc/frr/ripd.conf && "
        "for i in 1 2 3; do "
        "test -S /var/run/frr/ripd.vty && exit 0; "
        "rm -f /var/run/frr/ripd.pid /var/run/frr/ripd.vty; "
        "/usr/lib/frr/ripd -d -F traditional -A 127.0.0.1 >/tmp/ripd.log 2>&1 || true; "
        "sleep 1; "
        "done; "
        "test -S /var/run/frr/ripd.vty";
    require_success("docker exec " + router + " sh -lc " + shell_quote(script));
    wait_for_frr(router);
    for (int attempt = 0; attempt < 30; ++attempt) {
        const CommandResult result =
            run_capture("docker exec " + router + " sh -lc 'test -S /var/run/frr/ripd.vty'");
        if (result.exit_code == 0) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    throw std::runtime_error("ripd did not become ready in " + router);
}

void wait_for_rip_route(const std::string &router, const std::string &prefix) {
    for (int attempt = 0; attempt < 90; ++attempt) {
        const CommandResult result = run_capture("docker exec " + router + " vtysh -c 'show ip route'");
        if (result.exit_code == 0 && result.output.find(prefix) != std::string::npos &&
            result.output.find("R>*") != std::string::npos) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    throw std::runtime_error("RIP route " + prefix + " was not learned by " + router);
}

void run_rip_lab(const fs::path &artifact_dir, const fs::path &config_dir, const fs::path &text_dir) {
    const fs::path log = artifact_dir / "02_rip_dynamic_routes.log";
    reset_file(log);

    run_logged(log, "docker network create --driver bridge --subnet 192.168.110.0/24 lab6_rip_lan10");
    run_logged(log, "docker network create --driver bridge --subnet 10.1.12.0/24 lab6_rip_link12");
    run_logged(log, "docker network create --driver bridge --subnet 10.1.23.0/24 lab6_rip_link23");
    run_logged(log, "docker network create --driver bridge --subnet 192.168.130.0/24 lab6_rip_lan30");

    start_router("lab6-rip-r1", "lab6_rip_lan10", "192.168.110.254");
    start_router("lab6-rip-r2", "lab6_rip_link12", "10.1.12.3");
    start_router("lab6-rip-r3", "lab6_rip_link23", "10.1.23.3");
    start_host("lab6-rip-pc1", "lab6_rip_lan10", "192.168.110.10");
    start_host("lab6-rip-pc2", "lab6_rip_lan30", "192.168.130.10");

    require_success("docker network connect --ip 10.1.12.2 lab6_rip_link12 lab6-rip-r1");
    require_success("docker network connect --ip 10.1.23.2 lab6_rip_link23 lab6-rip-r2");
    require_success("docker network connect --ip 192.168.130.254 lab6_rip_lan30 lab6-rip-r3");

    start_frr_daemons("lab6-rip-r1");
    start_frr_daemons("lab6-rip-r2");
    start_frr_daemons("lab6-rip-r3");
    wait_for_frr("lab6-rip-r1");
    wait_for_frr("lab6-rip-r2");
    wait_for_frr("lab6-rip-r3");
    enable_ripd("lab6-rip-r1");
    enable_ripd("lab6-rip-r2");
    enable_ripd("lab6-rip-r3");

    configure_host_route("lab6-rip-pc1", "192.168.110.254");
    configure_host_route("lab6-rip-pc2", "192.168.130.254");

    docker_exec_vtysh("lab6-rip-r1", {
        "configure terminal", "hostname R1-RIP", "interface eth0",
        "description LAN_PC1_192.168.110.0/24", "ip address 192.168.110.254/24",
        "no shutdown", "interface eth1", "description LINK_R1_R2_10.1.12.0/24",
        "ip address 10.1.12.2/24", "no shutdown", "router rip", "version 2",
        "network 192.168.110.0/24", "network 10.1.12.0/24", "end", "write memory"});

    docker_exec_vtysh("lab6-rip-r2", {
        "configure terminal", "hostname R2-RIP", "interface eth0",
        "description LINK_R1_R2_10.1.12.0/24", "ip address 10.1.12.3/24",
        "no shutdown", "interface eth1", "description LINK_R2_R3_10.1.23.0/24",
        "ip address 10.1.23.2/24", "no shutdown", "router rip", "version 2",
        "network 10.1.12.0/24", "network 10.1.23.0/24", "end", "write memory"});

    docker_exec_vtysh("lab6-rip-r3", {
        "configure terminal", "hostname R3-RIP", "interface eth0",
        "description LINK_R2_R3_10.1.23.0/24", "ip address 10.1.23.3/24",
        "no shutdown", "interface eth1", "description LAN_PC2_192.168.130.0/24",
        "ip address 192.168.130.254/24", "no shutdown", "router rip", "version 2",
        "network 10.1.23.0/24", "network 192.168.130.0/24", "end", "write memory"});

    wait_for_rip_route("lab6-rip-r1", "192.168.130.0/24");
    wait_for_rip_route("lab6-rip-r3", "192.168.110.0/24");

    save_config("lab6-rip-r1", config_dir / "rip_R1.conf");
    save_config("lab6-rip-r2", config_dir / "rip_R2.conf");
    save_config("lab6-rip-r3", config_dir / "rip_R3.conf");

    run_logged(log, "docker exec lab6-rip-r1 vtysh -c 'show ip rip' -c 'show ip route'");
    run_logged(log, "docker exec lab6-rip-r2 vtysh -c 'show ip rip' -c 'show ip route'");
    run_logged(log, "docker exec lab6-rip-r3 vtysh -c 'show ip rip' -c 'show ip route'");
    run_logged(log, "docker exec lab6-rip-pc1 ping -c 4 -W 2 192.168.130.10");

    write_text_capture(text_dir / "rip_routes.txt", {
        {"docker exec lab6-rip-r1 vtysh -c 'show ip rip' -c 'show ip route'",
         "docker exec lab6-rip-r1 vtysh -c 'show ip rip' -c 'show ip route'"},
        {"docker exec lab6-rip-r3 vtysh -c 'show ip route'",
         "docker exec lab6-rip-r3 vtysh -c 'show ip route'"},
        {"docker exec lab6-rip-pc1 ping -c 4 -W 2 192.168.130.10",
         "docker exec lab6-rip-pc1 ping -c 4 -W 2 192.168.130.10"}});
}

void run_vlan_lab(const fs::path &artifact_dir, const fs::path &text_dir) {
    const fs::path log = artifact_dir / "03_vlan_bridge.log";
    reset_file(log);

    run_logged(log, "docker run -d --name lab6-vlan-lab --privileged " + kImage +
                        " sleep infinity");

    const std::string setup_script = R"(
set -e
ip netns add vlan10-host
ip netns add vlan20-host
ip netns add router

ip link add br0 type bridge vlan_filtering 1
ip link set br0 up

ip link add swp1 type veth peer name pc10-eth0
ip link add swp2 type veth peer name pc20-eth0
ip link add swp3 type veth peer name rtr-eth0

ip link set pc10-eth0 netns vlan10-host
ip link set pc20-eth0 netns vlan20-host
ip link set rtr-eth0 netns router

ip link set swp1 master br0
ip link set swp2 master br0
ip link set swp3 master br0
ip link set swp1 up
ip link set swp2 up
ip link set swp3 up

bridge vlan del dev swp1 vid 1
bridge vlan del dev swp2 vid 1
bridge vlan del dev swp3 vid 1
bridge vlan add dev swp1 vid 10 pvid untagged
bridge vlan add dev swp2 vid 20 pvid untagged
bridge vlan add dev swp3 vid 10
bridge vlan add dev swp3 vid 20

ip netns exec vlan10-host ip link set lo up
ip netns exec vlan10-host ip link set pc10-eth0 up
ip netns exec vlan10-host ip addr add 172.16.10.10/24 dev pc10-eth0
ip netns exec vlan10-host ip route add default via 172.16.10.1

ip netns exec vlan20-host ip link set lo up
ip netns exec vlan20-host ip link set pc20-eth0 up
ip netns exec vlan20-host ip addr add 172.16.20.10/24 dev pc20-eth0
ip netns exec vlan20-host ip route add default via 172.16.20.1

ip netns exec router ip link set lo up
ip netns exec router ip link set rtr-eth0 up
ip netns exec router ip link add link rtr-eth0 name rtr-eth0.10 type vlan id 10
ip netns exec router ip link add link rtr-eth0 name rtr-eth0.20 type vlan id 20
ip netns exec router ip addr add 172.16.10.1/24 dev rtr-eth0.10
ip netns exec router ip addr add 172.16.20.1/24 dev rtr-eth0.20
ip netns exec router ip link set rtr-eth0.10 up
ip netns exec router ip link set rtr-eth0.20 up
ip netns exec router sysctl -w net.ipv4.ip_forward=1 >/dev/null
)";

    require_success("docker exec lab6-vlan-lab sh -lc " + shell_quote(setup_script));

    const std::vector<std::pair<std::string, std::string>> commands = {
        {"docker exec lab6-vlan-lab bridge vlan show",
         "docker exec lab6-vlan-lab bridge vlan show"},
        {"docker exec lab6-vlan-lab ip netns exec router ip -d link show rtr-eth0.10",
         "docker exec lab6-vlan-lab ip netns exec router ip -d link show rtr-eth0.10"},
        {"docker exec lab6-vlan-lab ip netns exec router ip route",
         "docker exec lab6-vlan-lab ip netns exec router ip route"},
        {"docker exec lab6-vlan-lab ip netns exec vlan10-host ping -c 4 -W 2 172.16.20.10",
         "docker exec lab6-vlan-lab ip netns exec vlan10-host ping -c 4 -W 2 172.16.20.10"}};

    for (const auto &[title, command] : commands) {
        run_logged(log, command);
    }
    write_text_capture(text_dir / "vlan.txt", commands);
}

int run_all(const char *argv0) {
    const fs::path root_dir = executable_dir(argv0);
    const fs::path artifact_dir = root_dir / "artifacts";
    const fs::path config_dir = artifact_dir / "configs";
    const fs::path text_dir = artifact_dir / "screenshot_text";
    fs::create_directories(config_dir);
    fs::create_directories(text_dir);

    ensure_image();
    cleanup();
    record_environment(artifact_dir);
    run_static_lab(artifact_dir, config_dir, text_dir);
    run_rip_lab(artifact_dir, config_dir, text_dir);
    run_vlan_lab(artifact_dir, text_dir);

    std::cout << "Artifacts written to " << artifact_dir << "\n";
    return 0;
}

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--cleanup") {
            cleanup();
            std::cout << "Lab containers and Docker networks cleaned.\n";
            return 0;
        }
        if (argc != 1) {
            std::cerr << "Usage: " << argv[0] << " [--cleanup]\n";
            return 2;
        }
        return run_all(argv[0]);
    } catch (const std::exception &ex) {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }
}
