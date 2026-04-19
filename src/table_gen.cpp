#include "table_gen.h"
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <algorithm>
#include <filesystem>

void TableGen::generate(const std::vector<std::unique_ptr<NFA>> &nfas,
                        const std::string &outputDir)
{
    if (nfas.empty())
    {
        std::cerr << "No NFAs to generate table for." << std::endl;
        return;
    }

    const NFA &nfa = *nfas[0];
    std::filesystem::path dir(outputDir);
    std::filesystem::create_directories(dir);
    auto filePath = dir / "regex.bin";
    
    std::ofstream out(filePath, std::ios::binary);
    if (!out.is_open())
    {
        std::cerr << "Could not open " << filePath << " for writing." << std::endl;
        return;
    }

    // Map global state IDs to 0-15
    std::map<int, int> globalToLocal;
    int nextLocal = 0;

    // Start state must be local 0
    globalToLocal[nfa.startStateId] = nextLocal++;

    for (auto const& [id, state] : nfa.states) {
        if (id != nfa.startStateId) {
            if (nextLocal < 16) {
                globalToLocal[id] = nextLocal++;
            }
        }
    }

    // 1. Bytes 0-1: accept_mask (16-bit, Little-Endian)
    uint16_t accept_mask = 0;
    for (auto const& [id, state] : nfa.states) {
        if (state.isAccept && globalToLocal.count(id)) {
            accept_mask |= (1 << globalToLocal[id]);
        }
    }
    out.put(static_cast<char>(accept_mask & 0xFF));
    out.put(static_cast<char>((accept_mask >> 8) & 0xFF));

    // 2. Bytes 2-4097: transitions (16 states * 128 chars * 2 bytes/mask)
    for (int s = 0; s < 16; ++s) {
        // Find the global ID for this local state
        int globalId = -1;
        for (auto const& [g, l] : globalToLocal) {
            if (l == s) {
                globalId = g;
                break;
            }
        }

        for (int c = 0; c < 128; ++c) {
            uint16_t next_state_mask = 0;
            if (globalId != -1) {
                const auto& state = nfa.states.at(globalId);
                auto it = state.transitions.find(static_cast<unsigned char>(c));
                if (it != state.transitions.end()) {
                    for (int dstGlobalId : it->second) {
                        if (globalToLocal.count(dstGlobalId)) {
                            next_state_mask |= (1 << globalToLocal[dstGlobalId]);
                        }
                    }
                }
            }
            // Write Little-Endian
            out.put(static_cast<char>(next_state_mask & 0xFF));
            out.put(static_cast<char>((next_state_mask >> 8) & 0xFF));
        }
    }

    std::cout << "Successfully generated " << filePath << " (size: " << out.tellp() << " bytes)" << std::endl;
    out.close();
}
