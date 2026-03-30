#ifndef GV_H
#define GV_H

// SPIFFS Storage Paths
#define WIFI_CONFIG_FILE_NAME "/wifi_config.txt"
#define POS_SETTING_FILE_NAME "/pos_setting.txt"
    
// Add these declarations so other .cpp files can use them
String encode_b64(String input);
String decode_b64(String input);

// Display/System Constants
#define SCR_W 480
#define SCR_H 800

#endif