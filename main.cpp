#include "pipeline_core.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>

// Forward declarations of setup_manager utilities
std::string auto_detect_asiair_ip();
void generate_dynamic_bash_script(const std::string& detected_ip);

// Forward declarations of tracking_engine utilities
TelescopeState calculate_predictive_geometry(double target_ra_hours, double target_dec_deg, 
                                             double lat_rad, double lon_deg, double lookahead_seconds);
void process_hardware_tracking_tick(const TelescopeState& state, const RuntimeConfiguration& conf, 
                                    int caa_id, UniversalRotatorState& rotator, int& global_frame_counter);

// Global variable linkages to allow cross-module visibility across open-source engines
double target_ra_hours = 0.0;
double target_dec_deg  = 0.0;

int main(int argc, char* argv[]) {
    if (argc == 1) {
        std::cout << "[INIT] Running ASIAIR Network Environment Interface Scanner...\n";
        generate_dynamic_bash_script(auto_detect_asiair_ip());
        std::cout << "[SUCCESS] Launch script generated: ./launch_pipeline.sh\n";
        return 0;
    }
    if (argc != 3) {
        std::cerr << "[ERROR] Expected syntax: " << argv << " <RA_Hours> <DEC_Degrees>\n";
        return -1;
    }

    target_ra_hours = std::stod(argv[1]);
    target_dec_deg  = std::stod(argv[2]);

    RuntimeConfiguration conf;
    if (!conf.load_from_file("rotator_config.conf")) {
        std::cerr << "[ERROR] Configuration parsing failed: 'rotator_config.conf' not found or invalid format.\n";
        return -1;
    }

    if (conf.exp_duration < 1.0) conf.exp_duration = 60.0;

    std::vector<std::string> clear_targets;
    clear_targets.push_back("custom_flats");
    clear_targets.push_back("calibrated_lights");
    clear_targets.push_back("processed");
    
    for (size_t i = 0; i < clear_targets.size(); ++i) {
        clear_directory_contents(clear_targets[i]);
    }
    
    std::vector<std::string> req_dirs;
    req_dirs.push_back("lights");
    req_dirs.push_back("custom_flats");
    req_dirs.push_back("calibrated_lights");
    req_dirs.push_back("processed");
    
    for (size_t i = 0; i < req_dirs.size(); ++i) { 
        ensure_directory_exists(req_dirs[i]);
    }

    if (!file_exists("master_flat_0deg.fits") || !file_exists("master_bias.fits")) {
        std::cerr << "[ERROR] Calibration error: Missing mandatory master FITS files in root folder.\n";
        return -1;
    }
    
    if (CAAGetNum() <= 0 || CAAOpen(0) != 0) {
        std::cerr << "[ERROR] ZWO CAA Rotator missing or busy on USB interface.\n";
        return -1;
    }
    
    UniversalRotatorState rotator_hardware_state;
    int global_frame_counter = 1;
    time_t last_exposure_log_time = time(0);

    std::cout << "[STANDALONE] Running in Pure Precision Standalone Mode.\n";
    std::cout << "[TRACKING STARTED] Monitoring celestial path natively via local CPU clocks...\n";

    while (true) {
        TelescopeState live_tele = calculate_predictive_geometry(target_ra_hours, target_dec_deg, conf.lat_rad, conf.lon_deg, 0.0);
        
        if (live_tele.altitude > 0.0) {
            process_hardware_tracking_tick(live_tele, conf, 0, rotator_hardware_state, global_frame_counter);
            
            // INDEPENDENT CADENCE EMISSION TIMING ENGINE
            time_t current_session_time = time(0);
            if (current_session_time - last_exposure_log_time >= static_cast<time_t>(conf.exp_duration)) {
                last_exposure_log_time = current_session_time;

                // Re-calculate the precise midpoint angle of the frame that just finished
                double historical_midpoint_lookahead = -(conf.exp_duration / 2.0);
                TelescopeState hist_tele = calculate_predictive_geometry(target_ra_hours, target_dec_deg, conf.lat_rad, conf.lon_deg, historical_midpoint_lookahead);
                double frame_midpoint_angle = calculate_field_angle(hist_tele.altitude, hist_tele.azimuth, conf.lat_rad);

                std::string mock_fits_name = "Light_Obj_" + std::to_string(global_frame_counter);
                while (mock_fits_name.length() < 14) { mock_fits_name.insert(10, "0"); }
                mock_fits_name += ".fits";

                std::cout << "\n[FRAME CAPTURE LOG] Exposure complete. Logging slot: " << mock_fits_name 
                          << " at Midpoint Angle: " << frame_midpoint_angle << "°\n";

                std::ofstream log_file("session_rotation_manifest.csv", std::ios::app);
                if (log_file.is_open()) {
                    log_file << mock_fits_name << ","
                             << std::fixed << std::setprecision(2) << frame_midpoint_angle << ","
                             << (rotator_hardware_state.frame_is_inverted ? "INVERTED" : "UPRIGHT") << "\n";
                    log_file.close();
                }

                execute_flat_transformation(global_frame_counter, frame_midpoint_angle, conf);
                global_frame_counter++;
            }

            std::cout << "[TRACKING ACTIVE] Motor Target Position: " << rotator_hardware_state.current_physical_steps << " steps\r" << std::flush;
        } else {
            std::cout << "[TRACKING PAUSED] Celestial target resides completely below local horizon limits.\r" << std::flush;
        }

        usleep(1000000); // 1-second ticks
    }

    CAAClose(0);
    return 0;
}

