#pragma once

// Common utilities etc. that need to be included early.

#ifdef USE_EXCEPTION_SPECIFICATIONS
    #define THROW(...) throw( __VA_ARGS__ )
#else
    #define THROW(...)
#endif

