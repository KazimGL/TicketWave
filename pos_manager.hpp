#ifndef POS_MANAGER_HPP
#define POS_MANAGER_HPP

#include <Arduino.h>

// ---------------------------------------------------------------
// Price data structure
// Order matches the Option arrays in kiosk_main.ino:
//   ticketOpts[0] = adult
//   ticketOpts[1] = child
//   ticketOpts[2] = senior
//   parkingOpts[0] = parking
//   eventOpts[0]  = event_gen
//   eventOpts[1]  = event_vip
// ---------------------------------------------------------------
struct KioskPrices {
    uint32_t adult;
    uint32_t child;
    uint32_t senior;
    uint32_t parking;
    uint32_t event_gen;
    uint32_t event_vip;
};

// Shared across kiosk_main.ino and pos_manager.cpp
extern KioskPrices pos_prices;

// Call once in setup() — loads saved values from NVS (or defaults)
void init_pos_manager();

// Call when "APPLY NEW RATES" is pressed — persists to NVS
void save_pos_prices();

// Optional: wipes NVS and restores factory defaults
void reset_pos_defaults();

#endif // POS_MANAGER_HPP
