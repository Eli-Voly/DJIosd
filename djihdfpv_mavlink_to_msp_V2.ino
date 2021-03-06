
/* DJI HD FPV Mavlink to MSP
 *  Converts Ardupilot Mavlink telemetry data to MSP telemetry data compatible with the DJI HD FPV system.
 *
 *  Arduino Nano TX to DJI Air unit RX(115200)
 *  Softwareserial RX is digital pin 8 connected to ardupilot TX telemetry port(57600)
 *  Softwareserial TX is digital pin 9 connected to ardupilot RX telemetry port(57600)
*/

#define SERIAL_TYPE                                                 0       //0==SoftSerial(Arduino_Nano), 1==HardSerial(others)
// #define MAH_CALIBRATION_FACTOR                                      1.0f    //used to calibrate mAh reading.
//#define SPEED_IN_KILOMETERS_PER_HOUR                                        //if commented out defaults to m/s
//#define IMPERIAL_UNITS                                                    //Altitude in feet, distance to home in miles.
// #define VEHICLE_TYPE                                                0       //0==ArduPlane, 1==ArduCopter, 2==INAVPlane, 3==INAVCopter. Used for flight modes
// #define STORE_GPS_LOCATION_IN_SUBTITLE_FILE                                 //comment out to disable. Stores GPS location in the goggles .srt file in place of the "uavBat:" field at a slow rate of ~2-3s per GPS coordinate
//#define DISPLAY_THROTTLE_POSITION                                         //will display the current throttle position(0-100%) in place of the osd_roll_pids_pos element.
//#define DISPLAY_WIND_SPEED_AND_DIRECTION                                  //Ardupilot only

#include <GCS_MAVLink.h>
#include <MSP.h>
#include "MSP_OSD.h"
#include "flt_modes.h"
#include "OSD_positions_config.h"

#if SERIAL_TYPE == 0
  #include <AltSoftSerial.h>
  HardwareSerial &mspSerial = Serial;
  AltSoftSerial mavlinkSerial;              // RX pin 8, TX pin 9
#elif SERIAL_TYPE == 1
  HardwareSerial &mspSerial = Serial2;
  HardwareSerial &mavlinkSerial = Serial3;
#endif

MSP msp;

uint32_t previousMillis_MSP = 0;
const uint32_t next_interval_MSP = 100;

uint8_t base_mode = MAV_MODE_PREFLIGHT;
uint8_t system_status = MAV_STATE_UNINIT;
uint32_t custom_mode = 0;                       //flight mode

uint8_t vbat = 0;
float airspeed = 0;
float groundspeed = 0;
int32_t relative_alt = 0;       // in milimeters
uint32_t altitude_msp = 0;      // EstimatedAltitudeCm
uint16_t rssi = 0;
uint8_t battery_remaining = 0;
uint32_t flightModeFlags = 0;
char craftname[15] = "";
int16_t amperage = 0;
uint16_t mAhDrawn = 0;
float f_mAhDrawn = 0.0;
uint8_t numSat = 0;
uint8_t pid_roll[3];
uint8_t pid_pitch[3];
uint8_t pid_yaw[3];
int32_t gps_lon = 0;
int32_t gps_lat = 0;
int32_t gps_alt = 0;
int32_t gps_home_lon = 0;
int32_t gps_home_lat = 0;
int32_t gps_home_alt = 0;
int16_t roll_angle = 0;
int16_t pitch_angle = 0;
//uint8_t escTemperature = 0;     // degrees celsius
//uint8_t rtc_date_time[9] = {20, 20, 01, 01, 00, 00, 00, 00, 00}; //YMDHMSM
uint32_t distanceToHome = 0;    // distance to home in meters
int16_t directionToHome = 0;   // direction to home in degrees
uint8_t fix_type = 0;           // < 0-1: no fix, 2: 2D fix, 3: 3D fix
uint8_t batteryCellCount = 0;
uint16_t batteryCapacity = 5200;
uint8_t legacyBatteryVoltage = 0;
uint8_t batteryState = 0;       // voltage color 0==white, 1==red
uint16_t batteryVoltage = 0;
int16_t heading = 0;
float dt = 0;
#ifdef MAH_CALIBRATION_FACTOR
float mAh_calib_factor = MAH_CALIBRATION_FACTOR;
#else
float mAh_calib_factor = 1;
#endif
uint8_t set_home = 1;
uint32_t general_counter = 0;
uint16_t blink_sats_orig_pos = osd_gps_sats_pos;
uint16_t blink_sats_blank_pos = 234;
uint32_t previousFlightMode = custom_mode;
uint8_t srtCounter = 1;
uint8_t thr_position = 0;
float wind_direction = 0;   // wind direction (degrees)
float wind_speed = 0;       // wind speed in ground plane (m/s)
float relative_wind_direction = 0;
float climb_rate = 0.0;

