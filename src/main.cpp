// main.cpp
#include "HelloVulkanApp.hpp"

#include <iostream>
#include <stdexcept>
#include <cstdlib>

int main() {
    HelloVulkanApp app;

    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}