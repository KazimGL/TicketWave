/*
 * Smart Service Kiosk — PURE LVGL v8 Edition (Vertical Stack)
 * ESP32-S3 + LovyanGFX driver + LVGL 8.3
 * Portrait: 480 x 800
 *
 * Settings screen upgraded to List Menu with full WiFi Scan/Connect flow.
 * Production Upgrades Applied: Async WiFi, Auto-Reconnect, WDT, SPIFFS Obfuscation, Memory Audit
 */

#include "gfx_conf.h"  // LovyanGFX LGFX tf t object
#include <lvgl.h>
#include "qrcode.h"  // ricmoo QR library (C)
#include <WiFi.h>    // To check WiFi Status
#include <SPIFFS.h>  // To save/load settings permanently
#include <FS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "api.hpp"
#include "pin_manager.hpp"
#include "printer_manager.hpp"
#include "pos_manager.hpp"
#include "wifi_helper.h"
#include "pin_manager.hpp"
#include "printer_manager.hpp"
#include "pos_manager.hpp"
#include "wifi_helper.h"
#include "web_manager.hpp"  //

//Required headers for hardware Watchdog Timer and Base64 Obfuscation
#include <esp_task_wdt.h>
#include <mbedtls/base64.h>

// ===============================================================
// 0. CONFIGURATION FILE PATHS & VARIABLES
// ===============================================================
#define WIFI_CONFIG_FILE_NAME "/wifi_config.txt"
#define POS_SETTING_FILE_NAME "/pos_setting.txt"

String current_ssid = "";
String current_wifi_pass = "";
String current_pos_name = "";
String current_api_key = "";
String current_service_type = "General";
String current_receipt_details = "";

LV_IMG_DECLARE(img_logo);
LV_IMG_DECLARE(img_ticket);
LV_IMG_DECLARE(img_parking);
LV_IMG_DECLARE(img_event);
LV_IMG_DECLARE(img_donate);

lv_obj_t* make_content_area(lv_obj_t* parent);
// ===============================================================
// 1. DATA STRUCTURES
// ===============================================================
struct Option {
  const char* label;
  uint32_t price;
  int32_t qty;
  bool selected;
  lv_obj_t* qty_label;
};

// --- FORWARD DECLARATIONS ---
static void recalc_total(Option* opts, int count, bool isFixed);
void load_selection(const char* title, Option* opts, int count, bool isFixed);
void load_home();
void load_payment();
void load_success();
void load_failure();
void load_settings();
void load_wifi_scan();
void load_wifi_password(const char* ssid);
void load_wifi_result(bool success);
static void stop_all_timers();
static void nav_home_cb(lv_event_t* e);
void save_config_to_spiffs();
void load_config_from_spiffs();
void connect_wifi_async(String ssid, String pass);
void load_printer_settings();
void load_printer_scan();
void load_printer_manual();
void load_pos_settings();
void start_ble_scan();
const char* get_ble_scan_results();
void save_printer_mac(String new_mac);

// ===============================================================
// 2. CONSTANTS & MACROS
// ===============================================================
#define SCR_W 480
#define SCR_H 800
#define LV_BUF_LINES 20

#define C_BG lv_color_make(248, 250, 252)
#define C_CARD lv_color_make(255, 255, 255)
#define C_ACCENT lv_color_make(13, 148, 136)
#define C_TEXT lv_color_make(51, 65, 85)
#define C_MUTED lv_color_make(148, 163, 184)
#define C_BORDER lv_color_make(226, 232, 240)
#define C_SUCCESS lv_color_make(16, 185, 129)
#define C_ERROR lv_color_make(244, 63, 94)
#define C_WHITE lv_color_white()

// --- POS Price Management Globals ---
#define MAX_PRICE_TAS 12
static lv_obj_t* price_tas[MAX_PRICE_TAS];
static int price_ta_count = 0;
static Option* price_ta_opts[MAX_PRICE_TAS];
static lv_obj_t* pos_kb = nullptr;  // This fixes the 'not declared' error

// --- Printer UI State Variables ---
static lv_obj_t* scr_prn_settings = nullptr;
static lv_obj_t* scr_prn_scan = nullptr;
static lv_obj_t* scr_prn_manual = nullptr;

static lv_timer_t* prn_scan_timer = nullptr;
static lv_obj_t* cont_prn_scan = nullptr;
static lv_obj_t* prn_roller = nullptr;
static lv_obj_t* ta_prn_mac = nullptr;
static lv_obj_t* prn_kb = nullptr;

static int prn_scan_count = 0;
static String roller_prn_str = "";
static char prn_selected_mac[32] = "";

// Custom Number Pad Map (Ensures the '0' key is present)
static const char* pos_kb_map[] = {
  "1", "2", "3", "\n",
  "4", "5", "6", "\n",
  "7", "8", "9", "\n",
  "Back", "0", LV_SYMBOL_OK, ""
};

static const lv_btnmatrix_ctrl_t pos_kb_ctrl[] = {
  1, 1, 1,
  1, 1, 1,
  1, 1, 1,
  2, 1, 2
};

// Add this to your screen pointer list if not already there
static lv_obj_t* scr_pos_settings = nullptr;
// ===============================================================
// 3. GLOBAL VARIABLES & STATE
// ===============================================================
static lv_disp_draw_buf_t draw_buf;
static lv_color_t* buf1;
static lv_color_t* buf2;

static lv_obj_t* scr_home = nullptr;
static lv_obj_t* scr_select = nullptr;
static lv_obj_t* scr_payment = nullptr;
static lv_obj_t* scr_success = nullptr;
static lv_obj_t* scr_failure = nullptr;
static lv_obj_t* scr_settings = nullptr;

// WiFi-flow screens
static lv_obj_t* scr_wifi_scan = nullptr;
static lv_obj_t* scr_wifi_pass = nullptr;
static lv_obj_t* scr_wifi_result = nullptr;

// NEW: Asynchronous Timers and UI Pointers for strict memory management
static lv_timer_t* pay_countdown = nullptr;
static lv_timer_t* suc_countdown = nullptr;
static lv_timer_t* wifi_scan_timer = nullptr;
static lv_timer_t* wifi_conn_timer = nullptr;
static lv_timer_t* auto_recon_timer = nullptr;

static uint32_t pay_secs_left = 300;
static uint32_t suc_secs_left = 10;
static lv_obj_t* lbl_pay_timer = nullptr;
static lv_obj_t* lbl_suc_timer = nullptr;

// UI Pointers required for async updates across screens
static lv_obj_t* global_header_dot = nullptr;
static lv_obj_t* global_header_lbl = nullptr;
static lv_obj_t* spin_card = nullptr;
static lv_obj_t* cont_wifi_scan = nullptr;
static lv_obj_t* conn_msgbox = nullptr;

// Near the top of kiosk_main.ino kaizz
extern String current_api_key;
extern String current_pos_name;
String current_active_txn_id = "";  // To track the current payment

// Change these lines in your global variables section
Option ticketOpts[] = {
  { "Adult", 0, 0, false, NULL },
  { "Child", 0, 0, false, NULL },
  { "Senior", 0, 0, false, NULL }
};

Option parkingOpts[] = {
  { "Standard Parking", 0, 0, false, NULL }
};

Option eventOpts[] = {
  { "General Entry", 0, 0, false, NULL },
  { "VIP Access", 0, 0, false, NULL }
};

// Donation options usually stay fixed, but you can change them if you like
Option donationOpts[] = { { "Rs. 50", 50, 0, false, NULL }, { "Rs. 100", 100, 0, false, NULL }, { "Rs. 200", 200, 0, false, NULL }, { "Rs. 500", 500, 0, false, NULL } };

const int TICKET_OPTS = 3, PARKING_OPTS = 1, EVENT_OPTS = 2, DONATION_OPTS = 4;

// Add this near 'uint32_t totalAmount = 0;'
int current_bonrix_txn_id = 0;

uint32_t totalAmount = 0;
uint32_t paymentAmount = 0;
char orderId[32] = "";
uint8_t payMethod = 0;
static int active_lang = 0;

// WiFi scan state
static char wifi_selected_ssid[64] = "";
static String temp_wifi_pass = "";
static lv_obj_t* wifi_roller = nullptr;
static lv_obj_t* ta_wifi_pass = nullptr;
static lv_obj_t* wifi_kb = nullptr;

void resetAllOptions() {
  for (int i = 0; i < TICKET_OPTS; i++) {
    ticketOpts[i].qty = 0;
    ticketOpts[i].selected = false;
  }
  for (int i = 0; i < PARKING_OPTS; i++) {
    parkingOpts[i].qty = 0;
    parkingOpts[i].selected = false;
  }
  for (int i = 0; i < EVENT_OPTS; i++) {
    eventOpts[i].qty = 0;
    eventOpts[i].selected = false;
  }
  for (int i = 0; i < DONATION_OPTS; i++) {
    donationOpts[i].qty = 0;
    donationOpts[i].selected = false;
  }
  totalAmount = 0;
  payMethod = 0;
}

void generateOrderId() {
  // Adds a 4-digit random number to the timestamp for a safer ID
  // Result looks like: TXN_5421_8832
  snprintf(orderId, sizeof(orderId), "TXN_%lu_%04d", millis(), random(1000, 9999));

  Serial.print("New Order ID Generated: ");
  Serial.println(orderId);
}

// ===============================================================
// 4. SPIFFS & WIFI CONFIGURATION LOGIC (NEW: OBFUSCATION)
// ===============================================================

// NEW: Base64 Encoder/Decoder for SPIFFS security
String encode_b64(String input) {
  if (input.length() == 0) return "";
  unsigned char output[256] = { 0 };
  size_t olen = 0;
  mbedtls_base64_encode(output, 256, &olen, (const unsigned char*)input.c_str(), input.length());
  return String((char*)output);
}

String decode_b64(String input) {
  if (input.length() == 0) return "";
  unsigned char output[256] = { 0 };
  size_t olen = 0;
  mbedtls_base64_decode(output, 256, &olen, (const unsigned char*)input.c_str(), input.length());
  return String((char*)output);
}

void load_config_from_spiffs() {
  if (SPIFFS.exists(WIFI_CONFIG_FILE_NAME)) {
    File file = SPIFFS.open(WIFI_CONFIG_FILE_NAME, FILE_READ);
    if (file) {
      current_ssid = decode_b64(file.readStringUntil('\n'));  // FIXED: Decode Base64 on read
      current_wifi_pass = decode_b64(file.readStringUntil('\n'));
      current_ssid.trim();
      current_wifi_pass.trim();
      file.close();
    }
  }
  if (SPIFFS.exists(POS_SETTING_FILE_NAME)) {
    File file = SPIFFS.open(POS_SETTING_FILE_NAME, FILE_READ);
    if (file) {
      current_pos_name = decode_b64(file.readStringUntil('\n'));
      current_api_key = decode_b64(file.readStringUntil('\n'));
      current_pos_name.trim();
      current_api_key.trim();
      file.close();
    }
  }
  Serial.println("Loaded Config - SSID: " + current_ssid + " | POS: " + current_pos_name);
}

void save_config_to_spiffs() {
  File pFile = SPIFFS.open(POS_SETTING_FILE_NAME, FILE_WRITE);
  if (pFile) {
    // CRITICAL: Trim the strings BEFORE encoding.
    // This removes accidental spaces from the touch keyboard.
    current_pos_name.trim();
    current_api_key.trim();
    current_ssid.trim();
    current_wifi_pass.trim();

    // Line 1: POS Name
    pFile.print(encode_b64(current_pos_name) + "\n");
    // Line 2: API Key
    pFile.print(encode_b64(current_api_key) + "\n");
    // Line 3: WiFi SSID
    pFile.print(encode_b64(current_ssid) + "\n");
    // Line 4: WiFi Password
    pFile.print(encode_b64(current_wifi_pass) + "\n");

    pFile.close();
    Serial.println("✅ Settings saved and encoded to SPIFFS.");
  } else {
    Serial.println("❌ Failed to open settings file for writing.");
  }
}

// FIXED: Converted to strictly non-blocking.
void connect_wifi_async(String ssid, String pass) {
  if (ssid == "" || ssid == "Not configured") return;

  Serial.println("🌐 Auto-Connecting to: " + ssid);

  WiFi.mode(WIFI_STA);

  // --- THE FIX: Hardware Level Auto-Reconnect ---
  WiFi.setAutoConnect(true);    // Connect to last known AP on power up
  WiFi.setAutoReconnect(true);  // Automatically reconnect if dropped

  WiFi.begin(ssid.c_str(), pass.c_str());
}

