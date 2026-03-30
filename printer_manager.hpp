#ifndef PRINTER_MANAGER_HPP
#define PRINTER_MANAGER_HPP

#include <Arduino.h>

void init_printer();
void printer_task(void* pvParameters);
bool is_printer_connected();
const char* get_printer_mac();
void force_printer_reconnect();
void print_receipt(const char* order_id, uint32_t amount, const char* bill_type, const char* details);

// --- ADD THESE TWO LINES ---
void start_ble_scan();
const char* get_ble_scan_results();
void save_printer_mac(String new_mac);

#endif