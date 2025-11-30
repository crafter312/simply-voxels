#pragma once
#include <iostream>

#ifdef ENABLE_VK_LOG
    // Compile-time enabled logging (zero overhead when not defined)
    #define VK_LOG(expr) do { std::cout << expr << std::endl; } while(0)
#else
    #define VK_LOG(expr) do { (void)0; } while(0)
#endif