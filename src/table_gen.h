#ifndef TABLE_GEN_H
#define TABLE_GEN_H

#include <vector>
#include <string>
#include <memory>
#include "nfa.h"

class TableGen {
public:
    TableGen() = delete; // Static class

    /**
     * @brief Generates a tightly packed binary file for dynamic NFA programming.
     * @param nfas        The vector of NFAs.
     * @param outputPath  The path to the output directory.
     */
    static void generate(const std::vector<std::unique_ptr<NFA>>& nfas, const std::string& outputPath);
};

#endif // TABLE_GEN_H
