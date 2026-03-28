#include "nfa.h"
#include <iostream>
#include <algorithm>

int NFABuilder::globalStateCounter = 0;

void NFA::addState(int id, bool isAccept) {
    states.emplace(id, NFAState(id, isAccept));
}

void NFA::addTransition(int fromId, unsigned char c, int toId) {
    states.at(fromId).transitions[c].insert(toId);
}

bool NFA::simulate(const std::string& input) const {
    std::set<int> activeStates = {startStateId};
    
    for (unsigned char c : input) {
        std::set<int> nextStates;
        for (int stateId : activeStates) {
            auto& state = states.at(stateId);
            if (state.transitions.count(c)) {
                for (int nextId : state.transitions.at(c)) {
                    nextStates.insert(nextId);
                }
            }
        }
        activeStates = nextStates;
        if (activeStates.empty()) return false;
    }
    
    for (int stateId : activeStates) {
        if (states.at(stateId).isAccept) return true;
    }
    return false;
}

std::unique_ptr<NFA> NFABuilder::build(ASTNode* root, int regexIdx) {
    if (!root) return nullptr;

    auto nfa = std::make_unique<NFA>(regexIdx);
    
    // 1. Linearization (assign positions to symbols)
    int localPosCounter = 1;
    std::map<int, unsigned char> posToChar; // 0 is DOT, others are literal chars
    linearize(root, localPosCounter, posToChar);
    int numPositions = localPosCounter - 1;

    // 2. Compute nullable, firstpos, lastpos
    computeNullableFirstLast(root);

    // 3. Compute followpos
    std::map<int, std::set<int>> followpos;
    for (int i = 1; i <= numPositions; ++i) {
        followpos[i] = std::set<int>();
    }
    computeFollowpos(root, followpos);

    // 4. Build NFA states and transitions
    // Glushkov states: 0 (initial) + 1..n
    // We map these to global IDs
    int startGlobalId = globalStateCounter;
    nfa->startStateId = startGlobalId;
    std::map<int, int> localToGlobal;
    localToGlobal[0] = startGlobalId;
    nfa->addState(startGlobalId, root->nullable);

    for (int i = 1; i <= numPositions; ++i) {
        localToGlobal[i] = ++globalStateCounter;
        bool isAccept = (root->lastpos.find(i) != root->lastpos.end());
        nfa->addState(localToGlobal[i], isAccept);
    }
    // Increment for the next NFA to use
    globalStateCounter++;

    // Transitions from state 0
    for (int p : root->firstpos) {
        unsigned char c = posToChar[p];
        if (c == 0) { // DOT
            for (int val = 0; val < 256; ++val) {
                nfa->addTransition(localToGlobal[0], static_cast<unsigned char>(val), localToGlobal[p]);
            }
        } else {
            nfa->addTransition(localToGlobal[0], c, localToGlobal[p]);
        }
    }

    // Transitions from state p > 0
    for (int p = 1; p <= numPositions; ++p) {
        for (int q : followpos[p]) {
            unsigned char c = posToChar[q];
            if (c == 0) { // DOT
                for (int val = 0; val < 256; ++val) {
                    nfa->addTransition(localToGlobal[p], static_cast<unsigned char>(val), localToGlobal[q]);
                }
            } else {
                nfa->addTransition(localToGlobal[p], c, localToGlobal[q]);
            }
        }
    }

    return nfa;
}

void NFABuilder::linearize(ASTNode* node, int& posCounter, std::map<int, unsigned char>& posToChar) {
    if (!node) return;
    switch (node->type) {
        case ASTNodeType::LITERAL: {
            auto n = static_cast<LiteralNode*>(node);
            n->position = posCounter++;
            posToChar[n->position] = static_cast<unsigned char>(n->value);
            break;
        }
        case ASTNodeType::DOT: {
            auto n = static_cast<DotNode*>(node);
            n->position = posCounter++;
            posToChar[n->position] = 0; // Use 0 to represent DOT
            break;
        }
        case ASTNodeType::CONCATENATION: {
            auto n = static_cast<ConcatenationNode*>(node);
            linearize(n->left.get(), posCounter, posToChar);
            linearize(n->right.get(), posCounter, posToChar);
            break;
        }
        case ASTNodeType::UNION: {
            auto n = static_cast<UnionNode*>(node);
            linearize(n->left.get(), posCounter, posToChar);
            linearize(n->right.get(), posCounter, posToChar);
            break;
        }
        case ASTNodeType::STAR: {
            auto n = static_cast<StarNode*>(node);
            linearize(n->inner.get(), posCounter, posToChar);
            break;
        }
        case ASTNodeType::PLUS: {
            auto n = static_cast<PlusNode*>(node);
            linearize(n->inner.get(), posCounter, posToChar);
            break;
        }
        case ASTNodeType::OPTIONAL: {
            auto n = static_cast<OptionalNode*>(node);
            linearize(n->inner.get(), posCounter, posToChar);
            break;
        }
    }
}

