#pragma once

#include "Logger.hpp"

#include <cstdlib>

#ifdef NDEBUG

#define VG_ASSERT(condition, message)

#else

#define VG_ASSERT(condition, message)                  \
    do                                                 \
    {                                                  \
        if (!(condition))                              \
        {                                              \
            Logger::Log(                               \
                LogLevel::Fatal,                       \
                "Assert",                              \
                message                                \
            );                                         \
                                                       \
            std::abort();                              \
        }                                              \
    } while (false)

#endif