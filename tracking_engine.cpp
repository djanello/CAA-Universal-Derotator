#include "pipeline_core.h"
#include <iostream>
#include <fstream>
#include <cmath>
#include <cstdlib>

TelescopeState calculate_predictive_geometry(double target_ra_hours, double target_dec_deg, 
                                             double lat_rad, double lon_deg, double lookahead_seconds) {
    TelescopeState telescope;
    time_t future_time = time(0) + static_cast<time_t>(lookahead_seconds);
    
    double julian_date = (static_cast<double>(future_time) / 86400.0) + 2440587.5;
    double t_j2000 = julian_date - 2451545.0; 

    double gmst_deg = 280.46061837 + (360.98564736629 * t_j2000);
    double lst_deg = gmst_deg + lon_deg; 
    
    double lst_rad = std::fmod(lst_deg, 360.0) * (M_PI / 180.0);
    if (lst_rad < 0.0) lst_rad += (2.0 * M_PI);

    double dec_rad = target_dec_deg * (M_PI / 180.0);
    double ra_rad  = target_ra_hours * 15.0 * (M_PI / 180.0);
    double ha_rad = lst_rad - ra_rad;

    double sin_alt = (std::sin(lat_rad) * std::sin(dec_rad)) + 
                     (std::cos(lat_rad) * std::cos(dec_rad) * std::cos(ha_rad));
    if (sin_alt > 1.0) sin_alt = 1.0; if (sin_alt < -1.0) sin_alt = -1.0;
    telescope.altitude = std::asin(sin_alt) * (180.0 / M_PI);

    double cos_az = (std::sin(dec_rad) - (std::sin(lat_rad) * sin_alt)) / 
                    (std::cos(lat_rad) * std::cos(std::asin(sin_alt)));
    if (cos_az > 1.0) cos_az = 1.0; if (cos_az < -1.0) cos_az = -1.0;
    
    double az_deg = std::acos(cos_az) * (180.0 / M_PI);
    telescope.azimuth = (std::sin(ha_rad) > 0) ? (360.0 - az_deg) : az_deg;
    telescope.mechanical_status = "Tracking";

    return telescope;
}

void process_hardware_tracking_tick(const TelescopeState& state, const RuntimeConfiguration& conf, 
                                    int caa_id, UniversalRotatorState& rotator, int& global_frame_counter) {
    if (state.mechanical_status == "Slewing" || state.mechanical_status == "Dithering" || state.mechanical_status == "ReCentering") {
        CAAStop(caa_id);
        return;
    }

    // 1. Compute the high-precision 4-Quadrant Equatorial Parallactic Angle
    time_t now = time(0);
    double julian_date = (static_cast<double>(now) / 86400.0) + 2440587.5;
    double t_j2000 = julian_date - 2451545.0; 
    double gmst_deg = 280.46061837 + (360.98564736629 * t_j2000);
    double lst_deg = gmst_deg + conf.lon_deg;
    double lst_rad = std::fmod(lst_deg, 360.0) * (M_PI / 180.0);
    if (lst_rad < 0.0) lst_rad += (2.0 * M_PI);

    extern double target_ra_hours;
    extern double target_dec_deg;
    
    double dec_rad = target_dec_deg * (M_PI / 180.0);
    double ra_rad  = target_ra_hours * 15.0 * (M_PI / 180.0);
    double ha_rad  = lst_rad - ra_rad;

    double y = std::sin(ha_rad);
    double x = (std::tan(conf.lat_rad) * std::cos(dec_rad)) - (std::sin(dec_rad) * std::cos(ha_rad));
    
    double target_angle_deg = std::atan2(y, x) * (180.0 / M_PI);
    if (target_angle_deg < 0.0) target_angle_deg += 360.0;
    
    if (rotator.frame_is_inverted) {
        target_angle_deg += 180.0;
        if (target_angle_deg >= 360.0) target_angle_deg -= 360.0;
    }

    // 2. Velocity-Driven Tracking Delta Baseline
    static double last_calculated_angle = -999.0;
    
    if (last_calculated_angle < -900.0) {
        last_calculated_angle = target_angle_deg;
        std::cout << "\n[INIT] Motor synchronized to initial sky baseline angle: " << target_angle_deg << "°\n";
        return;
    }

    double tracking_velocity_delta = target_angle_deg - last_calculated_angle;
    
    if (tracking_velocity_delta > 180.0)  tracking_velocity_delta -= 360.0;
    if (tracking_velocity_delta < -180.0) tracking_velocity_delta += 360.0;
    
    last_calculated_angle = target_angle_deg;

    int proposed_physical_target = rotator.current_physical_steps + static_cast<int>(std::round(tracking_velocity_delta * STEPS_PER_DEGREE));

    // 3. 270-Degree Cable Wrap Boundary Enforcer
    if (proposed_physical_target > rotator.mechanical_max_limit || proposed_physical_target < rotator.mechanical_min_limit) {
        std::cout << "\n[ANTI-WRAP] Cable threshold limit hit. Swapping orientation matrix 180°...\n";
        CAAStop(caa_id);
        
        rotator.frame_is_inverted = !rotator.frame_is_inverted;
        
        target_angle_deg = std::atan2(y, x) * (180.0 / M_PI);
        if (target_angle_deg < 0.0) target_angle_deg += 360.0;
        if (rotator.frame_is_inverted) {
            target_angle_deg += 180.0;
            if (target_angle_deg >= 360.0) target_angle_deg -= 360.0;
        }
        
        last_calculated_angle = target_angle_deg;
        return;
    }

    // 4. Sensitivity Deadzone Filter (0.04° window)
    int step_delta = std::abs(proposed_physical_target - rotator.current_physical_steps);
    if (step_delta >= 2) { 
        CAAMove(caa_id, proposed_physical_target);
        rotator.current_physical_steps = proposed_physical_target;
    }
}

