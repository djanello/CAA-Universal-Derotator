/*
 * Copyright (C) 2026 David Janello
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://gnu.org>.
 */

#include "pipeline_core.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

std::string auto_detect_asiair_ip() {
    struct addrinfo hints, *res;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo("asiair.local", "4400", &hints, &res) == 0) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(((struct sockaddr_in*)res->ai_addr)->sin_addr), ip_str, INET_ADDRSTRLEN);
        freeaddrinfo(res);
        return std::string(ip_str);
    }
    std::vector<std::string> standard_gateways;
    standard_gateways.push_back("10.0.0.1");
    standard_gateways.push_back("192.168.21.1");
    
    for (size_t i = 0; i < standard_gateways.size(); ++i) {
        std::string ip = standard_gateways[i];
        int test_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (test_sock >= 0) {
            struct sockaddr_in target;
            target.sin_family = AF_INET; target.sin_port = htons(4400);
            target.sin_addr.s_addr = inet_addr(ip.c_str());
            struct timeval tv = {1, 0};
            setsockopt(test_sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(tv));
            if (connect(test_sock, (struct sockaddr*)&target, sizeof(target)) == 0) {
                close(test_sock); return ip;
            }
            close(test_sock);
        }
    }
    return "10.0.0.1";
}

void generate_dynamic_bash_script(const std::string& detected_ip) {
    // === THE CRITICAL SYSTEM AUTO-DETECT CHECK ===
    // Verification: Scan host environment paths to confirm if the Siril binary is present
    std::cout << "[INIT] Verifying background software dependencies...\n";
    if (std::system("which siril > /dev/null 2>&1") != 0) {
        std::cerr << "\n[CRITICAL DEPRENDENCY ERROR] 'siril' binary was not found on your system path.\n";
        std::cerr << "                             This application requires Siril v1.4.0+ for stacking.\n";
        std::cerr << "To resolve on Ubuntu/Debian, run: sudo apt install siril\n\n";
        return;
    }
    std::cout << "[SUCCESS] Siril command line binary identified successfully.\n";

    std::ofstream b("launch_pipeline.sh");
    if (!b.is_open()) {
        std::cerr << "[ERROR] Critical configuration step failed: Unable to write 'launch_pipeline.sh'.\n";
        return;
    }

    b << "#!/bin/bash\n\nMOUNT_POINT=\"/mnt/asiair\"\nASIAIR_IP=\"" << detected_ip << "\"\n";
    b << "CONFIG_FILE=\"rotator_config.conf\"\nBINARY_NAME=\"./rotator_pipeline\"\n\n";

    std::ifstream template_file("script_template.txt");
    if (!template_file.is_open()) {
        std::cerr << "[ERROR] Launcher script warning: 'script_template.txt' file missing from root directory.\n";
        b.close();
        return;
    }

    std::string line;
    while (std::getline(template_file, line)) {
        b << line << "\n";
    }

    template_file.close();
    b.close();
    chmod("launch_pipeline.sh", 0755);
}

