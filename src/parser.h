#ifndef PARSER_H
#define PARSER_H

#include <memory>
#include <vector>
#include <set>
#include "lexer.h"

enum class ASTNodeType {
    LITERAL,
    DOT,
    CONCATENATION,
    UNION,
    STAR,
    PLUS,
    OPTIONAL
};

class ASTNode {
public:
    ASTNodeType type;
    virtual ~ASTNode() = default;

    // The following fields are populated by the NFA builder (Stage 2), NOT the parser.
    bool nullable = false;
    std::set<int> firstpos;
    std::set<int> lastpos;

    explicit ASTNode(ASTNodeType t) : type(t) {}
};

std::string nodeTypeToString(ASTNodeType type);

class LiteralNode : public ASTNode {
public:
    char value;
    int position; // Used for Glushkov's construction
    explicit LiteralNode(char v) : ASTNode(ASTNodeType::LITERAL), value(v), position(-1) {}
};

class DotNode : public ASTNode {
public:
    int position; // Used for Glushkov's construction
    DotNode() : ASTNode(ASTNodeType::DOT), position(-1) {}
};

class ConcatenationNode : public ASTNode {
public:
    std::unique_ptr<ASTNode> left;
    std::unique_ptr<ASTNode> right;
    ConcatenationNode(std::unique_ptr<ASTNode> l, std::unique_ptr<ASTNode> r)
        : ASTNode(ASTNodeType::CONCATENATION), left(std::move(l)), right(std::move(r)) {}
};

class UnionNode : public ASTNode {
public:
    std::unique_ptr<ASTNode> left;
    std::unique_ptr<ASTNode> right;
    UnionNode(std::unique_ptr<ASTNode> l, std::unique_ptr<ASTNode> r)
        : ASTNode(ASTNodeType::UNION), left(std::move(l)), right(std::move(r)) {}
};

class StarNode : public ASTNode {
public:
    std::unique_ptr<ASTNode> inner;
    explicit StarNode(std::unique_ptr<ASTNode> i)
        : ASTNode(ASTNodeType::STAR), inner(std::move(i)) {}
};

class PlusNode : public ASTNode {
public:
    std::unique_ptr<ASTNode> inner;
    explicit PlusNode(std::unique_ptr<ASTNode> i)
        : ASTNode(ASTNodeType::PLUS), inner(std::move(i)) {}
};

class OptionalNode : public ASTNode {
public:
    std::unique_ptr<ASTNode> inner;
    explicit OptionalNode(std::unique_ptr<ASTNode> i)
        : ASTNode(ASTNodeType::OPTIONAL), inner(std::move(i)) {}
};