void setup()
{

    // Serial.begin(9600);
    mspSerial.begin(115200);
    msp.begin(mspSerial);
    mavlinkSerial.begin(57600);

    //Used as ground for mavlink input data
    pinMode(12, OUTPUT);
    digitalWrite(12, LOW);

}

void loop()
{

    //receive mavlink data
    mavl_receive();

    //send MSP data
    uint32_t currentMillis_MSP = millis();
    if ((uint32_t)(currentMillis_MSP - previousMillis_MSP) >= next_interval_MSP) {
        previousMillis_MSP = currentMillis_MSP;

        // GPS_calculateDistanceAndDirectionToHome();
        // set_flight_mode_flags();
        // mAh_drawn_calc();
        // blink_sats();

        // Serial.println(heading);
        send_msp_to_airunit();

        general_counter += next_interval_MSP;
    }

    // if(custom_mode != previousFlightMode){
    //     previousFlightMode = custom_mode;
    //     display_flight_mode();
    // }

    // if(batteryCellCount == 0 && vbat > 0)set_battery_cells_number();

    // check_system_status();

    // //display flight mode every 10s
    // if (general_counter % 10000 == 0)display_flight_mode();

    // //set GPS home when 3D fix
    // if(fix_type > 2 && set_home == 1 && gps_lat != 0 && gps_lon != 0 && numSat > 5){
    //   gps_home_lat = gps_lat;
    //   gps_home_lon = gps_lon;
    //   gps_home_alt = gps_alt;
    //   GPS_calc_longitude_scaling(gps_home_lat);
    //   set_home = 0;
    // }

#ifdef DISPLAY_WIND_SPEED_AND_DIRECTION
        display_wind_speed_and_direction();
#endif
}

void check_system_status()
{
    if(system_status == MAV_STATE_CRITICAL && (general_counter % 800 == 0))
    {
      char failsafe[15] = {"FAILSAFE"};
      show_text(&failsafe);
    }
    if(system_status == MAV_STATE_EMERGENCY && (general_counter % 800 == 0))
    {
      char em[15] = {"EMERGENCY"};
      show_text(&em);
    }
    if(system_status == MAV_STATE_CALIBRATING && (general_counter % 800 == 0))
    {
      char calib[15] = {"CALIBRATING"};
      show_text(&calib);
    }
}

void blink_sats()
{
    if(general_counter % 900 == 0 && set_home == 1 && blink_sats_orig_pos > 2000){
      invert_pos(&osd_gps_sats_pos, &blink_sats_blank_pos);
    }
    else if(set_home == 0){
      osd_gps_sats_pos = blink_sats_orig_pos;
    }
}

void mAh_drawn_calc()
{
    f_mAhDrawn += ((float)amperage * 10.0 * (millis() - dt) / 3600000.0) * mAh_calib_factor;
    mAhDrawn = (uint16_t)f_mAhDrawn;
    dt = millis();
}

