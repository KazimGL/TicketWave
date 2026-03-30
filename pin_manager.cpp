#include "pin_manager.hpp"
#include <SPIFFS.h>

#define PIN_FILE_PATH "/pin.txt"

static String current_pin = "";
static void (*on_auth_success)() = nullptr;

static lv_obj_t* pin_modal_bg = nullptr;
static lv_obj_t* ta_pin       = nullptr;
static lv_obj_t* lbl_error    = nullptr;
static lv_obj_t* pin_kb       = nullptr;   // FIX: track keyboard explicitly

// ---------------------------------------------------------------
// Logic
// ---------------------------------------------------------------
void init_pin_system() {
  if (SPIFFS.exists(PIN_FILE_PATH)) {
    File file = SPIFFS.open(PIN_FILE_PATH, FILE_READ);
    if (file) {
      current_pin = file.readStringUntil('\n');
      current_pin.trim();
      file.close();
      Serial.println("[PIN] PIN loaded successfully.");
    }
  } else {
    Serial.println("[PIN] No PIN configured yet.");
  }
}

bool has_pin() {
  return current_pin.length() > 0;
}

static void save_new_pin(String new_pin) {
  current_pin = new_pin;
  File file = SPIFFS.open(PIN_FILE_PATH, FILE_WRITE);
  if (file) {
    file.println(current_pin);
    file.close();
  }
}

// ---------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------
static void close_modal_cb(lv_event_t* e) {
  if (pin_modal_bg && lv_obj_is_valid(pin_modal_bg)) {
    lv_obj_del(pin_modal_bg);
  }
  pin_modal_bg = nullptr;
  ta_pin       = nullptr;
  lbl_error    = nullptr;
  pin_kb       = nullptr;
}

static void auth_kb_event_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  // LV_EVENT_READY fires when the checkmark/enter key is pressed
  if (code == LV_EVENT_READY) {
    if (!ta_pin || !lv_obj_is_valid(ta_pin)) return;
    String entered = lv_textarea_get_text(ta_pin);
    if (entered == current_pin) {
      close_modal_cb(nullptr);
      if (on_auth_success) on_auth_success();
    } else {
      lv_textarea_set_text(ta_pin, "");
      if (lbl_error && lv_obj_is_valid(lbl_error)) {
        lv_label_set_text(lbl_error, "Incorrect PIN. Try again.");
      }
    }
  }
}

static void setup_kb_event_cb(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    if (!ta_pin || !lv_obj_is_valid(ta_pin)) return;
    String entered = lv_textarea_get_text(ta_pin);
    if (entered.length() > 0) {
      save_new_pin(entered);
      close_modal_cb(nullptr);
      lv_obj_t* mb = lv_msgbox_create(lv_scr_act(), "Success",
        "New PIN has been set.", NULL, true);
      lv_obj_center(mb);
    } else {
      if (lbl_error && lv_obj_is_valid(lbl_error)) {
        lv_label_set_text(lbl_error, "PIN cannot be empty.");
      }
    }
  }
}

// ---------------------------------------------------------------
// UI Builder
// FIX: Returns the keyboard pointer directly instead of using kaizz
//      a fragile hardcoded child index (was lv_obj_get_child(card,4))
// ---------------------------------------------------------------
static lv_obj_t* create_base_modal(const char* title) {
  // Semi-transparent overlay
  pin_modal_bg = lv_obj_create(lv_scr_act());
  lv_obj_set_size(pin_modal_bg, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(pin_modal_bg, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(pin_modal_bg, LV_OPA_50, 0);
  lv_obj_set_style_border_width(pin_modal_bg, 0, 0);
  lv_obj_set_style_radius(pin_modal_bg, 0, 0);
  lv_obj_clear_flag(pin_modal_bg, LV_OBJ_FLAG_SCROLLABLE);

  // White card
  lv_obj_t* card = lv_obj_create(pin_modal_bg);
  lv_obj_set_size(card, 380, 420);
  lv_obj_center(card);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_shadow_width(card, 24, 0);
  lv_obj_set_style_shadow_color(card, lv_color_make(0,0,0), 0);
  lv_obj_set_style_shadow_opa(card, LV_OPA_20, 0);
  lv_obj_set_style_pad_all(card, 16, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  // Close button (X)
  lv_obj_t* btn_close = lv_btn_create(card);
  lv_obj_set_size(btn_close, 40, 40);
  lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, 0, -8);
  lv_obj_set_style_bg_opa(btn_close, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(btn_close, 0, 0);
  lv_obj_add_event_cb(btn_close, close_modal_cb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* lbl_close = lv_label_create(btn_close);
  lv_label_set_text(lbl_close, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_color(lbl_close, lv_color_make(148,163,184), 0);
  lv_obj_center(lbl_close);

  // Title
  lv_obj_t* lbl_title = lv_label_create(card);
  lv_label_set_text(lbl_title, title);
  lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(lbl_title, lv_color_make(51,65,85), 0);
  lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 0, 8);

  // Password text area
  ta_pin = lv_textarea_create(card);
  lv_textarea_set_password_mode(ta_pin, true);
  lv_textarea_set_one_line(ta_pin, true);
  lv_textarea_set_max_length(ta_pin, 6);
  lv_obj_set_width(ta_pin, 200);
  lv_obj_align(ta_pin, LV_ALIGN_TOP_MID, 0, 56);

  // Error label
  lbl_error = lv_label_create(card);
  lv_label_set_text(lbl_error, "");
  lv_obj_set_style_text_color(lbl_error, lv_color_make(244,63,94), 0);
  lv_obj_set_style_text_font(lbl_error, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_error, LV_ALIGN_TOP_MID, 0, 108);

  // Numeric keyboard — FIX: stored in static pointer, returned to caller
  pin_kb = lv_keyboard_create(card);
  lv_keyboard_set_mode(pin_kb, LV_KEYBOARD_MODE_NUMBER);
  lv_keyboard_set_textarea(pin_kb, ta_pin);
  lv_obj_set_size(pin_kb, 340, 210);
  lv_obj_align(pin_kb, LV_ALIGN_BOTTOM_MID, 0, -8);

  return pin_kb;   // Caller attaches its own event callback to this pointer kaizz
}

void show_pin_auth_dialog(void (*success_callback)()) {
  on_auth_success = success_callback;
  lv_obj_t* kb = create_base_modal("Enter Admin PIN");
  // FIX: attach event directly to the returned keyboard pointer — no child index
  lv_obj_add_event_cb(kb, auth_kb_event_cb, LV_EVENT_ALL, nullptr);
}

void show_pin_setup_dialog() {
  lv_obj_t* kb = create_base_modal("Set New PIN");
  lv_textarea_set_placeholder_text(ta_pin, "New 4-6 digit PIN");
  // FIX: same — direct pointer, not fragile child index
  lv_obj_add_event_cb(kb, setup_kb_event_cb, LV_EVENT_ALL, nullptr);
}
