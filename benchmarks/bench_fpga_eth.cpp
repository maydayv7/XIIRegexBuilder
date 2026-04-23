#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <cstring>
#include <chrono>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include "protocol.h"

int main(int argc, char* argv[]) {
    std::string inputFile = (argc > 1) ? argv[1] : "inputs/large_test_strings.txt";
    
    std::ifstream in(inputFile);
    std::vector<std::string> strings;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        strings.push_back(line);
    }
    in.close();

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in fpga_addr;
    memset(&fpga_addr, 0, sizeof(fpga_addr));
    fpga_addr.sin_family = AF_INET;
    fpga_addr.sin_port = htons(xiir::protocol::RX_PORT);
    inet_pton(AF_INET, xiir::protocol::FPGA_IP, &fpga_addr.sin_addr);

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buffer[2048];
    uint32_t seq = 0;

    std::cout << "Starting C++ Ethernet Benchmark (" << strings.size() << " strings)..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    for (const auto& s : strings) {
        // Build Packet
        uint8_t pkt[1500];
        uint32_t seq_be = htonl(seq);
        uint16_t num_be = htons(1);
        memcpy(pkt, &seq_be, 4);
        memcpy(pkt + 4, &num_be, 2);
        pkt[6] = (uint8_t)s.length();
        memcpy(pkt + 7, s.c_str(), s.length());

        sendto(sock, pkt, 7 + s.length(), 0, (struct sockaddr*)&fpga_addr, sizeof(fpga_addr));

        int n = recv(sock, buffer, sizeof(buffer), 0);
        if (n < 0) {
            std::cerr << "Timeout on seq " << seq << std::endl;
            break;
        }
        seq++;
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;

    std::cout << "\n=== C++ Ethernet Benchmark Summary ===" << std::endl;
    std::cout << "Total Time: " << diff.count() << " seconds" << std::endl;
    std::cout << "Throughput: " << seq / diff.count() << " strings/sec" << std::endl;

    close(sock);
    return 0;
}