void display_wind_speed_and_direction()
{
    relative_wind_direction = (360 + (wind_direction - heading));
    if(relative_wind_direction < 0)relative_wind_direction += 360;
    if(relative_wind_direction > 360)relative_wind_direction -= 360;

    String dir = "";

    if(relative_wind_direction <= 22.5)dir = "???";
    else if(relative_wind_direction <= 67.5)dir = "???";
    else if(relative_wind_direction <= 112.5)dir = "???";
    else if(relative_wind_direction <= 157.5)dir = "???";
    else if(relative_wind_direction <= 202.5)dir = "???";
    else if(relative_wind_direction <= 247.5)dir = "???";
    else if(relative_wind_direction <= 292.5)dir = "???";
    else if(relative_wind_direction <= 337.5)dir = "???";
    else if(relative_wind_direction <= 360)dir = "???";

    if((general_counter % 4000 == 0))
    {
      String w = "";
      #ifdef IMPERIAL_UNITS
          w = w + dir + " " + (wind_speed * 2.2369) + " mph";
      #elif defined(SPEED_IN_KILOMETERS_PER_HOUR)
          w = w + dir + " " + (wind_speed * 3.6) + " km/h";
      #else
          w = w + dir + " " + wind_speed + " m/s";
      #endif

      char wind[15] = {0};
      w.toCharArray(wind, 15);
      show_text(&wind);
    }
}

void set_flight_mode_flags()
{
    if(base_mode & MAV_MODE_FLAG_SAFETY_ARMED){
        flightModeFlags |= ARM_ACRO_BF;
    }
    else{
        flightModeFlags &= ~ARM_ACRO_BF;
    }
    if(custom_mode == STABILIZE){
        flightModeFlags |= STAB_BF;
    }
    else{
        flightModeFlags &= ~STAB_BF;
    }
    if((system_status == MAV_STATE_CRITICAL || system_status == MAV_STATE_EMERGENCY)){
        flightModeFlags |= FS_BF;
    }
    else{
        flightModeFlags &= ~FS_BF;
    }
    if(custom_mode == RTL){
        flightModeFlags |= RESC_BF;
    }
    else{
        flightModeFlags &= ~RESC_BF;
    }
}

void set_battery_cells_number()
{
    if(vbat < 43)batteryCellCount = 1;
    else if(vbat < 85)batteryCellCount = 2;
    else if(vbat < 127)batteryCellCount = 3;
    else if(vbat < 169)batteryCellCount = 4;
    else if(vbat < 211)batteryCellCount = 5;
    else if(vbat < 255)batteryCellCount = 6;
}

void show_text(char (*text)[15])
{
    memcpy(craftname, *text, sizeof(craftname));
}

