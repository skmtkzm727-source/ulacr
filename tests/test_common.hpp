#pragma once
#include <iostream>
#include <sstream>
#include <stdexcept>

#define ULACR_CHECK(cond) \
    do { \
        if (!(cond)) { \
            std::ostringstream oss; \
            oss << "CHECK failed: " #cond " at " << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(oss.str()); \
        } \
    } while (0)

#define ULACR_CHECK_EQ(a, b) \
    do { \
        auto _va = (a); auto _vb = (b); \
        if (!(_va == _vb)) { \
            std::ostringstream oss; \
            oss << "CHECK_EQ failed: " #a " != " #b " (" << _va << " != " << _vb << ") at " \
                << __FILE__ << ":" << __LINE__; \
            throw std::runtime_error(oss.str()); \
        } \
    } while (0)
