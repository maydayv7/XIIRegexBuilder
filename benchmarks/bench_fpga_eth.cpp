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

    char buffer[65535];
    uint32_t seq = 0;
    uint32_t total_strings_processed = 0;

    std::cout << "Starting C++ Ethernet Benchmark (" << strings.size() << " strings)..." << std::endl;
    auto start = std::chrono::high_resolution_clock::now();

    size_t batch_size = 16;
    for (size_t i = 0; i < strings.size(); i += batch_size) {
        size_t chunk_end = std::min(i + batch_size, strings.size());
        uint16_t chunk_len = (uint16_t)(chunk_end - i);

        // Build Request Packet
        // Layout: SEQ(4) | NUM_STR(2) | [LEN(1) | DATA(N)] x NUM_STR
        uint8_t pkt[1500];
        uint32_t seq_be = htonl(seq);
        uint16_t num_be = htons(chunk_len);
        
        memcpy(pkt + 0, &seq_be, 4);
        memcpy(pkt + 4, &num_be, 2);
        
        size_t offset = xiir::protocol::REQ_HEADER_SIZE;
        for (size_t j = i; j < chunk_end; ++j) {
            pkt[offset++] = (uint8_t)strings[j].length();
            memcpy(pkt + offset, strings[j].c_str(), strings[j].length());
            offset += strings[j].length();
        }

        sendto(sock, pkt, offset, 0, (struct sockaddr*)&fpga_addr, sizeof(fpga_addr));

        // Receive Response Packet
        // Layout: SEQ(4) | NUM_RES(2) | STAMP(4) | [MATCH(bits) | HITS] x NUM_RES
        int n = recv(sock, buffer, sizeof(buffer), 0);
        if (n < 0) {
            std::cerr << "Timeout on seq " << seq << std::endl;
            break;
        }

        if (n < (int)xiir::protocol::RES_HEADER_SIZE) {
            std::cerr << "Malformed response (too short): " << n << " bytes" << std::endl;
            break;
        }
        
        // Parse response header
        uint32_t res_seq;
        uint16_t res_num;
        uint32_t res_stamp;
        
        memcpy(&res_seq,   buffer + 0, 4);
        memcpy(&res_num,   buffer + 4, 2);
        memcpy(&res_stamp, buffer + 6, 4);
        
        res_seq   = ntohl(res_seq);
        res_num   = ntohs(res_num);
        res_stamp = ntohl(res_stamp);

        if (res_seq != seq) {
            std::cerr << "Sequence mismatch! Expected " << seq << ", got " << res_seq << std::endl;
        }

        total_strings_processed += res_num;
        seq++;
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;

    std::cout << "\n=== C++ Ethernet Benchmark Summary ===" << std::endl;
    std::cout << "Total Time: " << diff.count() << " seconds" << std::endl;
    std::cout << "Throughput: " << total_strings_processed / diff.count() << " strings/sec" << std::endl;
    std::cout << "Success: " << total_strings_processed << "/" << strings.size() << std::endl;

    close(sock);
    return 0;
}
