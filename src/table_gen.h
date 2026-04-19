#ifndef TABLE_GEN_H
#define TABLE_GEN_H

#include <vector>
#include <string>
#include <memory>
#include "nfa.h"

class TableGen
{
public:
    TableGen() = delete;

    /**
     * @brief Generates a tightly packed binary table for a single NFA.
     * @param nfas           Vector containing at least one NFA.
     * @param outputPath     Path where regex.bin will be written.
     */
    static void generate(const std::vector<std::unique_ptr<NFA>> &nfas,
                         const std::string &outputPath);
};

#endif // TABLE_GEN_H
