#include "table_gen.h"
#include <fstream>
#include <iostream>
#include <cstdint>
#include <map>
#include <set>
#include <filesystem>
#include <algorithm>

void TableGen::generate(const std::vector<std::unique_ptr<NFA>>& nfas, const std::string& outputPathStr) {
    std::filesystem::path outputDir(outputPathStr);
    if (!std::filesystem::exists(outputDir)) {
        std::filesystem::create_directories(outputDir);
    }
    std::filesystem::path binFilePath = outputDir / "regexes.bin";
    std::ofstream binFile(binFilePath, std::ios::binary);

    if (!binFile.is_open()) {
        std::cerr << "Failed to open binary file for writing: " << binFilePath << std::endl;
        return;
    }

    const int NUM_SLOTS = 16;
    const int MAX_STATES = 16;

    for (int s = 0; s < NUM_SLOTS; ++s) {
        uint16_t accept_mask = 0x0000;
        uint16_t transitions[MAX_STATES][256] = {0};

        if (s < static_cast<int>(nfas.size())) {
            const auto& nfa = *nfas[s];
            std::map<int, int> globalToLocal;
            int localIdx = 0;

            // Start state always local index 0
            globalToLocal[nfa.startStateId] = localIdx++;

            // Map other states
            std::set<int> otherIds;
            for (const auto& pair : nfa.states) {
                if (pair.first != nfa.startStateId)
                    otherIds.insert(pair.first);
            }
            for (int id : otherIds) {
                if (localIdx < MAX_STATES) {
                    globalToLocal[id] = localIdx++;
                }
            }

            // Fill accept mask
            for (const auto& [globalId, localId] : globalToLocal) {
                if (nfa.states.at(globalId).isAccept) {
                    accept_mask |= (1 << localId);
                }
            }

            // Fill transitions
            for (const auto& [srcGlobalId, srcState] : nfa.states) {
                if (globalToLocal.find(srcGlobalId) == globalToLocal.end()) continue;
                int srcLocal = globalToLocal[srcGlobalId];

                for (const auto& [c, dstGlobalIds] : srcState.transitions) {
                    for (int dstGlobalId : dstGlobalIds) {
                        if (globalToLocal.find(dstGlobalId) != globalToLocal.end()) {
                            int dstLocal = globalToLocal[dstGlobalId];
                            transitions[srcLocal][c] |= (1 << dstLocal);
                        }
                    }
                }
            }
        }

        // Write accept_mask (Little-Endian)
        uint8_t am_bytes[2];
        am_bytes[0] = accept_mask & 0xFF;
        am_bytes[1] = (accept_mask >> 8) & 0xFF;
        binFile.write(reinterpret_cast<const char*>(am_bytes), 2);

        // Write transitions (16 states * 256 chars * 2 bytes LE)
        for (int state = 0; state < MAX_STATES; ++state) {
            for (int ch = 0; ch < 256; ++ch) {
                uint16_t mask = transitions[state][ch];
                uint8_t m_bytes[2];
                m_bytes[0] = mask & 0xFF;
                m_bytes[1] = (mask >> 8) & 0xFF;
                binFile.write(reinterpret_cast<const char*>(m_bytes), 2);
            }
        }
    }

    binFile.close();
    std::cout << "Successfully generated binary table: " << binFilePath << " (Size: " << std::filesystem::file_size(binFilePath) << " bytes)" << std::endl;
}
