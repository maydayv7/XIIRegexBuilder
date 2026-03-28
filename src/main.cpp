#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "lexer.h"
#include "parser.h"

void printAST(const ASTNode* node, int indent = 0) {
    if (!node) return;
    for (int i = 0; i < indent; ++i) std::cout << "  ";
    
    std::cout << nodeTypeToString(node->type);
    if (node->type == ASTNodeType::LITERAL) {
        std::cout << " (" << static_cast<const LiteralNode*>(node)->value << ")";
    }
    std::cout << std::endl;

    switch (node->type) {
        case ASTNodeType::CONCATENATION: {
            auto c = static_cast<const ConcatenationNode*>(node);
            printAST(c->left.get(), indent + 1);
            printAST(c->right.get(), indent + 1);
            break;
        }
        case ASTNodeType::UNION: {
            auto u = static_cast<const UnionNode*>(node);
            printAST(u->left.get(), indent + 1);
            printAST(u->right.get(), indent + 1);
            break;
        }
        case ASTNodeType::STAR: {
            auto s = static_cast<const StarNode*>(node);
            printAST(s->inner.get(), indent + 1);
            break;
        }
        case ASTNodeType::PLUS: {
            auto p = static_cast<const PlusNode*>(node);
            printAST(p->inner.get(), indent + 1);
            break;
        }
        case ASTNodeType::OPTIONAL: {
            auto o = static_cast<const OptionalNode*>(node);
            printAST(o->inner.get(), indent + 1);
            break;
        }
        default: break;
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <regex_file>" << std::endl;
        return 1;
    }

    std::string filename = argv[1];
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open " << filename << std::endl;
        return 1;
    }

    std::string line;
    int lineNum = 0;
    int regexIdx = 0;

    while (std::getline(file, line)) {
        lineNum++;

        // Trim leading and trailing whitespace
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue; // Blank line
        size_t last = line.find_last_not_of(" \t\r\n");
        std::string trimmedLine = line.substr(first, (last - first + 1));

        // Skip comments
        if (trimmedLine[0] == '#') continue;

        std::cout << "--- Regex [" << regexIdx++ << "]: " << trimmedLine << " ---" << std::endl;

        Lexer lexer(trimmedLine, lineNum);
        try {
            std::vector<Token> tokens = lexer.tokenize();
            Parser parser(tokens);
            auto ast = parser.parse();
            printAST(ast.get());
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
        std::cout << std::endl;
    }

    return 0;
}
