#include "wifi_helper.h"
#include <WiFi.h>
#include <Preferences.h>
#include <lvgl.h>

// Initialize NVS storage object
Preferences wifiPrefs;

// External variables so your main screen knows what we are connected to
extern String current_ssid;
extern String current_wifi_pass;

/* =========================================================
   1. NVS SAVE LOGIC (Runs only when password is correct)
   ========================================================= */
void save_wifi_to_nvs(String ssid, String pass) {
    wifiPrefs.begin("wifi-store", false); // false = Read/Write mode
    wifiPrefs.putString("ssid", ssid);
    wifiPrefs.putString("pass", pass);
    wifiPrefs.end();
    Serial.println("💾 WiFi Credentials Saved to NVS Memory.");
}

/* =========================================================
   2. AUTO-CONNECT LOGIC (Runs once during Boot-up)
   ========================================================= */
void wifi_auto_reconnect_logic() {
    wifiPrefs.begin("wifi-store", true); // true = Read-Only mode
    String savedSSID = wifiPrefs.getString("ssid", "");
    String savedPASS = wifiPrefs.getString("pass", "");
    wifiPrefs.end();

    if (savedSSID != "") {
        Serial.printf("🔄 NVS found saved WiFi: %s. Auto-connecting...\n", savedSSID.c_str());
        
        WiFi.mode(WIFI_STA);
        
        // CRITICAL: Tells the ESP32 chip to handle dropped connections automatically
        WiFi.setAutoConnect(true);
        WiFi.setAutoReconnect(true); 
        
        WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
        
        // Sync global variables for your UI
        current_ssid = savedSSID;
        current_wifi_pass = savedPASS;
    } else {
        Serial.println("ℹ️ No saved WiFi found in NVS. User must connect manually.");
    }
}

/* =========================================================
   3. MANUAL CONNECT LOGIC (Runs when user types password)
   ========================================================= */
void attempt_connect(String ssid, String pass) {
    if(ssid == "") return;
    
    Serial.printf("[WIFI] Manual connection attempt to: %s\n", ssid.c_str());
    
    // Start fresh
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true); // Ensure it stays connected if router reboots
    WiFi.begin(ssid.c_str(), pass.c_str());

    // Show "Connecting" Popup on Screen
    lv_obj_t * mbox = lv_msgbox_create(lv_layer_top(), "Connecting", ssid.c_str(), NULL, false);
    lv_obj_center(mbox);
    
    int attempts = 0;
    // Wait up to 8 seconds (40 attempts * 200ms) for router to accept password
    while(WiFi.status() != WL_CONNECTED && attempts++ < 40) { 
        delay(200); 
        lv_timer_handler(); // Keeps the screen from freezing
    }
    
    // Close the "Connecting" popup
    lv_msgbox_close(mbox);

    // --- THE MOMENT OF TRUTH ---
    if(WiFi.status() == WL_CONNECTED) {
        Serial.println("✅ Connected to WiFi Successfully!");
        Serial.printf("🌐 IP Address: %s\n", WiFi.localIP().toString().c_str());
        
        // 👉 THE MAGIC LINE: Save to NVS only because the connection worked!
        save_wifi_to_nvs(ssid, pass); 
        
        // Sync global variables
        current_ssid = ssid;
        current_wifi_pass = pass;

        // Show Success Popup
        lv_obj_t * m_succ = lv_msgbox_create(lv_layer_top(), "Success", "WiFi Connected!", NULL, true);
        lv_obj_center(m_succ);
        
    } else {
        Serial.println("❌ Connection Failed");
        
        // Show Error Popup
        lv_obj_t * m_err = lv_msgbox_create(lv_layer_top(), "Error", "Incorrect Password or Timeout", NULL, true);
        lv_obj_center(m_err);
    }
}