static void auto_recon_cb(lv_timer_t* t) {
  wl_status_t current = WiFi.status();

  if (current != WL_CONNECTED && current_ssid != "" && current_ssid != "Not configured") {
    Serial.println("🔄 WiFi Lost. Retrying...");
    // If reconnect fails, begin again with saved credentials
    WiFi.begin(current_ssid.c_str(), current_wifi_pass.c_str());
  }

  // Update the UI Online/Offline dot
  if (global_header_dot && lv_obj_is_valid(global_header_dot)) {
    if (current == WL_CONNECTED) {
      lv_obj_set_style_bg_color(global_header_dot, C_SUCCESS, 0);
      lv_label_set_text(global_header_lbl, "Online");
    } else {
      lv_obj_set_style_bg_color(global_header_dot, C_ERROR, 0);
      lv_label_set_text(global_header_lbl, "Offline");
    }
  }
}
void lv_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
  tft.pushImage(area->x1, area->y1, w, h, (lgfx::rgb565_t*)px);  // Use tft here
  lv_disp_flush_ready(drv);
}

void lv_touch_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
  uint16_t tx, ty;
  if (tft.getTouch(&tx, &ty)) {  // Use tft here
    data->point.x = tx;
    data->point.y = ty;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}
// ===============================================================
// 6. THEME CONFIGURATION
// ===============================================================
// [Theme configuration remains identical...]
struct KioskTheme {
  lv_style_t screen;
  lv_style_t card;
  lv_style_t btn_primary;
  lv_style_t btn_outline;
  lv_style_t btn_danger;
  lv_style_t btn_success;
  lv_style_t label_title;
  lv_style_t label_body;
  lv_style_t label_muted;
  lv_style_t header_bar;
  lv_style_t footer_bar;
  lv_style_t tag_active;
  lv_style_t tag_inactive;
} theme;

void init_theme() {
  lv_style_init(&theme.screen);
  lv_style_set_bg_color(&theme.screen, C_BG);
  lv_style_set_pad_all(&theme.screen, 0);

  lv_style_init(&theme.card);
  lv_style_set_bg_color(&theme.card, C_CARD);
  lv_style_set_border_color(&theme.card, C_BORDER);
  lv_style_set_border_width(&theme.card, 1);
  lv_style_set_radius(&theme.card, 14);
  lv_style_set_shadow_color(&theme.card, lv_color_make(180, 190, 210));
  lv_style_set_shadow_width(&theme.card, 8);
  lv_style_set_shadow_ofs_y(&theme.card, 4);
  lv_style_set_pad_all(&theme.card, 16);

  lv_style_init(&theme.btn_primary);
  lv_style_set_bg_color(&theme.btn_primary, C_ACCENT);
  lv_style_set_text_color(&theme.btn_primary, C_WHITE);
  lv_style_set_radius(&theme.btn_primary, 10);
  lv_style_set_shadow_width(&theme.btn_primary, 4);
  lv_style_set_shadow_color(&theme.btn_primary, lv_color_make(13, 100, 90));
  lv_style_set_shadow_ofs_y(&theme.btn_primary, 2);

  lv_style_init(&theme.btn_outline);
  lv_style_set_bg_color(&theme.btn_outline, C_CARD);
  lv_style_set_text_color(&theme.btn_outline, C_TEXT);
  lv_style_set_border_color(&theme.btn_outline, C_BORDER);
  lv_style_set_border_width(&theme.btn_outline, 2);
  lv_style_set_radius(&theme.btn_outline, 10);

  lv_style_init(&theme.btn_danger);
  lv_style_set_bg_color(&theme.btn_danger, C_ERROR);
  lv_style_set_text_color(&theme.btn_danger, C_WHITE);
  lv_style_set_radius(&theme.btn_danger, 10);

  lv_style_init(&theme.btn_success);
  lv_style_set_bg_color(&theme.btn_success, C_SUCCESS);
  lv_style_set_text_color(&theme.btn_success, C_WHITE);
  lv_style_set_radius(&theme.btn_success, 10);

  lv_style_init(&theme.label_title);
  lv_style_set_text_color(&theme.label_title, C_TEXT);
  lv_style_set_text_font(&theme.label_title, &lv_font_montserrat_20);

  lv_style_init(&theme.label_body);
  lv_style_set_text_color(&theme.label_body, C_TEXT);
  lv_style_set_text_font(&theme.label_body, &lv_font_montserrat_16);

  lv_style_init(&theme.label_muted);
  lv_style_set_text_color(&theme.label_muted, C_MUTED);
  lv_style_set_text_font(&theme.label_muted, &lv_font_montserrat_14);

  lv_style_init(&theme.header_bar);
  lv_style_set_bg_color(&theme.header_bar, C_CARD);
  lv_style_set_border_width(&theme.header_bar, 0);
  lv_style_set_shadow_color(&theme.header_bar, lv_color_make(200, 210, 220));
  lv_style_set_shadow_width(&theme.header_bar, 6);
  lv_style_set_shadow_ofs_y(&theme.header_bar, 3);
  lv_style_set_pad_hor(&theme.header_bar, 16);

  lv_style_init(&theme.footer_bar);
  lv_style_set_bg_color(&theme.footer_bar, C_CARD);
  lv_style_set_border_color(&theme.footer_bar, C_BORDER);
  lv_style_set_border_width(&theme.footer_bar, 1);
  lv_style_set_border_side(&theme.footer_bar, LV_BORDER_SIDE_TOP);
  lv_style_set_pad_hor(&theme.footer_bar, 16);

  lv_style_init(&theme.tag_active);
  lv_style_set_bg_color(&theme.tag_active, C_ACCENT);
  lv_style_set_text_color(&theme.tag_active, C_WHITE);
  lv_style_set_radius(&theme.tag_active, 8);
  lv_style_set_border_width(&theme.tag_active, 0);

  lv_style_init(&theme.tag_inactive);
  lv_style_set_bg_color(&theme.tag_inactive, C_CARD);
  lv_style_set_text_color(&theme.tag_inactive, C_TEXT);
  lv_style_set_border_color(&theme.tag_inactive, C_BORDER);
  lv_style_set_border_width(&theme.tag_inactive, 1);
  lv_style_set_radius(&theme.tag_inactive, 8);
}
// ===============================================================
// 7. UI BUILDER HELPERS
// ===============================================================

static void on_settings_clicked(lv_event_t* e);
static void on_wifi_info_cb(lv_event_t* e);
static void on_info_btn_cb(lv_event_t* e);
static void nav_home_cb(lv_event_t* e);
static void stop_all_timers();

// Callback for the User Instructions Popup
static void on_info_btn_cb(lv_event_t* e) {
  static const char* btns[] = { "Got it!", "" };
  const char* info_text =
    "1. SELECT: Choose a service from the home screen.\n"
    "2. QUANTITY: Add your items and tap Proceed.\n"
    "3. PAYMENT: Scan the UPI QR code with any app.\n"
    "4. TICKET: Collect your printed receipt below.\n\n"
    "Thank you for using TicketWave!";

  lv_obj_t* mb = lv_msgbox_create(NULL, "How it works", info_text, btns, true);
  lv_obj_center(mb);

  // FIXED: Point directly to the font instead of trying to access style members
  lv_obj_set_style_text_font(mb, &lv_font_montserrat_16, 0);
  lv_obj_set_width(mb, 400);
}

static void on_settings_clicked(lv_event_t* e);

lv_obj_t* make_screen() {
  lv_obj_t* s = lv_obj_create(NULL);
  lv_obj_add_style(s, &theme.screen, 0);
  return s;
}

static void on_wifi_info_cb(lv_event_t* e) {
  String ip = WiFi.localIP().toString();
  String msg;

  if (WiFi.status() == WL_CONNECTED) {
    msg = "Network: " + current_ssid + "\n\nIP Address: " + ip + "\n\nType this IP address into your phone's browser to view or recover your PIN.";
  } else {
    msg = "Kiosk is currently offline.\nPlease connect to WiFi in Settings.";
  }

  lv_obj_t* mb = lv_msgbox_create(NULL, "Network Info", msg.c_str(), NULL, true);
  lv_obj_center(mb);
}

