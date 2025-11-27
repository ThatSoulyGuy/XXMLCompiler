#ifndef XXML_DEBUG_H
#define XXML_DEBUG_H

#include <iostream>

// Debug output macro for XXML Compiler
// Define XXML_DEBUG to enable debug output, otherwise it's a no-op
#ifdef XXML_DEBUG
#define DEBUG_OUT(x) std::cerr << x
#else
#define DEBUG_OUT(x)
#endif

#endif // XXML_DEBUG_H