void mavl_receive()
{

    mavlink_message_t msg;
    mavlink_status_t status;

    while(mavlinkSerial.available() > 0 )
    {

    uint8_t c = mavlinkSerial.read();

    if(mavlink_parse_char(MAVLINK_COMM_0, c, &msg, &status)) {

      switch(msg.msgid)
      {

          case MAVLINK_MSG_ID_SYS_STATUS:  // SYS_STATUS
          {
            mavlink_sys_status_t sys_status;
            mavlink_msg_sys_status_decode(&msg, &sys_status);

            vbat = (uint8_t)(sys_status.voltage_battery / 100);
            battery_remaining = (uint8_t)(sys_status.battery_remaining);
            amperage = sys_status.current_battery;

          }
          break;

          case MAVLINK_MSG_ID_ATTITUDE:
          {
            mavlink_attitude_t attitude;
            mavlink_msg_attitude_decode(&msg, &attitude);

            pitch_angle = attitude.pitch * 573;
            roll_angle = attitude.roll * 573;

          }

          case MAVLINK_MSG_ID_VFR_HUD:  // VFR_HUD
          {
            mavlink_vfr_hud_t vfr_hud;
            mavlink_msg_vfr_hud_decode(&msg, &vfr_hud);
            if (vfr_hud.airspeed >=.5) {
                airspeed = vfr_hud.airspeed;
            }
            if (vfr_hud.heading <= 360 && vfr_hud.heading >= 0) {
                heading = vfr_hud.heading;
            }

            //airspeed = vfr_hud.airspeed;                    //float
            groundspeed = vfr_hud.groundspeed;              //float
            // heading = vfr_hud.heading;
            thr_position = (uint8_t)vfr_hud.throttle;
            climb_rate = vfr_hud.climb;                     //float m/s

          }
          break;

          case MAVLINK_MSG_ID_GLOBAL_POSITION_INT:  // MAVLINK_MSG_ID_GLOBAL_POSITION_INT
          {
            mavlink_global_position_int_t global_position_int;
            mavlink_msg_global_position_int_decode(&msg, &global_position_int);

            relative_alt = global_position_int.relative_alt;
            gps_lat = global_position_int.lat;
            gps_lon = global_position_int.lon;
            gps_alt = global_position_int.alt;

          }
          break;

          case MAVLINK_MSG_ID_GPS_RAW_INT:  // MAVLINK_MSG_ID_GPS_RAW_INT
          {
            mavlink_gps_raw_int_t gps_raw_int;
            mavlink_msg_gps_raw_int_decode(&msg, &gps_raw_int);

            fix_type = gps_raw_int.fix_type;
            numSat = gps_raw_int.satellites_visible;

          }
          break;

        case MAVLINK_MSG_ID_HEARTBEAT:  // HEARTBEAT
          {
            mavlink_heartbeat_t heartbeat;
            mavlink_msg_heartbeat_decode(&msg, &heartbeat);

            base_mode = heartbeat.base_mode;
            system_status = heartbeat.system_status;
            custom_mode = heartbeat.custom_mode;

          }
          break;

         case MAVLINK_MSG_ID_RC_CHANNELS_RAW:  // RC_CHANNELS_RAW
          {
            mavlink_rc_channels_raw_t rc_channels_raw;
            mavlink_msg_rc_channels_raw_decode(&msg, &rc_channels_raw);

            rssi = (uint16_t)map(rc_channels_raw.rssi, 0, 255, 0, 1023); //scale 0-1023

          }
          break;

          case MAVLINK_MSG_ID_WIND:
          {
            mavlink_wind_t wind;
            mavlink_msg_wind_decode(&msg, &wind);

            wind_direction = wind.direction;
            wind_speed = wind.speed;
          }
          break;

      default:
        break;
      }
    }

    }
}

msp_battery_state_t battery_state = {0};
msp_name_t name = {0};
//msp_fc_version_t fc_version = {0};
msp_status_BF_t status_BF = {0};
msp_analog_t analog = {0};
msp_raw_gps_t raw_gps = {0};
msp_comp_gps_t comp_gps = {0};
msp_attitude_t attitude = {0};
msp_altitude_t altitude = {0};
#ifdef DISPLAY_THROTTLE_POSITION
msp_pid_t pid = {0};
#endif

