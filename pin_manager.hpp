#ifndef PIN_MANAGER_HPP
#define PIN_MANAGER_HPP

#include <Arduino.h>
#include <lvgl.h>

// Initializes the PIN system (loads saved PIN from memory)
void init_pin_system();

// Checks if a PIN is currently set
bool has_pin();

// UI: Shows the popup to enter the PIN. 
// If correct, it automatically runs the success_callback.
void show_pin_auth_dialog(void (*success_callback)());

// UI: Shows the popup to set a new PIN.
void show_pin_setup_dialog();

#endif