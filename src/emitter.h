#ifndef EMITTER_H
#define EMITTER_H

#include <vector>
#include <string>
#include <memory>
#include "nfa.h"

#include <filesystem>
#include <system_error>

class Emitter
{
public:
    Emitter() = delete; // Static class, no instances

    /**
