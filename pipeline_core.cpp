#ifndef PIPELINE_CORE_H
#define PIPELINE_CORE_H

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cmath>
#include <map>
#include <cstdlib>
#include <iomanip>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

#include "CAA_API.h"

const int STEPS_FOR_360_DEG   = 18000;
const int STEPS_FOR_180_DEG   = 9000;
const double STEPS_PER_DEGREE = 50.0;

struct RuntimeConfiguration {
    int lat_deg = 0, lat_min = 0;
    double lat_sec = 0.0;
    double lat_rad = 0.0;
    int img_w = 0, img_h = 0;
    double dx = 0.0, dy = 0.0;
    double tx = 1.0, ty = 1.0;
    double exp_duration = 0.0;

    bool load_from_file(const std::string& filepath) {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;

        std::string line;
        std::map<std::string, std::string> config_map;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::stringstream ss(line);
            std::string key, val, equal;
            if (ss >> key >> equal >> val && equal == "=") {
                config_map[key] = val;
            }
        }
        try {
            lat_deg      = std::stoi(config_map["latitude_deg"]);
            lat_min      = std::stoi(config_map["latitude_min"]);
            lat_sec      = std::stod(config_map["latitude_sec"]);
            img_w        = std::stoi(config_map["sensor_width"]);
            img_h        = std::stoi(config_map["sensor_height"]);
            dx           = std::stod(config_map["offset_x"]);
            dy           = std::stod(config_map["offset_y"]);
            tx           = std::stod(config_map["tilt_scale_x"]);
            ty           = std::stod(config_map["tilt_scale_y"]);
            exp_duration = std::stod(config_map["exposure_duration_sec"]);
            
            double decimal_deg = lat_deg + (lat_min / 60.0) + (lat_sec / 3600.0);
            lat_rad = decimal_deg * (M_PI / 180.0);
        } catch (...) { return false; }
        return true;
    }
};

struct TelescopeState {
    double altitude = 0.0;
    double azimuth = 0.0;
    std::string mechanical_status = "Unknown";
};

struct UniversalRotatorState {
    int current_physical_steps = 0; 
    int mechanical_min_limit = -6750; 
    int mechanical_max_limit =  6750; 
    bool frame_is_inverted = false; 
};

inline bool clear_directory_contents(const std::string& dir_path) {
    DIR* dir = opendir(dir_path.c_str());
    if (dir == nullptr) return false;
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename == "." || filename == "..") continue;
        std::string full_path = dir_path + "/" + filename;
        unlink(full_path.c_str());
    }
    closedir(dir);
    return true;
}

inline bool file_exists(const std::string& path) {
    struct stat buffer;
    return (stat(path.c_str(), &buffer) == 0);
}

inline bool ensure_directory_exists(const std::string& path) {
    struct stat st = {0};
    if (stat(path.c_str(), &st) == -1) {
        if (mkdir(path.c_str(), 0777) != 0) return false;
    }
    return true;
}

inline double calculate_field_angle(double altitude_deg, double azimuth_deg, double lat_rad) {
    double alt_rad = altitude_deg * (M_PI / 180.0);
    double az_rad  = azimuth_deg * (M_PI / 180.0);
    double y = std::sin(az_rad);
    double x = (std::tan(lat_rad) * std::cos(alt_rad)) - (std::sin(alt_rad) * std::cos(az_rad));
    double angle_deg = std::atan2(y, x) * (180.0 / M_PI);
    if (angle_deg < 0.0) angle_deg += 360.0;
    return angle_deg;
}

inline void execute_flat_transformation(int frame_id, double raw_deg, const RuntimeConfiguration& conf) {
    double siril_angle = 360.0 - raw_deg;
    if (siril_angle >= 360.0) siril_angle -= 360.0;

    std::string pad = std::to_string(frame_id);
    while (pad.length() < 4) pad = "0" + pad;

    std::ofstream script("process_flat.ssf");
    script << "requires 1.2.0\nload master_flat_0deg.fits\nsub master_bias.fits\n";

    double shifted_center_x = (conf.img_w / 2.0) + (conf.dx * conf.tx);
    double shifted_center_y = (conf.img_h / 2.0) + (conf.dy * conf.ty);
    int crop_x1 = static_cast<int>(shifted_center_x - (conf.img_w / 2.0));
    int crop_y1 = static_cast<int>(shifted_center_y - (conf.img_h / 2.0));

    if (std::abs(conf.dx) > 0.1 || std::abs(conf.dy) > 0.1) {
        script << "boxselect " << (crop_x1 < 0 ? 0 : crop_x1) << " " << (crop_y1 < 0 ? 0 : crop_y1) 
               << " " << conf.img_w << " " << conf.img_h << "\ncrop\n";
    }
    script << "rotate " << siril_angle << " -nocrop\nsave custom_flats/flat_match_" << pad << ".fits\nclose\n";
    script.close();

    std::system("siril -s process_flat.ssf > /dev/null 2>&1");
}

#endif

