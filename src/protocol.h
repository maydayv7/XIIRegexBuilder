#ifndef XIIR_PROTOCOL_H
#define XIIR_PROTOCOL_H

#include <cstdint>

namespace xiir {
namespace protocol {

    // Network Config
    const char* const FPGA_IP = "192.168.2.10";
    const char* const HOST_IP = "192.168.2.100";
    const uint8_t FPGA_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    
    const uint16_t RX_PORT = 7777; // FPGA listens here
    const uint16_t TX_PORT = 7778; // Host listens here

    // Batching Config
    const uint32_t MAX_PAYLOAD_BYTES = 1400;
    const uint32_t COALESCE_TIMEOUT_US = 200;

    // Packet Header Sizes
    const uint32_t REQ_HEADER_SIZE = 6; // SEQ(4) + NUM_STR(2)
    const uint32_t RES_HEADER_SIZE = 10; // SEQ(4) + NUM_RES(2) + STAMP(4)

    /**
     * @brief Calculates the size of a single match result in bytes.
     * @param num_regex Number of active regexes
     * @return Result size in bytes
     */
    inline uint32_t get_result_size(int num_regex) {
        uint32_t match_bits_bytes = (num_regex + 7) / 8;
        uint32_t hit_counts_bytes = num_regex * 2;
        return match_bits_bytes + hit_counts_bytes;
    }

} // namespace protocol
} // namespace xiir

#endif // XIIR_PROTOCOL_H