void send_msp_to_airunit()
{

    //MSP_FC_VERSION
    // fc_version.versionMajor = 4;
    // fc_version.versionMinor = 1;
    // fc_version.versionPatchLevel = 1;
    // msp.send(MSP_FC_VERSION, &fc_version, sizeof(fc_version));

    //MSP_NAME
    memcpy(name.craft_name, craftname, sizeof(craftname));
    msp.send(MSP_NAME, &name, sizeof(name));

    //MSP_STATUS
    status_BF.flightModeFlags = flightModeFlags;
    msp.send(MSP_STATUS, &status_BF, sizeof(status_BF));

    //MSP_ANALOG
    analog.vbat = vbat;
    analog.rssi = rssi;
    analog.amperage = amperage;
    analog.mAhDrawn = mAhDrawn;
    msp.send(MSP_ANALOG, &analog, sizeof(analog));

    //MSP_BATTERY_STATE
    battery_state.amperage = amperage;
    battery_state.batteryVoltage = vbat * 10;
    battery_state.mAhDrawn = mAhDrawn;
    battery_state.batteryCellCount = batteryCellCount;
    battery_state.batteryCapacity = batteryCapacity;
    battery_state.batteryState = batteryState;
#ifdef STORE_GPS_LOCATION_IN_SUBTITLE_FILE
    if(general_counter % 400 == 0 && srtCounter != 0){
        String str = "";
        str = str + (abs(gps_lat)/10) + "" + (abs(gps_lon)/10);
        char m[] = {str[srtCounter-1], str[srtCounter]};
        battery_state.legacyBatteryVoltage = atoi(m);
        if(srtCounter <= (str.length()-2))srtCounter += 2;
          else srtCounter = 0;
    }
    else if(general_counter % 400 == 0 && srtCounter < 1){
        battery_state.legacyBatteryVoltage = 255;
        srtCounter = 1;
    }
#else
    battery_state.legacyBatteryVoltage = vbat;
#endif
    msp.send(MSP_BATTERY_STATE, &battery_state, sizeof(battery_state));

    //MSP_RAW_GPS
    raw_gps.lat = gps_lat;
    raw_gps.lon = gps_lon;
    raw_gps.numSat = numSat;
    raw_gps.alt = relative_alt / 10;
    raw_gps.groundSpeed = (int16_t)(airspeed * 100);
    //raw_gps.groundSpeed = 300;

    // msp.send(MSP_RAW_GPS, &raw_gps, sizeof(raw_gps));

    //MSP_COMP_GPS
    comp_gps.distanceToHome = 50;
    comp_gps.directionToHome = heading;
    msp.send(MSP_COMP_GPS, &comp_gps, sizeof(comp_gps));

    //MSP_ATTITUDE
    attitude.pitch = pitch_angle;
    attitude.roll = roll_angle;
    //attitude.yaw = heading;
    msp.send(MSP_ATTITUDE, &attitude, sizeof(attitude));

    //MSP_ALTITUDE
    altitude.estimatedActualPosition = relative_alt / 10; //cm
    altitude.estimatedActualVelocity = (int16_t)(climb_rate * 100); //m/s to cm/s
    msp.send(MSP_ALTITUDE, &altitude, sizeof(altitude));

    //

#ifdef DISPLAY_THROTTLE_POSITION
    //MSP_PID_CONFIG
    pid.roll[0] = thr_position;
    msp.send(MSP_PID, &pid, sizeof(pid));
#endif

    //MSP_OSD_CONFIG
    send_osd_config();
}

msp_osd_config_t msp_osd_config = {0};

