#ifndef EMITTER_H
#define EMITTER_H

#include <vector>
#include <string>
#include <memory>
#include "nfa.h"

class Emitter {
public:
    explicit Emitter(const std::vector<std::unique_ptr<NFA>>& nfas);
    void emit(const std::string& outputDir);

private:
    const std::vector<std::unique_ptr<NFA>>& nfas;

    void emitNFAModule(const NFA& nfa, const std::string& outputDir);
    void emitTopModule(const std::string& outputDir);
    void emitTestbench(const std::string& outputDir);
};

#endif // EMITTER_H
