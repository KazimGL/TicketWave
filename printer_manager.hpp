#ifndef PRINTER_MANAGER_HPP
#define PRINTER_MANAGER_HPP

#include <Arduino.h>

// ---------------------------------------------------------------
// BLE Connection State Machine @kaiz
// ---------------------------------------------------------------
enum class PrinterState {
    IDLE,        // No target MAC configured
    SCANNING,    // BLE scan active, looking for target or listing devices
    CONNECTING,  // Target found, handshake in progress
    READY,       // Connected and characteristic obtained — ready to print
    ERROR        // Connection failed; watchdog will retry
};

// ---------------------------------------------------------------
// Core Lifecycle
// ---------------------------------------------------------------
void init_printer();
void printer_task(void* pvParameters);   // Pin to Core 0

// ---------------------------------------------------------------
// Status Queries  (safe to call from any core / LVGL timer)
// ---------------------------------------------------------------
bool              is_printer_connected();
const char*       get_printer_mac();       // Target MAC or "Not Configured"
PrinterState      get_printer_state();     // Current state-machine state
const char*       get_printer_state_str(); // Human-readable state string

// ---------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------
void save_printer_mac(String new_mac);    // Validates, persists, forces reconnect
void force_printer_reconnect();           // Reset state → SCANNING

// ---------------------------------------------------------------
// BLE Scan UI helpers  (results consumed by LVGL roller)
// ---------------------------------------------------------------
void        start_ble_scan();             // Start/restart discovery scan
const char* get_ble_scan_results();       // "\n"-delimited "Name (MAC)" list

// ---------------------------------------------------------------
// Printing  (call only when is_printer_connected() == true)
// ---------------------------------------------------------------
void print_receipt(const char* order_id,
                   uint32_t    amount,
                   const char* bill_type,
                   const char* details);

#endif // PRINTER_MANAGER_HPP