void send_osd_config()
{

#ifdef IMPERIAL_UNITS
    msp_osd_config.units = 0;
#else
    msp_osd_config.units = 1;
#endif

    msp_osd_config.osd_item_count = 56;
    msp_osd_config.osd_stat_count = 24;
    msp_osd_config.osd_timer_count = 2;
    msp_osd_config.osd_warning_count = 16;              // 16
    msp_osd_config.osd_profile_count = 1;              // 1
    msp_osd_config.osdprofileindex = 1;                // 1
    msp_osd_config.overlay_radio_mode = 0;             //  0

    msp_osd_config.osd_rssi_value_pos = osd_rssi_value_pos;
    msp_osd_config.osd_main_batt_voltage_pos = osd_main_batt_voltage_pos;
    msp_osd_config.osd_crosshairs_pos = osd_crosshairs_pos;
    msp_osd_config.osd_artificial_horizon_pos = osd_artificial_horizon_pos;
    msp_osd_config.osd_horizon_sidebars_pos = osd_horizon_sidebars_pos;
    msp_osd_config.osd_item_timer_1_pos = osd_item_timer_1_pos;
    msp_osd_config.osd_item_timer_2_pos = osd_item_timer_2_pos;
    msp_osd_config.osd_flymode_pos = osd_flymode_pos;
    msp_osd_config.osd_craft_name_pos = osd_craft_name_pos;
    msp_osd_config.osd_throttle_pos_pos = osd_throttle_pos_pos;
    msp_osd_config.osd_vtx_channel_pos = osd_vtx_channel_pos;
    msp_osd_config.osd_current_draw_pos = osd_current_draw_pos;
    msp_osd_config.osd_mah_drawn_pos = osd_mah_drawn_pos;
    msp_osd_config.osd_gps_speed_pos = osd_gps_speed_pos;
    msp_osd_config.osd_gps_sats_pos = osd_gps_sats_pos;
    msp_osd_config.osd_altitude_pos = osd_altitude_pos;
    msp_osd_config.osd_roll_pids_pos = osd_roll_pids_pos;
    msp_osd_config.osd_pitch_pids_pos = osd_pitch_pids_pos;
    msp_osd_config.osd_yaw_pids_pos = osd_yaw_pids_pos;
    msp_osd_config.osd_power_pos = osd_power_pos;
    msp_osd_config.osd_pidrate_profile_pos = osd_pidrate_profile_pos;
    msp_osd_config.osd_warnings_pos = osd_warnings_pos;
    msp_osd_config.osd_avg_cell_voltage_pos = osd_avg_cell_voltage_pos;
    msp_osd_config.osd_gps_lon_pos = osd_gps_lon_pos;
    msp_osd_config.osd_gps_lat_pos = osd_gps_lat_pos;
    msp_osd_config.osd_debug_pos = osd_debug_pos;
    msp_osd_config.osd_pitch_angle_pos = osd_pitch_angle_pos;
    msp_osd_config.osd_roll_angle_pos = osd_roll_angle_pos;
    msp_osd_config.osd_main_batt_usage_pos = osd_main_batt_usage_pos;
    msp_osd_config.osd_disarmed_pos = osd_disarmed_pos;
    msp_osd_config.osd_home_dir_pos = osd_home_dir_pos;
    msp_osd_config.osd_home_dist_pos = osd_home_dist_pos;
    msp_osd_config.osd_numerical_heading_pos = osd_numerical_heading_pos;
    msp_osd_config.osd_numerical_vario_pos = osd_numerical_vario_pos;
    msp_osd_config.osd_compass_bar_pos = osd_compass_bar_pos;
    msp_osd_config.osd_esc_tmp_pos = osd_esc_tmp_pos;
    msp_osd_config.osd_esc_rpm_pos = osd_esc_rpm_pos;
    msp_osd_config.osd_remaining_time_estimate_pos = osd_remaining_time_estimate_pos;
    msp_osd_config.osd_rtc_datetime_pos = osd_rtc_datetime_pos;
    msp_osd_config.osd_adjustment_range_pos = osd_adjustment_range_pos;
    msp_osd_config.osd_core_temperature_pos = osd_core_temperature_pos;
    msp_osd_config.osd_anti_gravity_pos = osd_anti_gravity_pos;
    msp_osd_config.osd_g_force_pos = osd_g_force_pos;
    msp_osd_config.osd_motor_diag_pos = osd_motor_diag_pos;
    msp_osd_config.osd_log_status_pos = osd_log_status_pos;
    msp_osd_config.osd_flip_arrow_pos = osd_flip_arrow_pos;
    msp_osd_config.osd_link_quality_pos = osd_link_quality_pos;
    msp_osd_config.osd_flight_dist_pos = osd_flight_dist_pos;
    msp_osd_config.osd_stick_overlay_left_pos = osd_stick_overlay_left_pos;
    msp_osd_config.osd_stick_overlay_right_pos = osd_stick_overlay_right_pos;
    msp_osd_config.osd_display_name_pos = osd_display_name_pos;
    msp_osd_config.osd_esc_rpm_freq_pos = osd_esc_rpm_freq_pos;
    msp_osd_config.osd_rate_profile_name_pos = osd_rate_profile_name_pos;
    msp_osd_config.osd_pid_profile_name_pos = osd_pid_profile_name_pos;
    msp_osd_config.osd_profile_name_pos = osd_profile_name_pos;
    msp_osd_config.osd_rssi_dbm_value_pos = osd_rssi_dbm_value_pos;
    msp_osd_config.osd_rc_channels_pos = osd_rc_channels_pos;

    msp.send(MSP_OSD_CONFIG, &msp_osd_config, sizeof(msp_osd_config));
}

void invert_pos(uint16_t *pos1, uint16_t *pos2)
{
    uint16_t tmp_pos = *pos1;
    *pos1 = *pos2;
    *pos2 = tmp_pos;
}

