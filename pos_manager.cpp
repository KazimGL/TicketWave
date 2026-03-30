    #include "pos_manager.hpp"
    #include <Preferences.h>

    // ---------------------------------------------------------------
    // NVS namespace — keep under 15 chars (ESP-IDF limit)
    // ---------------------------------------------------------------
    #define NVS_NAMESPACE  "pos_data"

    // NVS keys — keep under 15 chars each
    #define KEY_ADULT      "p_adult"
    #define KEY_CHILD      "p_child"
    #define KEY_SENIOR     "p_senior"
    #define KEY_PARKING    "p_park"
    #define KEY_EVENT_GEN  "p_egen"
    #define KEY_EVENT_VIP  "p_evip"

    // ---------------------------------------------------------------
    // Default prices (used when no saved data exists)
    // ---------------------------------------------------------------
    #define DEFAULT_ADULT      150
    #define DEFAULT_CHILD       75
    #define DEFAULT_SENIOR     100
    #define DEFAULT_PARKING     50
    #define DEFAULT_EVENT_GEN  200
    #define DEFAULT_EVENT_VIP  500

    // ---------------------------------------------------------------
    // Definition of the extern declared in pos_manager.hpp
    // ---------------------------------------------------------------
    KioskPrices pos_prices;

    // One shared Preferences instance — opened/closed per operation
    // so it is never left open across screen transitions.
    static Preferences prefs;

    // ---------------------------------------------------------------
    // init_pos_manager()
    // Call once in setup().  Reads NVS into pos_prices; if a key is
    // missing (first boot) the default value is written back so the
    // next read always finds a valid entry.
    // ---------------------------------------------------------------
    void init_pos_manager() {
        // true = read-only; we only read here
        if (!prefs.begin(NVS_NAMESPACE, true)) {
            // NVS namespace doesn't exist yet — use defaults and
            // immediately save them so NVS is initialised.
            Serial.println("[POS] NVS namespace not found, writing defaults.");
            pos_prices = {
                DEFAULT_ADULT,
                DEFAULT_CHILD,
                DEFAULT_SENIOR,
                DEFAULT_PARKING,
                DEFAULT_EVENT_GEN,
                DEFAULT_EVENT_VIP
            };
            prefs.end();
            save_pos_prices();   // creates the namespace + all keys
            return;
        }

        pos_prices.adult     = prefs.getUInt(KEY_ADULT,     DEFAULT_ADULT);
        pos_prices.child     = prefs.getUInt(KEY_CHILD,     DEFAULT_CHILD);
        pos_prices.senior    = prefs.getUInt(KEY_SENIOR,    DEFAULT_SENIOR);
        pos_prices.parking   = prefs.getUInt(KEY_PARKING,   DEFAULT_PARKING);
        pos_prices.event_gen = prefs.getUInt(KEY_EVENT_GEN, DEFAULT_EVENT_GEN);
        pos_prices.event_vip = prefs.getUInt(KEY_EVENT_VIP, DEFAULT_EVENT_VIP);

        prefs.end();

        Serial.printf("[POS] Rates loaded — Adult:%lu Child:%lu Senior:%lu "
                    "Park:%lu EvGen:%lu EvVIP:%lu\n",
                    pos_prices.adult, pos_prices.child, pos_prices.senior,
                    pos_prices.parking, pos_prices.event_gen, pos_prices.event_vip);
    }

    // ---------------------------------------------------------------
    // save_pos_prices()
    // Writes the current pos_prices struct back to NVS.
    // Called by the "APPLY NEW RATES" button handler in kiosk_main.
    // ---------------------------------------------------------------
    void save_pos_prices() {
        // false = read-write
        if (!prefs.begin(NVS_NAMESPACE, false)) {
            Serial.println("[POS] ERROR: could not open NVS for writing.");
            return;
        }

        prefs.putUInt(KEY_ADULT,     pos_prices.adult);
        prefs.putUInt(KEY_CHILD,     pos_prices.child);
        prefs.putUInt(KEY_SENIOR,    pos_prices.senior);
        prefs.putUInt(KEY_PARKING,   pos_prices.parking);
        prefs.putUInt(KEY_EVENT_GEN, pos_prices.event_gen);
        prefs.putUInt(KEY_EVENT_VIP, pos_prices.event_vip);

        prefs.end();

        Serial.printf("[POS] Rates saved  — Adult:%lu Child:%lu Senior:%lu "
                    "Park:%lu EvGen:%lu EvVIP:%lu\n",
                    pos_prices.adult, pos_prices.child, pos_prices.senior,
                    pos_prices.parking, pos_prices.event_gen, pos_prices.event_vip);
    }

    // ---------------------------------------------------------------
    // reset_pos_defaults()
    // Clears NVS and reloads factory prices.
    // Handy for a "Factory Reset" menu option if you add one later.
    // ---------------------------------------------------------------
    void reset_pos_defaults() {
        pos_prices = {
            DEFAULT_ADULT,
            DEFAULT_CHILD,
            DEFAULT_SENIOR,
            DEFAULT_PARKING,
            DEFAULT_EVENT_GEN,
            DEFAULT_EVENT_VIP
        };
        save_pos_prices();
        Serial.println("[POS] Factory defaults restored and saved.");
    }