void NFABuilder::computeNullableFirstLast(ASTNode* node) {
    if (!node) return;
    switch (node->type) {
        case ASTNodeType::LITERAL: {
            auto n = static_cast<LiteralNode*>(node);
            n->nullable = false;
            n->firstpos = {n->position};
            n->lastpos = {n->position};
            break;
        }
        case ASTNodeType::DOT: {
            auto n = static_cast<DotNode*>(node);
            n->nullable = false;
            n->firstpos = {n->position};
            n->lastpos = {n->position};
            break;
        }
        case ASTNodeType::CONCATENATION: {
            auto n = static_cast<ConcatenationNode*>(node);
            computeNullableFirstLast(n->left.get());
            computeNullableFirstLast(n->right.get());
            n->nullable = n->left->nullable && n->right->nullable;
            
            n->firstpos = n->left->firstpos;
            if (n->left->nullable) {
                n->firstpos.insert(n->right->firstpos.begin(), n->right->firstpos.end());
            }
            
            n->lastpos = n->right->lastpos;
            if (n->right->nullable) {
                n->lastpos.insert(n->left->lastpos.begin(), n->left->lastpos.end());
            }
            break;
        }
        case ASTNodeType::UNION: {
            auto n = static_cast<UnionNode*>(node);
            computeNullableFirstLast(n->left.get());
            computeNullableFirstLast(n->right.get());
            n->nullable = n->left->nullable || n->right->nullable;
            
            n->firstpos = n->left->firstpos;
            n->firstpos.insert(n->right->firstpos.begin(), n->right->firstpos.end());
            
            n->lastpos = n->left->lastpos;
            n->lastpos.insert(n->right->lastpos.begin(), n->right->lastpos.end());
            break;
        }
        case ASTNodeType::STAR: {
            auto n = static_cast<StarNode*>(node);
            computeNullableFirstLast(n->inner.get());
            n->nullable = true;
            n->firstpos = n->inner->firstpos;
            n->lastpos = n->inner->lastpos;
            break;
        }
        case ASTNodeType::PLUS: {
            auto n = static_cast<PlusNode*>(node);
            computeNullableFirstLast(n->inner.get());
            n->nullable = n->inner->nullable;
            n->firstpos = n->inner->firstpos;
            n->lastpos = n->inner->lastpos;
            break;
        }
        case ASTNodeType::OPTIONAL: {
            auto n = static_cast<OptionalNode*>(node);
            computeNullableFirstLast(n->inner.get());
            n->nullable = true;
            n->firstpos = n->inner->firstpos;
            n->lastpos = n->inner->lastpos;
            break;
        }
    }
}

void NFABuilder::computeFollowpos(ASTNode* node, std::map<int, std::set<int>>& followpos) {
    if (!node) return;
    switch (node->type) {
        case ASTNodeType::CONCATENATION: {
            auto n = static_cast<ConcatenationNode*>(node);
            computeFollowpos(n->left.get(), followpos);
            computeFollowpos(n->right.get(), followpos);
            for (int p : n->left->lastpos) {
                followpos[p].insert(n->right->firstpos.begin(), n->right->firstpos.end());
            }
            break;
        }
        case ASTNodeType::STAR: {
            auto n = static_cast<StarNode*>(node);
            computeFollowpos(n->inner.get(), followpos);
            for (int p : n->inner->lastpos) {
                followpos[p].insert(n->inner->firstpos.begin(), n->inner->firstpos.end());
            }
            break;
        }
        case ASTNodeType::PLUS: {
            auto n = static_cast<PlusNode*>(node);
            computeFollowpos(n->inner.get(), followpos);
            for (int p : n->inner->lastpos) {
                followpos[p].insert(n->inner->firstpos.begin(), n->inner->firstpos.end());
            }
            break;
        }
        case ASTNodeType::UNION: {
            auto n = static_cast<UnionNode*>(node);
            computeFollowpos(n->left.get(), followpos);
            computeFollowpos(n->right.get(), followpos);
            break;
        }
        case ASTNodeType::OPTIONAL: {
            auto n = static_cast<OptionalNode*>(node);
            computeFollowpos(n->inner.get(), followpos);
            break;
        }
        default: break;
    }
}