void display_flight_mode()
{

    #if VEHICLE_TYPE == 0
    char txt[15];
    strcpy_P(txt, arduPlaneModeStr[custom_mode]);
    show_text(&txt);

    #elif VEHICLE_TYPE == 1
    char txt[15];
    strcpy_P(txt, arduCopterModeStr[custom_mode]);
    show_text(&txt);

    #elif VEHICLE_TYPE == 2
    char txt[15];
    strcpy_P(txt, inavPlaneModeStr[custom_mode]);
    show_text(&txt);

    #elif VEHICLE_TYPE == 3
    char txt[15];
    strcpy_P(txt, inavCopterModeStr[custom_mode]);
    show_text(&txt);

    #endif

}


//from here on code from Betaflight github https://github.com/betaflight/betaflight/blob/c8b5edb415c33916c91a7ccc8bd19c7276540cd1/src/main/io/gps.c

float GPS_scaleLonDown = 1.0f;  // this is used to offset the shrinking longitude as we go towards the poles

void GPS_calc_longitude_scaling(int32_t lat)
{
    float rads = (fabsf((float)lat) / 10000000.0f) * 0.0174532925f;
    GPS_scaleLonDown = cos_approx(rads);
}

#define DISTANCE_BETWEEN_TWO_LONGITUDE_POINTS_AT_EQUATOR_IN_HUNDREDS_OF_KILOMETERS 1.113195f
#define TAN_89_99_DEGREES 5729.57795f
// Get distance between two points in cm
// Get bearing from pos1 to pos2, returns an 1deg = 100 precision
void GPS_distance_cm_bearing(int32_t *currentLat1, int32_t *currentLon1, int32_t *destinationLat2, int32_t *destinationLon2, uint32_t *dist, int32_t *bearing)
{
    float dLat = *destinationLat2 - *currentLat1; // difference of latitude in 1/10 000 000 degrees
    float dLon = (float)(*destinationLon2 - *currentLon1) * GPS_scaleLonDown;
    *dist = sqrtf(sq(dLat) + sq(dLon)) * DISTANCE_BETWEEN_TWO_LONGITUDE_POINTS_AT_EQUATOR_IN_HUNDREDS_OF_KILOMETERS;

    *bearing = 9000.0f + atan2f(-dLat, dLon) * TAN_89_99_DEGREES;      // Convert the output radians to 100xdeg
    if (*bearing < 0)
        *bearing += 36000;
}

void GPS_calculateDistanceAndDirectionToHome(void)
{
     if (gps_home_lat != 0 && gps_home_lon != 0) {      // If we don't have home set, do not display anything
        uint32_t dist;
        int32_t dir;
        GPS_distance_cm_bearing(&gps_lat, &gps_lon, &gps_home_lat, &gps_home_lon, &dist, &dir);
        distanceToHome = dist / 100;
        directionToHome = dir / 100;
     } else {
         distanceToHome = 0;
         directionToHome = 0;
     }
}

#define M_PIf       3.14159265358979323846f
#define sinPolyCoef3 -1.666568107e-1f
#define sinPolyCoef5  8.312366210e-3f
#define sinPolyCoef7 -1.849218155e-4f
#define sinPolyCoef9  0

float sin_approx(float x)
{
    int32_t xint = x;
    if (xint < -32 || xint > 32) return 0.0f;                               // Stop here on error input (5 * 360 Deg)
    while (x >  M_PIf) x -= (2.0f * M_PIf);                                 // always wrap input angle to -PI..PI
    while (x < -M_PIf) x += (2.0f * M_PIf);
    if (x >  (0.5f * M_PIf)) x =  (0.5f * M_PIf) - (x - (0.5f * M_PIf));   // We just pick -90..+90 Degree
    else if (x < -(0.5f * M_PIf)) x = -(0.5f * M_PIf) - ((0.5f * M_PIf) + x);
    float x2 = x * x;
    return x + x * x2 * (sinPolyCoef3 + x2 * (sinPolyCoef5 + x2 * (sinPolyCoef7 + x2 * sinPolyCoef9)));
}

float cos_approx(float x)
{
    return sin_approx(x + (0.5f * M_PIf));
}