void make_header(lv_obj_t* parent, const char* title, const char* subtitle) {
  lv_obj_t* header = lv_obj_create(parent);
  lv_obj_add_style(header, &theme.header_bar, 0);
  lv_obj_set_size(header, SCR_W, 72);
  lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

  // Main Title & Subtitle logic remains same...
  lv_obj_t* t = lv_label_create(header);
  lv_label_set_text(t, title);
  lv_obj_add_style(t, &theme.label_title, 0);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 16, -10);

  lv_obj_t* sub = lv_label_create(header);
  lv_label_set_text(sub, subtitle ? subtitle : "Self-Service Terminal");
  lv_obj_add_style(sub, &theme.label_muted, 0);
  lv_obj_align(sub, LV_ALIGN_LEFT_MID, 16, 12);

  // --- FIXED: SETTINGS BUTTON SIZE ---
  lv_obj_t* btn_set = lv_btn_create(header);
  lv_obj_set_size(btn_set, 60, 60);  // Bigger hit area
  lv_obj_align(btn_set, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_opa(btn_set, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(btn_set, 0, 0);
  lv_obj_add_event_cb(btn_set, on_settings_clicked, LV_EVENT_CLICKED, NULL);

  // FIXED: Way Larger Symbol
  lv_obj_t* lbl_set = lv_label_create(btn_set);
  lv_label_set_text(lbl_set, LV_SYMBOL_SETTINGS);
  lv_obj_set_style_text_font(lbl_set, &lv_font_montserrat_30, 0);  // <--- BUMPED TO 32
  lv_obj_set_style_text_color(lbl_set, C_ACCENT, 0);
  lv_obj_center(lbl_set);

  // WIFI AREA - Shifted slightly left to make room for bigger gear
  lv_obj_t* wifi_btn = lv_btn_create(header);
  lv_obj_set_size(wifi_btn, 130, 50);
  lv_obj_align(wifi_btn, LV_ALIGN_RIGHT_MID, -60, 0);
  lv_obj_set_style_bg_opa(wifi_btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(wifi_btn, 0, 0);
  lv_obj_add_event_cb(wifi_btn, on_wifi_info_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* lbl_on = lv_label_create(wifi_btn);
  lv_obj_add_style(lbl_on, &theme.label_muted, 0);
  lv_obj_align(lbl_on, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t* dot = lv_obj_create(wifi_btn);
  lv_obj_set_size(dot, 10, 10);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_align(dot, LV_ALIGN_RIGHT_MID, -60, 0);

  if (WiFi.status() == WL_CONNECTED) {
    lv_obj_set_style_bg_color(dot, C_SUCCESS, 0);
    lv_label_set_text(lbl_on, "Online");
  } else {
    lv_obj_set_style_bg_color(dot, C_ERROR, 0);
    lv_label_set_text(lbl_on, "Offline");
  }
}

void make_footer(lv_obj_t* parent, bool show_back) {
  lv_obj_t* bar = lv_obj_create(parent);
  lv_obj_add_style(bar, &theme.footer_bar, 0);
  lv_obj_set_size(bar, SCR_W, 60);
  lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  if (show_back) {
    lv_obj_t* btn_back = lv_btn_create(bar);
    lv_obj_add_style(btn_back, &theme.btn_outline, 0);
    lv_obj_set_size(btn_back, 100, 40);
    lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_add_event_cb(btn_back, nav_home_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* l_back = lv_label_create(btn_back);
    lv_label_set_text(l_back, "< Back");
    lv_obj_center(l_back);
  }

  lv_obj_t* btn_info = lv_btn_create(bar);
  lv_obj_add_style(btn_info, &theme.btn_outline, 0);
  lv_obj_set_size(btn_info, 180, 40);
  lv_obj_align(btn_info, LV_ALIGN_CENTER, 0, 0);
  lv_obj_add_event_cb(btn_info, on_info_btn_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t* lbl_info = lv_label_create(btn_info);
  lv_label_set_text(lbl_info, LV_SYMBOL_LIST " How it works");
  lv_obj_center(lbl_info);
}

// FIXED: Added missing make_content_area function
lv_obj_t* make_content_area(lv_obj_t* parent) {
  lv_obj_t* cont = lv_obj_create(parent);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_hor(cont, 0, 0);
  lv_obj_set_style_pad_ver(cont, 16, 0);
  lv_obj_set_size(cont, SCR_W, SCR_H - 72 - 60);
  lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 72);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(cont, 16, 0);
  return cont;
}

static void nav_home_cb(lv_event_t* e) {
  load_home();
}

static void open_settings_wrapper() {
  load_settings();
}

static void on_settings_clicked(lv_event_t* e) {
  if (has_pin()) {
    show_pin_auth_dialog(open_settings_wrapper);
  } else {
    load_settings();
  }
}

static void stop_all_timers() {
  if (pay_countdown) {
    lv_timer_del(pay_countdown);
    pay_countdown = nullptr;
  }
  if (suc_countdown) {
    lv_timer_del(suc_countdown);
    suc_countdown = nullptr;
  }
  // Reset pointers
  lbl_pay_timer = nullptr;
  lbl_suc_timer = nullptr;
}

// ===============================================================
// 8. HOME SCREEN
// ===============================================================

struct ServiceDef {
  const char* label;
  const void* icon;  // MUST be const void* for images!
  Option* opts;
  int count;
  bool isFixed;
  lv_color_t color;
};

static ServiceDef services[] = {
  { "Ticket Booking", &img_ticket, ticketOpts, TICKET_OPTS, false, { 13, 148, 136 } },
  { "Parking Fee", &img_parking, parkingOpts, PARKING_OPTS, false, { 16, 185, 129 } },
  { "Event Pass", &img_event, eventOpts, EVENT_OPTS, false, { 139, 92, 246 } },
  { "Make a Donation", &img_donate, donationOpts, DONATION_OPTS, true, { 244, 63, 94 } }
};

struct CardCbData {
  int idx;
};
static CardCbData card_cb_data[4];

static void on_service_card(lv_event_t* e) {
  CardCbData* d = (CardCbData*)lv_event_get_user_data(e);
  resetAllOptions();
  current_service_type = String(services[d->idx].label);
  load_selection(services[d->idx].label, services[d->idx].opts, services[d->idx].count, services[d->idx].isFixed);
}

void load_home() {
  stop_all_timers();
  lv_obj_t* old_scr = lv_scr_act();
  scr_home = make_screen();
  make_header(scr_home, "TicketWave", nullptr);
  make_footer(scr_home, false);

  lv_obj_t* cont = make_content_area(scr_home);

  lv_obj_t* lbl_sel = lv_label_create(cont);
  lv_obj_add_style(lbl_sel, &theme.label_muted, 0);
  lv_label_set_text(lbl_sel, "SELECT A SERVICE");
  lv_obj_set_width(lbl_sel, 440);

  lv_obj_t* list = lv_obj_create(cont);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_height(list, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, 16, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < 4; i++) {
    lv_obj_t* card = lv_btn_create(list);
    lv_obj_add_style(card, &theme.card, 0);
    lv_obj_set_size(card, 440, 110);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    card_cb_data[i].idx = i;
    lv_obj_add_event_cb(card, on_service_card, LV_EVENT_CLICKED, &card_cb_data[i]);

    lv_obj_t* circle = lv_obj_create(card);
    lv_obj_set_size(circle, 56, 56);
    lv_obj_align(circle, LV_ALIGN_LEFT_MID, 24, 0);
    lv_obj_set_style_bg_opa(circle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* img_icon = lv_img_create(circle);
    lv_img_set_src(img_icon, services[i].icon);
    lv_obj_center(img_icon);

    lv_obj_t* lbl_name = lv_label_create(card);
    lv_obj_add_style(lbl_name, &theme.label_body, 0);
    lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_20, 0);
    lv_label_set_text(lbl_name, services[i].label);
    lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 100, 32);

    lv_obj_t* lbl_tap = lv_label_create(card);
    lv_obj_add_style(lbl_tap, &theme.label_muted, 0);
    lv_label_set_text(lbl_tap, "Tap to select");
    lv_obj_align(lbl_tap, LV_ALIGN_TOP_LEFT, 100, 60);
  }

  lv_scr_load(scr_home);
  if (old_scr) lv_obj_del_async(old_scr);
}

// ===============================================================
// 9. SELECTION SCREEN
// ===============================================================
// [Core business logic remains fully untouched]
String live_qr_string = "";

struct SelCtx {
  Option* opts;
  int count;
  bool isFixed;
  lv_obj_t* lbl_total;
  lv_obj_t* btn_proceed;
};
static SelCtx sel_ctx;

struct QtyCtx {
  int idx;
  int delta;
  lv_obj_t* lbl_qty;
};
static QtyCtx qty_ctxs[10];

struct FixCtx {
  int idx;
  lv_obj_t* btn_self;
};
static FixCtx fix_ctxs[10];

struct PayCtx {
  int method;
  lv_obj_t* btns[3];
};
static PayCtx pay_ctx;

static void refresh_total_label() {
  if (!sel_ctx.lbl_total) return;
  char buf[24];
  snprintf(buf, sizeof(buf), "Rs. %lu", totalAmount);
  lv_label_set_text(sel_ctx.lbl_total, buf);
  if (sel_ctx.btn_proceed) {
    if (totalAmount > 0) {
      lv_obj_add_style(sel_ctx.btn_proceed, &theme.btn_primary, 0);
      lv_obj_remove_style(sel_ctx.btn_proceed, &theme.btn_outline, 0);
      lv_obj_add_flag(sel_ctx.btn_proceed, LV_OBJ_FLAG_CLICKABLE);
    } else {
      lv_obj_add_style(sel_ctx.btn_proceed, &theme.btn_outline, 0);
      lv_obj_remove_style(sel_ctx.btn_proceed, &theme.btn_primary, 0);
      lv_obj_clear_flag(sel_ctx.btn_proceed, LV_OBJ_FLAG_CLICKABLE);
    }
    lv_obj_invalidate(sel_ctx.btn_proceed);
  }
}

static void recalc_total(Option* opts, int count, bool isFixed) {
  totalAmount = 0;
  for (int i = 0; i < count; i++) {
    if (isFixed) {
      if (opts[i].selected) totalAmount = opts[i].price;
    } else {
      totalAmount += (uint32_t)opts[i].qty * opts[i].price;
    }
  }
  refresh_total_label();
}

static void on_qty_btn(lv_event_t* e) {
  QtyCtx* c = (QtyCtx*)lv_event_get_user_data(e);
  Option& o = sel_ctx.opts[c->idx];
  int newQty = o.qty + c->delta;
  if (newQty < 0 || newQty > 99) return;
  o.qty = newQty;
  char buf[4];
  snprintf(buf, sizeof(buf), "%d", o.qty);
  lv_label_set_text(c->lbl_qty, buf);
  recalc_total(sel_ctx.opts, sel_ctx.count, sel_ctx.isFixed);
}

static void on_fixed_btn(lv_event_t* e) {
  FixCtx* c = (FixCtx*)lv_event_get_user_data(e);
  for (int i = 0; i < sel_ctx.count; i++) {
    sel_ctx.opts[i].selected = false;
    lv_obj_add_style(fix_ctxs[i].btn_self, &theme.btn_outline, 0);
    lv_obj_remove_style(fix_ctxs[i].btn_self, &theme.btn_primary, 0);
  }
  sel_ctx.opts[c->idx].selected = true;
  lv_obj_add_style(c->btn_self, &theme.btn_primary, 0);
  lv_obj_remove_style(c->btn_self, &theme.btn_outline, 0);
  recalc_total(sel_ctx.opts, sel_ctx.count, sel_ctx.isFixed);
}

static void on_pay_method(lv_event_t* e) {
  int* method = (int*)lv_event_get_user_data(e);
  for (int i = 0; i < 3; i++) {
    if (i == *method) {
      lv_obj_add_style(pay_ctx.btns[i], &theme.tag_active, 0);
      lv_obj_remove_style(pay_ctx.btns[i], &theme.tag_inactive, 0);
    } else {
      lv_obj_add_style(pay_ctx.btns[i], &theme.tag_inactive, 0);
      lv_obj_remove_style(pay_ctx.btns[i], &theme.tag_active, 0);
    }
  }
  payMethod = *method;
}
static int pay_method_vals[3] = { 0, 1, 2 };

static void on_proceed(lv_event_t* e) {
  if (totalAmount == 0) return;

  // 1. Show "Please Wait" overlay
  lv_obj_t* mb = lv_msgbox_create(NULL, "Processing", "Talking to Bonrix Server...", NULL, false);
  lv_obj_center(mb);
  lv_task_handler();

  // 2. Prepare JSON and Receipt Details
  current_receipt_details = "";
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();

  // 3. Loop through items to build both the Receipt String and JSON Data
  for (int i = 0; i < sel_ctx.count; i++) {
    int qty = sel_ctx.isFixed ? 1 : sel_ctx.opts[i].qty;
    bool isSelected = (sel_ctx.isFixed && sel_ctx.opts[i].selected) || (!sel_ctx.isFixed && qty > 0);

    // --- Inside the item loop in on_proceed() ---
    if (isSelected) {
      if (current_receipt_details != "") current_receipt_details += "\n";

      int qty = sel_ctx.isFixed ? 1 : sel_ctx.opts[i].qty;
      String nameQty = String(sel_ctx.opts[i].label) + " x" + String(qty);
      uint32_t lineTotal = qty * sel_ctx.opts[i].price;

      char lineBuf[40];
      // 3 spaces + 18 chars (name) + 1 space + 7 chars (price) + 3 spaces = 32 Total
      snprintf(lineBuf, sizeof(lineBuf), "   %-18s %7lu   ", nameQty.c_str(), (unsigned long)lineTotal);

      current_receipt_details += String(lineBuf);


      // --- BUILD THE JSON OBJECT FOR API ---
      JsonObject item = array.add<JsonObject>();
      item["ticket_count"] = qty;
      item["ticket_price"] = sel_ctx.opts[i].price;
      item["total_amount"] = lineTotal;
      item["type"] = sel_ctx.opts[i].label;
    }
  }

  // 4. Serialize JSON and Generate Order ID
  String detailDataJSON;
  serializeJson(doc, detailDataJSON);
  generateOrderId();

  // 5. Call API
  UpiData upi = fetch_upi_data(totalAmount, String(orderId), detailDataJSON);

  // 6. Close the wait message safely
  if (mb && lv_obj_is_valid(mb)) {
    lv_msgbox_close(mb);
  }

  // 7. Handle Navigation based on API Result
  if (upi.getStatus()) {
    live_qr_string = upi.getUpiString();
    current_active_txn_id = upi.getTransactionId();
    load_payment();
  } else {
    load_failure();
  }
}

void load_selection(const char* title, Option* opts, int count, bool isFixed) {
  lv_obj_t* old_scr = lv_scr_act();
  scr_select = make_screen();

  sel_ctx.opts = opts;
  sel_ctx.count = count;
  sel_ctx.isFixed = isFixed;
  sel_ctx.lbl_total = nullptr;
  sel_ctx.btn_proceed = nullptr;

  make_header(scr_select, title, nullptr);
  make_footer(scr_select, true);

  lv_obj_t* cont = make_content_area(scr_select);
  // I've increased the row padding slightly here to 20 for a cleaner look
  lv_obj_set_style_pad_row(cont, 20, 0);

  // --- 1. ITEM SELECTION AREA ---
  if (!isFixed) {
    for (int i = 0; i < count; i++) {
      lv_obj_t* row = lv_obj_create(cont);
      lv_obj_add_style(row, &theme.card, 0);
      lv_obj_set_size(row, 440, 80);
      lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_pad_hor(row, 16, 0);
      lv_obj_set_style_pad_ver(row, 0, 0);
      lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

      lv_obj_t* left = lv_obj_create(row);
      lv_obj_set_size(left, 200, LV_SIZE_CONTENT);
      lv_obj_set_style_border_width(left, 0, 0);
      lv_obj_set_style_bg_opa(left, LV_OPA_TRANSP, 0);
      lv_obj_set_style_pad_all(left, 0, 0);
      lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
      lv_obj_set_flex_align(left, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

      lv_obj_t* lbl_name = lv_label_create(left);
      lv_obj_add_style(lbl_name, &theme.label_body, 0);
      lv_label_set_text(lbl_name, opts[i].label);

      char priceStr[16];
      snprintf(priceStr, sizeof(priceStr), "Rs. %lu", opts[i].price);
      lv_obj_t* lbl_price = lv_label_create(left);
      lv_obj_add_style(lbl_price, &theme.label_muted, 0);
      lv_label_set_text(lbl_price, priceStr);

      lv_obj_t* stepper = lv_obj_create(row);
      lv_obj_set_size(stepper, 140, 52);
      lv_obj_set_style_border_width(stepper, 0, 0);
      lv_obj_set_style_bg_opa(stepper, LV_OPA_TRANSP, 0);
      lv_obj_set_style_pad_all(stepper, 0, 0);
      lv_obj_set_flex_flow(stepper, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(stepper, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_clear_flag(stepper, LV_OBJ_FLAG_SCROLLABLE);

      lv_obj_t* btn_m = lv_btn_create(stepper);
      lv_obj_add_style(btn_m, &theme.btn_outline, 0);
      lv_obj_set_size(btn_m, 44, 44);
      qty_ctxs[i * 2].idx = i;
      qty_ctxs[i * 2].delta = -1;

      lv_obj_t* lbl_qty = lv_label_create(stepper);
      lv_obj_add_style(lbl_qty, &theme.label_title, 0);
      lv_label_set_text(lbl_qty, "0");
      lv_obj_set_width(lbl_qty, 36);
      lv_obj_set_style_text_align(lbl_qty, LV_TEXT_ALIGN_CENTER, 0);

      qty_ctxs[i * 2].lbl_qty = lbl_qty;
      qty_ctxs[i * 2 + 1].idx = i;
      qty_ctxs[i * 2 + 1].delta = +1;
      qty_ctxs[i * 2 + 1].lbl_qty = lbl_qty;

      lv_obj_add_event_cb(btn_m, on_qty_btn, LV_EVENT_CLICKED, &qty_ctxs[i * 2]);
      lv_obj_t* lbl_m = lv_label_create(btn_m);
      lv_label_set_text(lbl_m, "-");
      lv_obj_center(lbl_m);

      lv_obj_t* btn_p = lv_btn_create(stepper);
      lv_obj_add_style(btn_p, &theme.btn_primary, 0);
      lv_obj_set_size(btn_p, 44, 44);
      lv_obj_add_event_cb(btn_p, on_qty_btn, LV_EVENT_CLICKED, &qty_ctxs[i * 2 + 1]);
      lv_obj_t* lbl_p = lv_label_create(btn_p);
      lv_label_set_text(lbl_p, "+");
      lv_obj_center(lbl_p);
    }
  } else {
    lv_obj_t* fix_grid = lv_obj_create(cont);
    lv_obj_set_width(fix_grid, 440);
    lv_obj_set_height(fix_grid, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(fix_grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fix_grid, 0, 0);
    lv_obj_set_style_pad_all(fix_grid, 0, 0);
    lv_obj_set_style_pad_column(fix_grid, 12, 0);
    lv_obj_set_style_pad_row(fix_grid, 12, 0);
    lv_obj_set_flex_flow(fix_grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(fix_grid, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(fix_grid, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < count; i++) {
      lv_obj_t* fbtn = lv_btn_create(fix_grid);
      lv_obj_add_style(fbtn, &theme.btn_outline, 0);
      lv_obj_set_size(fbtn, 210, 64);
      fix_ctxs[i].idx = i;
      fix_ctxs[i].btn_self = fbtn;
      lv_obj_add_event_cb(fbtn, on_fixed_btn, LV_EVENT_CLICKED, &fix_ctxs[i]);
      lv_obj_t* fl = lv_label_create(fbtn);
      lv_obj_add_style(fl, &theme.label_body, 0);
      lv_label_set_text(fl, opts[i].label);
      lv_obj_center(fl);
    }
  }

  // --- 2. TOTAL AMOUNT CARD (Slid up since "Pay Via" is removed) ---
  lv_obj_t* tot_card = lv_obj_create(cont);
  lv_obj_add_style(tot_card, &theme.card, 0);
  lv_obj_set_width(tot_card, 440);
  lv_obj_set_height(tot_card, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(tot_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(tot_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(tot_card, 12, 0);
  lv_obj_clear_flag(tot_card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* lbl_tot_hd = lv_label_create(tot_card);
  lv_obj_add_style(lbl_tot_hd, &theme.label_muted, 0);
  lv_label_set_text(lbl_tot_hd, "TOTAL AMOUNT");

  lv_obj_t* lbl_tot = lv_label_create(tot_card);
  lv_obj_add_style(lbl_tot, &theme.label_title, 0);
  lv_obj_set_style_text_font(lbl_tot, &lv_font_montserrat_24, 0);
  lv_label_set_text(lbl_tot, "Rs. 0");
  sel_ctx.lbl_total = lbl_tot;

  lv_obj_t* proc_btn = lv_btn_create(tot_card);
  lv_obj_add_style(proc_btn, &theme.btn_outline, 0);
  lv_obj_set_width(proc_btn, LV_PCT(100));
  lv_obj_set_height(proc_btn, 48);
  lv_obj_clear_flag(proc_btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(proc_btn, on_proceed, LV_EVENT_CLICKED, nullptr);
  sel_ctx.btn_proceed = proc_btn;
  lv_obj_t* proc_lbl = lv_label_create(proc_btn);
  lv_label_set_text(proc_lbl, "Proceed to Payment >");
  lv_obj_center(proc_lbl);

  lv_scr_load(scr_select);
  if (old_scr) lv_obj_del_async(old_scr);
}

// ===============================================================
// 10. QR CODE & PAYMENT SCREEN
// ===============================================================
#define QR_CANVAS_SIZE 250

static lv_obj_t* make_qr_canvas(lv_obj_t* parent, const char* text) {
  lv_color_t bg_color = lv_color_white();
  lv_color_t fg_color = lv_color_black();
  lv_obj_t* qr = lv_qrcode_create(parent, QR_CANVAS_SIZE, fg_color, bg_color);
  lv_obj_set_style_border_color(qr, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_border_width(qr, 10, LV_PART_MAIN);
  if (text != NULL && strlen(text) > 0) {
    lv_qrcode_update(qr, text, strlen(text));
  } else {
    lv_qrcode_update(qr, "upi://pay?pa=error", 18);
  }
  return qr;
}

static void on_pay_cancel(lv_event_t* e) {
  load_home();
}

static void pay_countdown_cb(lv_timer_t* t) {
  // 1. Check for Timeout (Safety First)
  if (pay_secs_left == 0) {
    stop_all_timers();
    load_failure();
    return;
  }

  // 2. Decrement the counter kaizz
  pay_secs_left--;

  // 3. --- AUTO-CONFIRMATION (DEMO MODE) ---
  // If starting at 60, 50 means 10 seconds have passed.
  if (pay_secs_left <= 50) {
    stop_all_timers();
    load_success();
    return;
  }

  // 4. Update the UI Label
  if (lbl_pay_timer && lv_obj_is_valid(lbl_pay_timer)) {
    uint32_t mm = pay_secs_left / 60;
    uint32_t ss = pay_secs_left % 60;

    char buf[10];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", mm, ss);  // Force 00:59 format
    lv_label_set_text(lbl_pay_timer, buf);

    // Turn text RED if less than 15 seconds remain
    if (pay_secs_left < 15) {
      lv_obj_set_style_text_color(lbl_pay_timer, C_ERROR, 0);
    } else {
      lv_obj_set_style_text_color(lbl_pay_timer, C_ACCENT, 0);
    }
  }
}

void load_payment() {
  stop_all_timers();
  lv_obj_t* old_scr = lv_scr_act();
  scr_payment = make_screen();
  make_header(scr_payment, "Scan & Pay", nullptr);
  make_footer(scr_payment, false);

  lv_obj_t* cont = make_content_area(scr_payment);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // --- Timer Card Section ---
  lv_obj_t* timer_card = lv_obj_create(cont);
  lv_obj_add_style(timer_card, &theme.card, 0);
  lv_obj_set_size(timer_card, 440, 64);
  lv_obj_clear_flag(timer_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_flex_flow(timer_card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(timer_card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* lbl_timer_hd = lv_label_create(timer_card);
  lv_obj_add_style(lbl_timer_hd, &theme.label_muted, 0);
  lv_label_set_text(lbl_timer_hd, "Time remaining:");

  lbl_pay_timer = lv_label_create(timer_card);
  lv_obj_add_style(lbl_pay_timer, &theme.label_title, 0);
  lv_obj_set_style_text_font(lbl_pay_timer, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(lbl_pay_timer, C_ACCENT, 0);
  lv_obj_set_width(lbl_pay_timer, 80);
  lv_obj_set_style_text_align(lbl_pay_timer, LV_TEXT_ALIGN_RIGHT, 0);
  lv_label_set_text(lbl_pay_timer, "01:00");  // Changed to 1 minute

  // --- QR Code Section ---
  lv_obj_t* qr_card = lv_obj_create(cont);
  lv_obj_add_style(qr_card, &theme.card, 0);
  lv_obj_set_size(qr_card, QR_CANVAS_SIZE + 40, QR_CANVAS_SIZE + 40);
  lv_obj_clear_flag(qr_card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(qr_card, 10, 0);

  lv_obj_t* qr_widget = make_qr_canvas(qr_card, live_qr_string.c_str());
  lv_obj_center(qr_widget);

  // --- FIX 1: Professional Branding ---
  lv_obj_t* lbl_upi = lv_label_create(cont);
  lv_obj_add_style(lbl_upi, &theme.label_body, 0);
  lv_label_set_text(lbl_upi, "Bonrix Pay");
  lv_obj_set_style_text_color(lbl_upi, C_ACCENT, 0);

  // --- FIX 2: Correct Amount Display (Rs. 0 Fix) ---
  char amtBuf[24];
  snprintf(amtBuf, sizeof(amtBuf), "Rs. %lu", totalAmount);
  lv_obj_t* lbl_amt = lv_label_create(cont);
  lv_obj_add_style(lbl_amt, &theme.label_title, 0);
  lv_obj_set_style_text_font(lbl_amt, &lv_font_montserrat_24, 0);
  lv_label_set_text(lbl_amt, amtBuf);

  // --- Order ID Section ---
  char oidBuf[32];
  snprintf(oidBuf, sizeof(oidBuf), "Order: %s", orderId);
  lv_obj_t* lbl_oid = lv_label_create(cont);
  lv_obj_add_style(lbl_oid, &theme.label_muted, 0);
  lv_label_set_text(lbl_oid, oidBuf);

  // --- Cancel Button ---
  lv_obj_t* btn_cancel = lv_btn_create(cont);
  lv_obj_add_style(btn_cancel, &theme.btn_outline, 0);
  lv_obj_set_size(btn_cancel, 440, 52);
  lv_obj_add_event_cb(btn_cancel, on_pay_cancel, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* l_canc = lv_label_create(btn_cancel);
  lv_label_set_text(l_canc, "Cancel Payment");
  lv_obj_set_style_text_color(l_canc, C_ERROR, 0);
  lv_obj_center(l_canc);

  // --- Logic Sync ---
  pay_secs_left = 60;  // 1 minute
  pay_countdown = lv_timer_create(pay_countdown_cb, 1000, nullptr);

  lv_scr_load(scr_payment);
  if (old_scr) lv_obj_del_async(old_scr);
}

// ===============================================================
// 11. RESULT SCREENS
// ===============================================================
static void on_done(lv_event_t* e) {
  load_home();
}

static void suc_countdown_cb(lv_timer_t* t) {
  if (suc_secs_left == 0) {
    lv_timer_del(suc_countdown);
    suc_countdown = nullptr;
    load_home();
    return;
  }
  suc_secs_left--;
  if (lbl_suc_timer && lv_obj_is_valid(lbl_suc_timer)) {
    char buf[32];
    snprintf(buf, sizeof(buf), "Auto return in %lus", suc_secs_left);
    lv_label_set_text(lbl_suc_timer, buf);
  }
}

void load_success() {
  stop_all_timers();
  lv_obj_t* old_scr = lv_scr_act();
  scr_success = make_screen();

  // --- THE MAGIC MOMENT: TRIGGER THE PRINTER ---

  print_receipt(orderId, totalAmount, current_service_type.c_str(), current_receipt_details.c_str());

  make_header(scr_success, "Payment Complete", nullptr);
  make_footer(scr_success, false);

  lv_obj_t* cont = make_content_area(scr_success);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  // Success Icon (Green Circle with OK)
  lv_obj_t* circle = lv_obj_create(cont);
  lv_obj_set_size(circle, 140, 140);
  lv_obj_set_style_bg_color(circle, C_SUCCESS, 0);
  lv_obj_set_style_border_width(circle, 0, 0);
  lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);

  lv_obj_t* lbl_check = lv_label_create(circle);
  lv_obj_set_style_text_font(lbl_check, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(lbl_check, C_WHITE, 0);
  lv_label_set_text(lbl_check, "OK");
  lv_obj_center(lbl_check);

  lv_obj_t* lbl_main = lv_label_create(cont);
  lv_obj_add_style(lbl_main, &theme.label_title, 0);
  lv_obj_set_style_text_font(lbl_main, &lv_font_montserrat_24, 0);
  lv_label_set_text(lbl_main, "Payment Successful!");
  lv_obj_set_style_text_align(lbl_main, LV_TEXT_ALIGN_CENTER, 0);

  // Display the Reference ID on screen for the user
  lv_obj_t* lbl_sub = lv_label_create(cont);
  lv_obj_add_style(lbl_sub, &theme.label_muted, 0);
  lv_label_set_text_fmt(lbl_sub, "Order: %s\nAmount: Rs. %lu", orderId, totalAmount);
  lv_obj_set_style_text_align(lbl_sub, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t* btn_done = lv_btn_create(cont);
  lv_obj_add_style(btn_done, &theme.btn_success, 0);
  lv_obj_set_size(btn_done, 300, 56);
  lv_obj_add_event_cb(btn_done, on_done, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl_done = lv_label_create(btn_done);
  lv_label_set_text(lbl_done, "Back to Home");
  lv_obj_center(lbl_done);

  lbl_suc_timer = lv_label_create(cont);
  lv_obj_add_style(lbl_suc_timer, &theme.label_muted, 0);
  lv_label_set_text(lbl_suc_timer, "Auto return in 10s");

  suc_secs_left = 10;
  suc_countdown = lv_timer_create(suc_countdown_cb, 1000, nullptr);

  lv_scr_load(scr_success);
  if (old_scr) lv_obj_del_async(old_scr);
}

// ===============================================================
// FAILURE SCREEN CALLBACKS & LOGIC
// ===============================================================
static void on_failure_cancel(lv_event_t* e) {
  load_home();
}
static void on_failure_retry(lv_event_t* e) {
  load_payment();
}

void load_failure() {
  stop_all_timers();
  lv_obj_t* old_scr = lv_scr_act();
  scr_failure = make_screen();

  make_header(scr_failure, "Payment Failed", nullptr);
  make_footer(scr_failure, true);

  lv_obj_t* cont = make_content_area(scr_failure);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* circle = lv_obj_create(cont);
  lv_obj_set_size(circle, 140, 140);
  lv_obj_set_style_bg_color(circle, C_ERROR, 0);
  lv_obj_set_style_border_width(circle, 0, 0);
  lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);

  lv_obj_t* lbl_x = lv_label_create(circle);
  lv_obj_set_style_text_font(lbl_x, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(lbl_x, C_WHITE, 0);
  lv_label_set_text(lbl_x, "X");
  lv_obj_center(lbl_x);

  lv_obj_t* lbl_main = lv_label_create(cont);
  lv_obj_add_style(lbl_main, &theme.label_title, 0);
  lv_obj_set_style_text_font(lbl_main, &lv_font_montserrat_24, 0);
  lv_label_set_text(lbl_main, "Payment Failed");
  lv_obj_set_style_text_align(lbl_main, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t* lbl_sub = lv_label_create(cont);
  lv_obj_add_style(lbl_sub, &theme.label_muted, 0);
  lv_label_set_text(lbl_sub, "Could not process. Please try again.");
  lv_obj_set_style_text_align(lbl_sub, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t* btn_row = lv_obj_create(cont);
  lv_obj_set_width(btn_row, 440);
  lv_obj_set_height(btn_row, 60);
  lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btn_row, 0, 0);
  lv_obj_set_style_pad_all(btn_row, 0, 0);
  lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* btn_cancel = lv_btn_create(btn_row);
  lv_obj_add_style(btn_cancel, &theme.btn_outline, 0);
  lv_obj_set_size(btn_cancel, 210, 52);
  lv_obj_add_event_cb(btn_cancel, on_failure_cancel, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lc = lv_label_create(btn_cancel);
  lv_label_set_text(lc, "Cancel");
  lv_obj_center(lc);

  lv_obj_t* btn_retry = lv_btn_create(btn_row);
  lv_obj_add_style(btn_retry, &theme.btn_danger, 0);
  lv_obj_set_size(btn_retry, 210, 52);
  lv_obj_add_event_cb(btn_retry, on_failure_retry, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lr = lv_label_create(btn_retry);
  lv_label_set_text(lr, "Retry");
  lv_obj_center(lr);

  lv_scr_load(scr_failure);
  if (old_scr) lv_obj_del_async(old_scr);
}

// ===============================================================
// 12. NEW SETTINGS SCREEN — Beautiful List Menu
// ===============================================================
static void on_settings_wifi_cb(lv_event_t* e) {
  load_wifi_scan();
}

static void on_settings_pos_cb(lv_event_t* e) {
  load_pos_settings();
}

static void on_settings_load_cb(lv_event_t* e) {
  static const char* btns[] = { "OK", "" };
  lv_obj_t* mb = lv_msgbox_create(scr_settings, "Load Data", "Data sync coming soon.", btns, true);
  lv_obj_center(mb);
}

static void on_settings_pin_cb(lv_event_t* e) {
  show_pin_setup_dialog();
}

static void on_settings_printer_cb(lv_event_t* e) {
  load_printer_settings();
}

void load_settings() {
  stop_all_timers();
  lv_obj_t* old_scr = lv_scr_act();

  scr_settings = make_screen();
  make_header(scr_settings, "Settings", "Device Configuration");
  make_footer(scr_settings, true);

  lv_obj_t* cont = make_content_area(scr_settings);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(cont, 20, 0);  // Gap between List and Status Card
  lv_obj_set_style_pad_hor(cont, 20, 0);

  // ==========================================
  // 1. NAVIGATION MENU (List)
  // ==========================================
  lv_obj_t* lbl_sec = lv_label_create(cont);
  lv_obj_add_style(lbl_sec, &theme.label_muted, 0);
  lv_label_set_text(lbl_sec, "DEVICE SETTINGS");
  lv_obj_set_width(lbl_sec, SCR_W - 40);

  lv_obj_t* lst = lv_list_create(cont);
  lv_obj_set_width(lst, SCR_W - 40);
  lv_obj_set_height(lst, LV_SIZE_CONTENT);
  lv_obj_add_style(lst, &theme.card, 0);  // Reuse card style for consistency
  lv_obj_set_style_pad_all(lst, 0, 0);
  lv_obj_clear_flag(lst, LV_OBJ_FLAG_SCROLLABLE);

  struct MenuItem {
    const char* icon;
    const char* text;
    lv_event_cb_t cb;
  };
  const MenuItem items[] = {
    { LV_SYMBOL_WIFI, "Select WiFi", on_settings_wifi_cb },
    { LV_SYMBOL_EDIT, "POS Setting", on_settings_pos_cb },
    { LV_SYMBOL_DOWNLOAD, "Load Data", on_settings_load_cb },
    { LV_SYMBOL_KEYBOARD, "Set PIN", on_settings_pin_cb },
    { LV_SYMBOL_LIST, "Printer Setting", on_settings_printer_cb },
  };
  const int ITEM_COUNT = 5;

  for (int i = 0; i < ITEM_COUNT; i++) {
    lv_obj_t* btn = lv_list_add_btn(lst, items[i].icon, items[i].text);
    lv_obj_set_height(btn, 64);
    lv_obj_set_style_bg_color(btn, C_CARD, 0);
    lv_obj_set_style_text_font(btn, &lv_font_montserrat_16, 0);
    lv_obj_set_style_border_width(btn, 0, 0);

    if (i < ITEM_COUNT - 1) {
      lv_obj_set_style_border_color(btn, C_BORDER, 0);
      lv_obj_set_style_border_width(btn, 1, 0);
      lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_BOTTOM, 0);
    }

    lv_obj_t* icon_lbl = lv_obj_get_child(btn, 0);
    if (icon_lbl) lv_obj_set_style_text_color(icon_lbl, C_ACCENT, 0);

    lv_obj_t* chevron = lv_label_create(btn);
    lv_label_set_text(chevron, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(chevron, C_MUTED, 0);
    lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -4, 0);

    lv_obj_add_event_cb(btn, items[i].cb, LV_EVENT_CLICKED, nullptr);
  }

  // ==========================================
  // 2. STATUS DASHBOARD (Card)
  // ==========================================
  lv_obj_t* status_card = lv_obj_create(cont);
  lv_obj_add_style(status_card, &theme.card, 0);
  lv_obj_set_width(status_card, SCR_W - 40);
  lv_obj_set_height(status_card, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(status_card, 16, 0);
  lv_obj_set_flex_flow(status_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(status_card, 12, 0);  // Spacing between status rows
  lv_obj_clear_flag(status_card, LV_OBJ_FLAG_SCROLLABLE);

  // --- NETWORK ROW ---
  lv_obj_t* net_cont = lv_obj_create(status_card);
  lv_obj_set_size(net_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(net_cont, 0, 0);
  lv_obj_set_style_border_width(net_cont, 0, 0);
  lv_obj_set_style_pad_all(net_cont, 0, 0);
  lv_obj_set_flex_flow(net_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(net_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* net_dot = lv_obj_create(net_cont);
  lv_obj_set_size(net_dot, 12, 12);
  lv_obj_set_style_radius(net_dot, LV_RADIUS_CIRCLE, 0);
  bool net_ok = (WiFi.status() == WL_CONNECTED);
  lv_obj_set_style_bg_color(net_dot, net_ok ? C_SUCCESS : C_ERROR, 0);
  lv_obj_set_style_border_width(net_dot, 0, 0);

  lv_obj_t* net_txt = lv_label_create(net_cont);
  if (net_ok) {
    lv_label_set_text_fmt(net_txt, "Network: %s", current_ssid.c_str());
  } else {
    lv_label_set_text(net_txt, "Network: Disconnected");
  }
  lv_obj_add_style(net_txt, &theme.label_body, 0);

  // --- PRINTER ROW ---
  lv_obj_t* prn_cont = lv_obj_create(status_card);
  lv_obj_set_size(prn_cont, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(prn_cont, 0, 0);
  lv_obj_set_style_border_width(prn_cont, 0, 0);
  lv_obj_set_style_pad_all(prn_cont, 0, 0);
  lv_obj_set_flex_flow(prn_cont, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(prn_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* prn_dot = lv_obj_create(prn_cont);
  lv_obj_set_size(prn_dot, 12, 12);
  lv_obj_set_style_radius(prn_dot, LV_RADIUS_CIRCLE, 0);
  PrinterState p_state = get_printer_state();
  lv_color_t dot_color = C_ERROR; 
  if (p_state == PrinterState::READY) {
      dot_color = C_SUCCESS;
  } else if (p_state == PrinterState::SCANNING || p_state == PrinterState::CONNECTING) {
      dot_color = lv_color_make(245, 158, 11); // Amber warning color
  }
  lv_obj_set_style_bg_color(prn_dot, dot_color, 0);
  lv_obj_set_style_border_width(prn_dot, 0, 0);

  lv_obj_t* prn_txt = lv_label_create(prn_cont);
  lv_obj_add_style(prn_txt, &theme.label_body, 0);
  
  lv_label_set_text_fmt(prn_txt, "Printer: %s", get_printer_state_str());

  lv_scr_load(scr_settings);
  if (old_scr) lv_obj_del_async(old_scr);
}

// ===============================================================
// 12b. PRINTER SETTINGS & BLUETOOTH SCAN FLOW
// ===============================================================

// --- Forward Declarations ---
void load_printer_settings();
void load_printer_scan();
void load_printer_manual();
void load_pos_settings();

static void on_prn_back_cb(lv_event_t* e) {
  load_settings();
}
static void on_prn_scan_back_cb(lv_event_t* e) {
  load_printer_settings();
}

// ---------------------------------------------------------------
// SCREEN 3: Manual MAC Entry
// ---------------------------------------------------------------

static void on_prn_manual_save(lv_event_t* e) {
  if (ta_prn_mac) {
    String manual_mac = lv_textarea_get_text(ta_prn_mac);
    manual_mac.trim();
    manual_mac.toUpperCase();

    if (manual_mac.length() > 0) {
      // Try to save it (backend will reject if invalid)
      save_printer_mac(manual_mac);

      // Check if the backend accepted it
      if (String(get_printer_mac()) == manual_mac) {
        lv_obj_t* mb = lv_msgbox_create(NULL, "Printer", "MAC Saved. Connecting...", NULL, false);
        lv_obj_center(mb);

        lv_timer_create([](lv_timer_t* t) {
          lv_obj_t* m = (lv_obj_t*)t->user_data;
          if (lv_obj_is_valid(m)) lv_msgbox_close(m);
          load_printer_settings();
        }, 1500, mb);
      } else {
        // Validation failed!
        lv_obj_t* mb = lv_msgbox_create(NULL, "Error", "Invalid MAC format!\nMust be XX:XX:XX:XX:XX:XX", NULL, true);
        lv_obj_center(mb);
      }
    }
  }
}

void load_printer_manual() {
  stop_all_timers();
  lv_obj_t* old_scr = lv_scr_act();
  scr_prn_manual = make_screen();
  make_header(scr_prn_manual, "Manual MAC", "Enter Printer MAC Address");

  lv_obj_t* cont = make_content_area(scr_prn_manual);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

  lv_obj_t* card = lv_obj_create(cont);
  lv_obj_add_style(card, &theme.card, 0);
  lv_obj_set_size(card, SCR_W - 40, 120);
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* lbl = lv_label_create(card);
  lv_obj_add_style(lbl, &theme.label_muted, 0);
  lv_label_set_text(lbl, "Format: 00:11:22:33:44:55");

  ta_prn_mac = lv_textarea_create(card);
  lv_textarea_set_one_line(ta_prn_mac, true);
  lv_obj_set_width(ta_prn_mac, LV_PCT(100));
  lv_textarea_set_placeholder_text(ta_prn_mac, "Tap to type MAC...");

  // Show keyboard when tapped
  lv_obj_add_event_cb(
    ta_prn_mac, [](lv_event_t* e) {
      if (lv_event_get_code(e) == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(prn_kb, ta_prn_mac);
        lv_obj_clear_flag(prn_kb, LV_OBJ_FLAG_HIDDEN);
      }
    },
    LV_EVENT_ALL, NULL);

  prn_kb = lv_keyboard_create(scr_prn_manual);
  lv_keyboard_set_mode(prn_kb, LV_KEYBOARD_MODE_TEXT_UPPER);
  lv_obj_set_size(prn_kb, SCR_W, 280);
  lv_obj_align(prn_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(prn_kb, LV_OBJ_FLAG_HIDDEN);

  // Footer Buttons
  lv_obj_t* btn_bar = lv_obj_create(scr_prn_manual);
  lv_obj_set_size(btn_bar, SCR_W, 80);
  lv_obj_set_style_radius(btn_bar, 10, 0);
  lv_obj_set_flex_flow(btn_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* btn_back = lv_btn_create(btn_bar);
  lv_obj_add_style(btn_back, &theme.btn_outline, 0);
  lv_obj_set_size(btn_back, 180, 52);
  lv_obj_add_event_cb(btn_back, on_prn_scan_back_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lb = lv_label_create(btn_back);
  lv_label_set_text(lb, "< Back");
  lv_obj_center(lb);

  lv_obj_t* btn_save = lv_btn_create(btn_bar);
  lv_obj_add_style(btn_save, &theme.btn_primary, 0);
  lv_obj_set_size(btn_save, 220, 52);
  lv_obj_add_event_cb(btn_save, on_prn_manual_save, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ls = lv_label_create(btn_save);
  lv_label_set_text(ls, LV_SYMBOL_SAVE " Save MAC");
  lv_obj_center(ls);

  lv_scr_load(scr_prn_manual);
  if (old_scr) lv_obj_del_async(old_scr);
}

// ---------------------------------------------------------------
// SCREEN 2: Async Printer Scan
// ---------------------------------------------------------------

static void async_prn_scan_cb(lv_timer_t* t) {
  // 1. Get real results from the printer manager
  roller_prn_str = String(get_ble_scan_results());

  if (roller_prn_str == "") {
    roller_prn_str = "No Devices Found";
  }

  // 2. Clean up UI
  lv_timer_del(t);
  prn_scan_timer = nullptr;
  if (spin_card && lv_obj_is_valid(spin_card)) lv_obj_del(spin_card);

  // 3. Create Roller with REAL data
  lv_obj_t* roller_card = lv_obj_create(cont_prn_scan);
  lv_obj_add_style(roller_card, &theme.card, 0);
  lv_obj_set_size(roller_card, SCR_W - 40, 240);

  prn_roller = lv_roller_create(roller_card);
  lv_roller_set_options(prn_roller, roller_prn_str.c_str(), LV_ROLLER_MODE_NORMAL);
  lv_obj_set_width(prn_roller, (SCR_W - 40) - 24);
  lv_obj_center(prn_roller);

  // Connect Button
  lv_obj_t* btn_conn = lv_btn_create(cont_prn_scan);
  lv_obj_add_style(btn_conn, &theme.btn_primary, 0);
  lv_obj_set_size(btn_conn, 300, 56);
  lv_obj_t* lc = lv_label_create(btn_conn);
  lv_label_set_text(lc, LV_SYMBOL_BLUETOOTH " Connect to Selected");
  lv_obj_center(lc);

  lv_obj_add_event_cb(
    btn_conn, [](lv_event_t* e) {
      if (prn_roller) {
        char selected_str[100];
        lv_roller_get_selected_str(prn_roller, selected_str, sizeof(selected_str));

        // Simple Logic: Extract MAC from "(AA:BB...)"
        String s = String(selected_str);
        int openParen = s.lastIndexOf('(');
        int closeParen = s.lastIndexOf(')');
        if (openParen != -1 && closeParen != -1) {
          String mac = s.substring(openParen + 1, closeParen);
          save_printer_mac(mac);
          load_printer_settings();
        }
      }
    },
    LV_EVENT_CLICKED, nullptr);
}

void load_printer_scan() {
  stop_all_timers();
  lv_obj_t* old_scr = lv_scr_act();
  scr_prn_scan = make_screen();
  make_header(scr_prn_scan, "Scan Printers", "Looking for Bluetooth devices...");
  make_footer(scr_prn_scan, false);  // Custom footer below

  cont_prn_scan = make_content_area(scr_prn_scan);

  spin_card = lv_obj_create(cont_prn_scan);
  lv_obj_add_style(spin_card, &theme.card, 0);
  lv_obj_set_size(spin_card, SCR_W - 40, 80);
  lv_obj_set_flex_flow(spin_card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(spin_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(spin_card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* spinner = lv_spinner_create(spin_card, 1000, 60);
  lv_obj_set_size(spinner, 40, 40);
  lv_obj_set_style_arc_color(spinner, C_ACCENT, LV_PART_INDICATOR);

  lv_obj_t* scan_lbl = lv_label_create(spin_card);
  lv_obj_add_style(scan_lbl, &theme.label_body, 0);
  lv_label_set_text(scan_lbl, "Scanning Bluetooth...");

  // Footer Back Button
  // Footer Back Button
  lv_obj_t* btn_bar = lv_obj_create(scr_prn_scan);
  lv_obj_set_size(btn_bar, SCR_W, 60);
  lv_obj_align(btn_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_style(btn_bar, &theme.footer_bar, 0);  // Added theme style
  lv_obj_clear_flag(btn_bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* btn_back = lv_btn_create(btn_bar);
  lv_obj_add_style(btn_back, &theme.btn_outline, 0);
  lv_obj_set_size(btn_back, 130, 40);

  // FIXED: Alignment corrected
  lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 16, 0);

  lv_obj_add_event_cb(btn_back, on_prn_scan_back_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lb = lv_label_create(btn_back);
  lv_label_set_text(lb, LV_SYMBOL_LEFT " Cancel");
  lv_obj_center(lb);

  // TODO: Trigger your backend BLE Scan here
  start_ble_scan();

  // Give the background scanner 2 seconds to find devices before showing the list
  prn_scan_timer = lv_timer_create(async_prn_scan_cb, 2000, nullptr);

  lv_scr_load(scr_prn_scan);
  if (old_scr) lv_obj_del_async(old_scr);
}

// ---------------------------------------------------------------
// SCREEN 1: Printer Dashboard
// ---------------------------------------------------------------
void load_printer_settings() {
  stop_all_timers();
  lv_obj_t* old_scr = lv_scr_act();
  scr_prn_settings = make_screen();
  make_header(scr_prn_settings, "Printer Settings", "Hardware Configuration");

  lv_obj_t* cont = make_content_area(scr_prn_settings);

  // Current Status Card
  lv_obj_t* stat_card = lv_obj_create(cont);
  lv_obj_add_style(stat_card, &theme.card, 0);
  lv_obj_set_size(stat_card, SCR_W - 40, 140);
  lv_obj_set_flex_flow(stat_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(stat_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

  lv_obj_t* dot = lv_obj_create(stat_card);
  lv_obj_set_size(dot, 20, 20);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  PrinterState p_state = get_printer_state();
  lv_color_t dot_color = C_ERROR; 
  if (p_state == PrinterState::READY) {
      dot_color = C_SUCCESS;
  } else if (p_state == PrinterState::SCANNING || p_state == PrinterState::CONNECTING) {
      dot_color = lv_color_make(245, 158, 11); // Amber warning color
  }
  lv_obj_set_style_bg_color(dot, dot_color, 0);

  lv_obj_t* lbl_stat = lv_label_create(stat_card);
  lv_obj_add_style(lbl_stat, &theme.label_title, 0);
  
  // This automatically shows "Scanning", "Ready", "Connecting", or "Error"
  lv_label_set_text(lbl_stat, get_printer_state_str());

  lv_obj_t* lbl_mac = lv_label_create(stat_card);
  lv_obj_add_style(lbl_mac, &theme.label_muted, 0);
  lv_label_set_text_fmt(lbl_mac, "Target MAC: %s", get_printer_mac());

  // Connection Options Card
  lv_obj_t* opt_card = lv_obj_create(cont);
  lv_obj_add_style(opt_card, &theme.card, 0);
  lv_obj_set_size(opt_card, SCR_W - 40, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(opt_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(opt_card, 16, 0);

  lv_obj_t* lbl_opt = lv_label_create(opt_card);
  lv_obj_add_style(lbl_opt, &theme.label_muted, 0);
  lv_label_set_text(lbl_opt, "CONNECT NEW PRINTER");

  lv_obj_t* btn_scan = lv_btn_create(opt_card);
  lv_obj_add_style(btn_scan, &theme.btn_primary, 0);
  lv_obj_set_size(btn_scan, LV_PCT(100), 56);
  lv_obj_add_event_cb(
    btn_scan, [](lv_event_t* e) {
      load_printer_scan();
    },
    LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ls = lv_label_create(btn_scan);
  lv_label_set_text(ls, LV_SYMBOL_BLUETOOTH " Scan for Printers");
  lv_obj_center(ls);

  lv_obj_t* btn_man = lv_btn_create(opt_card);
  lv_obj_add_style(btn_man, &theme.btn_outline, 0);
  lv_obj_set_size(btn_man, LV_PCT(100), 56);
  lv_obj_add_event_cb(
    btn_man, [](lv_event_t* e) {
      load_printer_manual();
    },
    LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lm = lv_label_create(btn_man);
  lv_label_set_text(lm, LV_SYMBOL_KEYBOARD " Enter MAC Manually");
  lv_obj_center(lm);

  // Footer
  // Footer
  lv_obj_t* btn_bar = lv_obj_create(scr_prn_settings);
  lv_obj_set_size(btn_bar, SCR_W, 60);
  lv_obj_align(btn_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_style(btn_bar, &theme.footer_bar, 0);  // Added theme style for consistency
  lv_obj_clear_flag(btn_bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* btn_back = lv_btn_create(btn_bar);
  lv_obj_add_style(btn_back, &theme.btn_outline, 0);
  lv_obj_set_size(btn_back, 120, 40);

  // FIXED: Perfectly centered vertically and offset from left
  lv_obj_align(btn_back, LV_ALIGN_LEFT_MID, 16, 0);

  lv_obj_add_event_cb(btn_back, on_prn_back_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbb = lv_label_create(btn_back);
  lv_label_set_text(lbb, LV_SYMBOL_LEFT " Back");
  lv_obj_center(lbb);

  lv_scr_load(scr_prn_settings);
  if (old_scr) lv_obj_del_async(old_scr);
}

// ===============================================================
// 12c. INTEGRATED POS SETTINGS — Synchronized with NVS
// ===============================================================

// Updated Event Handler
static void on_pos_ta_event(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* ta = lv_event_get_target(e);
  lv_obj_t* active_screen = lv_scr_act();

  if (code == LV_EVENT_FOCUSED) {
    if (pos_kb == nullptr) {
      pos_kb = lv_keyboard_create(active_screen);
      // Using standard NUMBER mode for prices is cleaner
      lv_keyboard_set_mode(pos_kb, LV_KEYBOARD_MODE_NUMBER);
      lv_obj_set_size(pos_kb, SCR_W, 280);
    }
    lv_obj_set_parent(pos_kb, active_screen);
    lv_obj_clear_flag(pos_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(pos_kb, ta);

    // This ensures the item scrolls into view so the keyboard doesn't hide it
    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
  }

  // FIX: Hides keyboard when OK or CANCEL is pressed
  if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
    if (pos_kb) lv_obj_add_flag(pos_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_state(ta, LV_STATE_FOCUSED);
    lv_indev_reset(NULL, ta);  // Hides the blinking cursor
  }
}

// Helper for the "Clear" button
static void on_clear_ta_cb(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_user_data(e);
  lv_textarea_set_text(ta, "");
}

void load_pos_settings() {
  stop_all_timers();
  price_ta_count = 0;

  lv_obj_t* old_scr = lv_scr_act();
  scr_pos_settings = make_screen();
  make_header(scr_pos_settings, "Rate Management", "Edit Service Prices");

  // Main Content Area
  lv_obj_t* cont = make_content_area(scr_pos_settings);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);

  // FIX: Proper Scrolling. We enable scroll and add massive bottom padding
  // so the keyboard doesn't cover the bottom items.
  lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_bottom(cont, 350, 0);

  struct RowDef {
    const char* name;
    Option* opt;
  };
  RowDef rows[] = {
    { "Adult Ticket", &ticketOpts[0] },
    { "Child Ticket", &ticketOpts[1] },
    { "Senior Ticket", &ticketOpts[2] },
    { "Parking Fee", &parkingOpts[0] },
    { "General Entry", &eventOpts[0] },
    { "VIP Access", &eventOpts[1] }
  };

  for (int i = 0; i < 6; i++) {
    lv_obj_t* card = lv_obj_create(cont);
    lv_obj_add_style(card, &theme.card, 0);
    lv_obj_set_size(card, SCR_W - 40, 90);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(card);
    lv_obj_add_style(lbl, &theme.label_body, 0);
    lv_label_set_text(lbl, rows[i].name);

    // Sub-container for TextArea + Clear Button
    lv_obj_t* input_cont = lv_obj_create(card);
    lv_obj_set_size(input_cont, 180, 50);
    lv_obj_set_style_bg_opa(input_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(input_cont, 0, 0);
    lv_obj_set_style_pad_all(input_cont, 0, 0);
    lv_obj_set_flex_flow(input_cont, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(input_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ta = lv_textarea_create(input_cont);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_accepted_chars(ta, "0123456789");
    lv_obj_set_size(ta, 120, 48);
    lv_obj_set_style_text_align(ta, LV_TEXT_ALIGN_RIGHT, 0);

    char pbuf[16];
    snprintf(pbuf, sizeof(pbuf), "%lu", rows[i].opt->price);
    lv_textarea_set_text(ta, pbuf);
    lv_obj_add_event_cb(ta, on_pos_ta_event, LV_EVENT_ALL, nullptr);

    // NEW: Clear Button ("X") for each row
    lv_obj_t* btn_clear = lv_btn_create(input_cont);
    lv_obj_set_size(btn_clear, 45, 45);
    lv_obj_add_style(btn_clear, &theme.btn_outline, 0);
    lv_obj_t* lbl_x = lv_label_create(btn_clear);
    lv_label_set_text(lbl_x, LV_SYMBOL_CLOSE);
    lv_obj_center(lbl_x);
    lv_obj_add_event_cb(btn_clear, on_clear_ta_cb, LV_EVENT_CLICKED, ta);

    price_tas[i] = ta;
    price_ta_opts[i] = rows[i].opt;
    price_ta_count++;
  }

  // Save Button - Fixed at the bottom of the screen
  lv_obj_t* btn_save = lv_btn_create(scr_pos_settings);
  lv_obj_set_size(btn_save, 400, 60);
  lv_obj_align(btn_save, LV_ALIGN_BOTTOM_MID, 0, -80);
  lv_obj_add_style(btn_save, &theme.btn_primary, 0);

  lv_obj_t* lbl_save = lv_label_create(btn_save);
  lv_label_set_text(lbl_save, LV_SYMBOL_SAVE " APPLY & SAVE RATES");
  lv_obj_center(lbl_save);

  lv_obj_add_event_cb(
    btn_save, [](lv_event_t* e) {
      for (int i = 0; i < price_ta_count; i++) {
        const char* val = lv_textarea_get_text(price_tas[i]);
        price_ta_opts[i]->price = (uint32_t)atoi(val);
      }
      pos_prices.adult = ticketOpts[0].price;
      pos_prices.child = ticketOpts[1].price;
      pos_prices.senior = ticketOpts[2].price;
      pos_prices.parking = parkingOpts[0].price;
      pos_prices.event_gen = eventOpts[0].price;
      pos_prices.event_vip = eventOpts[1].price;

      save_pos_prices();
      load_settings();
    },
    LV_EVENT_CLICKED, NULL);

  make_footer(scr_pos_settings, true);
  lv_scr_load(scr_pos_settings);
  if (old_scr) lv_obj_del_async(old_scr);
}

// ===============================================================
// 13. WIFI SCAN SCREEN (NEW: ASYNCHRONOUS NON-BLOCKING)
// ===============================================================

static String roller_ssid_str = "";
static int wifi_scan_count = 0;

static void on_wifi_scan_next_cb(lv_event_t* e) {
  if (wifi_roller == nullptr || wifi_scan_count == 0) return;
  uint16_t sel = lv_roller_get_selected(wifi_roller);
  String ssidAtIdx = WiFi.SSID(sel);
  ssidAtIdx.toCharArray(wifi_selected_ssid, sizeof(wifi_selected_ssid));
  load_wifi_password(wifi_selected_ssid);
}

static void on_wifi_scan_back_cb(lv_event_t* e) {
  load_settings();
}
static void on_wifi_rescan_cb(lv_event_t* e) {
  load_wifi_scan();
}

// FIXED: Asynchronous generation of the scan result UI to prevent blocking
static void build_wifi_results_ui() {
  if (wifi_scan_count <= 0) {
    lv_obj_t* err_card = lv_obj_create(cont_wifi_scan);
    lv_obj_add_style(err_card, &theme.card, 0);
    lv_obj_set_size(err_card, SCR_W - 40, 120);
    lv_obj_set_flex_flow(err_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(err_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(err_card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_no = lv_label_create(err_card);
    lv_obj_add_style(lbl_no, &theme.label_title, 0);
    lv_label_set_text(lbl_no, "No networks found.");

    lv_obj_t* lbl_no_sub = lv_label_create(err_card);
    lv_obj_add_style(lbl_no_sub, &theme.label_muted, 0);
    lv_label_set_text(lbl_no_sub, "Tap 'Re-scan' to try again.");

    wifi_roller = nullptr;
  } else {
    roller_ssid_str = "";
    for (int i = 0; i < wifi_scan_count; i++) {
      if (i > 0) roller_ssid_str += "\n";
      roller_ssid_str += WiFi.SSID(i);
    }

    lv_obj_t* instr_lbl = lv_label_create(cont_wifi_scan);
    lv_obj_add_style(instr_lbl, &theme.label_muted, 0);
    lv_label_set_text(instr_lbl, "Scroll to select your network:");

    lv_obj_t* roller_card = lv_obj_create(cont_wifi_scan);
    lv_obj_add_style(roller_card, &theme.card, 0);
    lv_obj_set_size(roller_card, SCR_W - 40, 240);
    lv_obj_set_style_pad_all(roller_card, 12, 0);
    lv_obj_clear_flag(roller_card, LV_OBJ_FLAG_SCROLLABLE);

    wifi_roller = lv_roller_create(roller_card);
    lv_roller_set_options(wifi_roller, roller_ssid_str.c_str(), LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(wifi_roller, 4);
    lv_obj_set_width(wifi_roller, (SCR_W - 40) - 24);
    lv_obj_center(wifi_roller);
    lv_obj_set_style_text_font(wifi_roller, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(wifi_roller, C_TEXT, 0);
    lv_obj_set_style_bg_color(wifi_roller, C_CARD, 0);
    lv_obj_set_style_border_width(wifi_roller, 0, 0);
    lv_obj_set_style_text_color(wifi_roller, C_ACCENT, LV_PART_SELECTED);
    lv_obj_set_style_bg_color(wifi_roller, lv_color_make(232, 248, 246), LV_PART_SELECTED);
    lv_obj_set_style_text_font(wifi_roller, &lv_font_montserrat_20, LV_PART_SELECTED);

    lv_obj_t* note_lbl = lv_label_create(cont_wifi_scan);
    lv_obj_add_style(note_lbl, &theme.label_muted, 0);
    char count_buf[48];
    snprintf(count_buf, sizeof(count_buf), "%d network(s) found. Select one above.", wifi_scan_count);
    lv_label_set_text(note_lbl, count_buf);
  }

  // Draw Bottom Button Bar
  lv_obj_t* btn_bar = lv_obj_create(scr_wifi_scan);
  lv_obj_set_style_bg_color(btn_bar, C_CARD, 0);
  lv_obj_set_style_border_color(btn_bar, C_BORDER, 0);
  lv_obj_set_style_border_width(btn_bar, 1, 0);
  lv_obj_set_style_border_side(btn_bar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_pad_hor(btn_bar, 20, 0);
  lv_obj_set_size(btn_bar, SCR_W, 80);
  lv_obj_align(btn_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(btn_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(btn_bar, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* btn_back = lv_btn_create(btn_bar);
  lv_obj_add_style(btn_back, &theme.btn_outline, 0);
  lv_obj_set_size(btn_back, 130, 52);
  lv_obj_add_event_cb(btn_back, on_wifi_scan_back_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lb = lv_label_create(btn_back);
  lv_label_set_text(lb, "< Back");
  lv_obj_center(lb);

  lv_obj_t* btn_rescan = lv_btn_create(btn_bar);
  lv_obj_add_style(btn_rescan, &theme.btn_outline, 0);
  lv_obj_set_size(btn_rescan, 130, 52);
  lv_obj_add_event_cb(btn_rescan, on_wifi_rescan_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lr = lv_label_create(btn_rescan);
  lv_label_set_text(lr, LV_SYMBOL_REFRESH " Scan");
  lv_obj_center(lr);

  lv_obj_t* btn_next = lv_btn_create(btn_bar);
  lv_obj_add_style(btn_next, wifi_scan_count > 0 ? &theme.btn_primary : &theme.btn_outline, 0);
  if (wifi_scan_count <= 0) lv_obj_clear_flag(btn_next, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(btn_next, 130, 52);
  lv_obj_add_event_cb(btn_next, on_wifi_scan_next_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* ln = lv_label_create(btn_next);
  lv_label_set_text(ln, "Next >");
  lv_obj_center(ln);
}

// FIXED: Timer callback handling the non-blocking WiFi response
static void async_wifi_scan_cb(lv_timer_t* t) {
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) return;  // Keep spinning, UI stays responsive!

  lv_timer_del(t);
  wifi_scan_timer = nullptr;

  if (spin_card && lv_obj_is_valid(spin_card)) {
    lv_obj_del(spin_card);
    spin_card = nullptr;
  }

  if (n == WIFI_SCAN_FAILED) {
    wifi_scan_count = 0;
  } else {
    wifi_scan_count = n;
  }

  build_wifi_results_ui();
}

void load_wifi_scan() {
  stop_all_timers();
  lv_obj_t* old_scr = lv_scr_act();

  scr_wifi_scan = make_screen();
  make_header(scr_wifi_scan, "Select WiFi", "Scanning Networks...");

  cont_wifi_scan = lv_obj_create(scr_wifi_scan);
  lv_obj_set_style_bg_opa(cont_wifi_scan, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont_wifi_scan, 0, 0);
  lv_obj_set_style_pad_hor(cont_wifi_scan, 20, 0);
  lv_obj_set_style_pad_ver(cont_wifi_scan, 16, 0);
  lv_obj_set_size(cont_wifi_scan, SCR_W, SCR_H - 72 - 80);
  lv_obj_align(cont_wifi_scan, LV_ALIGN_TOP_MID, 0, 72);
  lv_obj_set_flex_flow(cont_wifi_scan, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont_wifi_scan, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(cont_wifi_scan, 16, 0);
  lv_obj_clear_flag(cont_wifi_scan, LV_OBJ_FLAG_SCROLLABLE);

  // Spinner runs smoothly because main thread isn't blocked
  spin_card = lv_obj_create(cont_wifi_scan);
  lv_obj_add_style(spin_card, &theme.card, 0);
  lv_obj_set_size(spin_card, SCR_W - 40, 80);
  lv_obj_set_flex_flow(spin_card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(spin_card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(spin_card, 16, 0);
  lv_obj_clear_flag(spin_card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* spinner = lv_spinner_create(spin_card, 1000, 60);
  lv_obj_set_size(spinner, 40, 40);
  lv_obj_set_style_arc_color(spinner, C_ACCENT, LV_PART_INDICATOR);

  lv_obj_t* scan_lbl = lv_label_create(spin_card);
  lv_obj_add_style(scan_lbl, &theme.label_body, 0);
  lv_label_set_text(scan_lbl, "Scanning for networks...");

  // NEW: Trigger Non-Blocking WiFi Scan
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.scanNetworks(true);  // true = async

  // Monitor via LVGL timer to keep UI buttery smooth
  wifi_scan_timer = lv_timer_create(async_wifi_scan_cb, 500, nullptr);

  lv_scr_load(scr_wifi_scan);
  if (old_scr && old_scr != scr_wifi_scan) lv_obj_del_async(old_scr);
}

// ===============================================================
// 14. WIFI PASSWORD SCREEN (NEW: ASYNCHRONOUS NON-BLOCKING)
// ===============================================================

static void wifi_kb_event_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  lv_obj_t* ta = lv_event_get_target(e);
  if (code == LV_EVENT_FOCUSED) {
    lv_keyboard_set_textarea(wifi_kb, ta);
    lv_obj_clear_flag(wifi_kb, LV_OBJ_FLAG_HIDDEN);
  }
  if (code == LV_EVENT_DEFOCUSED) {
    lv_keyboard_set_textarea(wifi_kb, NULL);
    lv_obj_add_flag(wifi_kb, LV_OBJ_FLAG_HIDDEN);
  }
}

// FIXED: Timer to monitor non-blocking connection
static int wifi_conn_timeout_ticks = 0;
static void async_wifi_conn_cb(lv_timer_t* t) {
  if (WiFi.status() == WL_CONNECTED || wifi_conn_timeout_ticks <= 0) {
    lv_timer_del(t);
    wifi_conn_timer = nullptr;

    if (conn_msgbox && lv_obj_is_valid(conn_msgbox)) {
      lv_msgbox_close(conn_msgbox);
      conn_msgbox = nullptr;
    }

    bool ok = (WiFi.status() == WL_CONNECTED);
    if (ok) {
      current_ssid = String(wifi_selected_ssid);
      current_wifi_pass = temp_wifi_pass;
      save_config_to_spiffs();
      configTime(19800, 0, "pool.ntp.org", "time.nist.gov");
    }
    load_wifi_result(ok);
  } else {
    wifi_conn_timeout_ticks--;
  }
}

static void on_wifi_connect_cb(lv_event_t* e) {
  if (ta_wifi_pass == nullptr) return;

  // 1. Get the credentials from the UI
  String ssid = String(wifi_selected_ssid);
  String pass = lv_textarea_get_text(ta_wifi_pass);

  // 2. Call the new helper from wifi_helper.cpp
  // This function handles the connection loop and SAVES TO NVS
  attempt_connect(ssid, pass);

  // 3. (Optional) Refresh the settings screen UI to show the new SSID
  load_settings();
}

static void on_wifi_pass_back_cb(lv_event_t* e) {
  load_wifi_scan();
}

void load_wifi_password(const char* ssid) {
  stop_all_timers();
  lv_obj_t* old_scr = lv_scr_act();

  scr_wifi_pass = make_screen();
  make_header(scr_wifi_pass, "WiFi Password", ssid);

  lv_obj_t* cont = lv_obj_create(scr_wifi_pass);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_hor(cont, 20, 0);
  lv_obj_set_style_pad_top(cont, 24, 0);
  lv_obj_set_size(cont, SCR_W, SCR_H - 72 - 80);
  lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, 72);
  lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(cont, 16, 0);
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* ssid_card = lv_obj_create(cont);
  lv_obj_add_style(ssid_card, &theme.card, 0);
  lv_obj_set_size(ssid_card, SCR_W - 40, 72);
  lv_obj_set_flex_flow(ssid_card, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(ssid_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(ssid_card, 12, 0);
  lv_obj_clear_flag(ssid_card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* wifi_icon = lv_label_create(ssid_card);
  lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(wifi_icon, C_ACCENT, 0);
  lv_obj_set_style_text_font(wifi_icon, &lv_font_montserrat_24, 0);

  lv_obj_t* ssid_col = lv_obj_create(ssid_card);
  lv_obj_set_size(ssid_col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(ssid_col, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(ssid_col, 0, 0);
  lv_obj_set_style_pad_all(ssid_col, 0, 0);
  lv_obj_set_flex_flow(ssid_col, LV_FLEX_FLOW_COLUMN);
  lv_obj_clear_flag(ssid_col, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* lbl_ssid_hd = lv_label_create(ssid_col);
  lv_obj_add_style(lbl_ssid_hd, &theme.label_muted, 0);
  lv_label_set_text(lbl_ssid_hd, "Connecting to:");

  lv_obj_t* lbl_ssid_val = lv_label_create(ssid_col);
  lv_obj_add_style(lbl_ssid_val, &theme.label_title, 0);
  lv_label_set_text(lbl_ssid_val, ssid);

  lv_obj_t* pass_card = lv_obj_create(cont);
  lv_obj_add_style(pass_card, &theme.card, 0);
  lv_obj_set_size(pass_card, SCR_W - 40, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(pass_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(pass_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(pass_card, 10, 0);
  lv_obj_clear_flag(pass_card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* lbl_pw = lv_label_create(pass_card);
  lv_obj_add_style(lbl_pw, &theme.label_muted, 0);
  lv_label_set_text(lbl_pw, "Password:");

  ta_wifi_pass = lv_textarea_create(pass_card);
  lv_textarea_set_one_line(ta_wifi_pass, true);
  lv_textarea_set_password_mode(ta_wifi_pass, true);
  lv_obj_set_width(ta_wifi_pass, LV_PCT(100));
  lv_textarea_set_placeholder_text(ta_wifi_pass, "Tap to type...");
  lv_obj_add_event_cb(ta_wifi_pass, wifi_kb_event_cb, LV_EVENT_ALL, NULL);

  lv_obj_t* btn_show = lv_btn_create(pass_card);
  lv_obj_add_style(btn_show, &theme.btn_outline, 0);
  lv_obj_set_size(btn_show, LV_PCT(100), 44);
  lv_obj_t* lbl_show = lv_label_create(btn_show);
  lv_label_set_text(lbl_show, LV_SYMBOL_EYE_OPEN " Show / Hide Password");
  lv_obj_center(lbl_show);
  lv_obj_add_event_cb(
    btn_show, [](lv_event_t* ev) {
      bool cur = lv_textarea_get_password_mode(ta_wifi_pass);
      lv_textarea_set_password_mode(ta_wifi_pass, !cur);
    },
    LV_EVENT_CLICKED, nullptr);

  wifi_kb = lv_keyboard_create(scr_wifi_pass);
  lv_keyboard_set_textarea(wifi_kb, ta_wifi_pass);
  lv_obj_set_size(wifi_kb, SCR_W, SCR_H / 3 + 20);
  lv_obj_align(wifi_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(wifi_kb, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* btn_bar = lv_obj_create(scr_wifi_pass);
  lv_obj_set_style_bg_color(btn_bar, C_CARD, 0);
  lv_obj_set_style_border_color(btn_bar, C_BORDER, 0);
  lv_obj_set_style_border_width(btn_bar, 1, 0);
  lv_obj_set_style_border_side(btn_bar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_pad_hor(btn_bar, 20, 0);
  lv_obj_set_size(btn_bar, SCR_W, 80);
  lv_obj_align(btn_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_flex_flow(btn_bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(btn_bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(btn_bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(btn_bar);

  lv_obj_t* btn_back2 = lv_btn_create(btn_bar);
  lv_obj_add_style(btn_back2, &theme.btn_outline, 0);
  lv_obj_set_size(btn_back2, 180, 52);
  lv_obj_add_event_cb(btn_back2, on_wifi_pass_back_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lb2 = lv_label_create(btn_back2);
  lv_label_set_text(lb2, "< Back");
  lv_obj_center(lb2);

  lv_obj_t* btn_conn = lv_btn_create(btn_bar);
  lv_obj_add_style(btn_conn, &theme.btn_primary, 0);
  lv_obj_set_size(btn_conn, 220, 52);
  lv_obj_add_event_cb(btn_conn, on_wifi_connect_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lc = lv_label_create(btn_conn);
  lv_label_set_text(lc, LV_SYMBOL_WIFI " Connect");
  lv_obj_center(lc);

  lv_scr_load(scr_wifi_pass);
  if (old_scr) lv_obj_del_async(old_scr);
}

// ===============================================================
// 15. WIFI RESULT SCREEN (Success / Failure)
// ===============================================================
static void on_wifi_result_back_cb(lv_event_t* e) {
  load_settings();
}
static void on_wifi_result_retry_cb(lv_event_t* e) {
  load_wifi_scan();
}

void load_wifi_result(bool success) {
  stop_all_timers();
  lv_obj_t* old_scr = lv_scr_act();

  scr_wifi_result = make_screen();
  make_header(scr_wifi_result,
              success ? "WiFi Connected" : "Connection Failed",
              success ? "Network configured" : "Could not connect");

  lv_obj_t* cont = make_content_area(scr_wifi_result);
  lv_obj_set_flex_align(cont, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(cont, 24, 0);

  lv_obj_t* circle = lv_obj_create(cont);
  lv_obj_set_size(circle, 140, 140);
  lv_obj_set_style_bg_color(circle, success ? C_SUCCESS : C_ERROR, 0);
  lv_obj_set_style_border_width(circle, 0, 0);
  lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);

  lv_obj_t* lbl_icon = lv_label_create(circle);
  lv_obj_set_style_text_font(lbl_icon, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(lbl_icon, C_WHITE, 0);
  lv_label_set_text(lbl_icon, success ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
  lv_obj_center(lbl_icon);

  lv_obj_t* lbl_main = lv_label_create(cont);
  lv_obj_add_style(lbl_main, &theme.label_title, 0);
  lv_obj_set_style_text_font(lbl_main, &lv_font_montserrat_24, 0);
  lv_label_set_text(lbl_main, success ? "Connected!" : "Failed to Connect");
  lv_obj_set_style_text_align(lbl_main, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t* lbl_sub = lv_label_create(cont);
  lv_obj_add_style(lbl_sub, &theme.label_muted, 0);
  if (success) {
    char buf[80];
    snprintf(buf, sizeof(buf), "Saved. Connected to:\n%s", wifi_selected_ssid);
    lv_label_set_text(lbl_sub, buf);
  } else {
    lv_label_set_text(lbl_sub, "Check password and try again.");
  }
  lv_obj_set_style_text_align(lbl_sub, LV_TEXT_ALIGN_CENTER, 0);

  if (success) {
    lv_obj_t* btn_done = lv_btn_create(cont);
    lv_obj_add_style(btn_done, &theme.btn_success, 0);
    lv_obj_set_size(btn_done, 300, 56);
    lv_obj_add_event_cb(btn_done, on_wifi_result_back_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* ld = lv_label_create(btn_done);
    lv_label_set_text(ld, LV_SYMBOL_OK " Done");
    lv_obj_center(ld);
  } else {
    lv_obj_t* btn_row = lv_obj_create(cont);
    lv_obj_set_width(btn_row, 440);
    lv_obj_set_height(btn_row, 60);
    lv_obj_set_style_bg_opa(btn_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(btn_row, 0, 0);
    lv_obj_set_style_pad_all(btn_row, 0, 0);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* btn_back3 = lv_btn_create(btn_row);
    lv_obj_add_style(btn_back3, &theme.btn_outline, 0);
    lv_obj_set_size(btn_back3, 200, 52);
    lv_obj_add_event_cb(btn_back3, on_wifi_result_back_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lb3 = lv_label_create(btn_back3);
    lv_label_set_text(lb3, "Settings");
    lv_obj_center(lb3);

    lv_obj_t* btn_retry = lv_btn_create(btn_row);
    lv_obj_add_style(btn_retry, &theme.btn_danger, 0);
    lv_obj_set_size(btn_retry, 200, 52);
    lv_obj_add_event_cb(btn_retry, on_wifi_result_retry_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* lr2 = lv_label_create(btn_retry);
    lv_label_set_text(lr2, LV_SYMBOL_REFRESH " Try Again");
    lv_obj_center(lr2);
  }

  lv_scr_load(scr_wifi_result);
  if (old_scr) lv_obj_del_async(old_scr);
}
void setup() {
  // --- 1. CORE SYSTEM START ---
  Serial.begin(115200);
  delay(1500);  // Wait for S3 USB CDC to stabilize
  Serial.println("🚀 [SYSTEM] Kiosk Booting...");

  // --- 2. HARDWARE WATCHDOG (Safety First) ---
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = 15000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
#else
  esp_task_wdt_init(15, true);
#endif
  esp_task_wdt_add(NULL);

  // --- 3. STORAGE & CONFIGURATION ---
  if (!SPIFFS.begin(true)) {
    Serial.println("❌ [STORAGE] SPIFFS Mount Failed");
  } else {
    Serial.println("📂 [STORAGE] SPIFFS Mounted.");
    load_config_from_spiffs();  // Loads PIN and general settings
    init_pin_system();
  }

  // Initialize POS Price Manager (NVS Persistence)
  init_pos_manager();

  // Transfer NVS prices to the UI Option arrays immediately
  ticketOpts[0].price = pos_prices.adult;
  ticketOpts[1].price = pos_prices.child;
  ticketOpts[2].price = pos_prices.senior;
  parkingOpts[0].price = pos_prices.parking;
  eventOpts[0].price = pos_prices.event_gen;
  eventOpts[1].price = pos_prices.event_vip;
  Serial.println("✅ [POS] Rates Synchronized from NVS.");

  // --- 4. NETWORK START (Early Start for Background Connection) ---
  // We start this now so it handshakes while we load the display
  wifi_auto_reconnect_logic();
  init_web_server();
  configTime(19800, 0, "pool.ntp.org", "time.nist.gov");  // India Timezone

  // --- 5. DISPLAY HARDWARE ---
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(C_BG.full);

  // Backlight Fix for CrowPanel 7"
  pinMode(38, OUTPUT);
  digitalWrite(38, HIGH);
  tft.setBrightness(220);
  Serial.println("📺 [DISPLAY] Backlight and GFX Initialized.");

  // --- 6. LVGL & MEMORY ALLOCATION ---
  lv_init();
  size_t buf_size = SCR_W * LV_BUF_LINES * sizeof(lv_color_t);

  // Use PSRAM for smooth 480x800 animations
  buf1 = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
  buf2 = (lv_color_t*)heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);

  if (buf1 == NULL || buf2 == NULL) {
    Serial.println("⚠️ [MEMORY] PSRAM Failed! Using Internal RAM fallback...");
    buf1 = (lv_color_t*)malloc(buf_size);
    buf2 = (lv_color_t*)malloc(buf_size);
    if (buf1 == NULL) ESP.restart();  // Critical failure
  }

  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, SCR_W * LV_BUF_LINES);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCR_W;
  disp_drv.ver_res = SCR_H;
  disp_drv.flush_cb = lv_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = lv_touch_cb;
  lv_indev_drv_register(&indev_drv);

  // --- 7. UI & PERIPHERALS ---
  init_theme();
  load_home();     // Start on Home Screen
  init_printer();  // Initialize BLE Printer settings

  // --- 8. MULTI-CORE TASKING ---
  // Assign Printer logic to Core 0 to prevent UI lag on Core 1
  xTaskCreatePinnedToCore(printer_task, "PrinterTask", 8192, NULL, 1, NULL, 0);

  // Timer to update the "Online/Offline" status dot every 10 seconds
  auto_recon_timer = lv_timer_create(auto_recon_cb, 10000, nullptr);

  Serial.println("🚀 [SYSTEM] Kiosk Ready for Customer.");
}

void loop() {
  lv_timer_handler();
  esp_task_wdt_reset();
  handle_web_server();
  delay(5);
}
