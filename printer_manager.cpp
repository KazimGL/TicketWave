#include "printer_manager.hpp"
#include <NimBLEDevice.h>
#include <SPIFFS.h>
#include <time.h>

#define PRINTER_CONFIG_FILE "/printer_mac.txt"
#define SCAN_DURATION_SECS 0  
#define CONNECT_TIMEOUT_MS 10000
#define WATCHDOG_RETRY_MS 3000  

static const NimBLEUUID kServiceUUID("49535343-fe7d-4ae5-8fa9-9fafd205e455");
static const NimBLEUUID kCharUUID("49535343-8841-43f4-a8d4-ecbe34729bb3");

static volatile PrinterState g_state = PrinterState::IDLE;
static String g_target_mac = "";    
static String g_scan_results = "";  

static NimBLEAdvertisedDevice* g_adv_device = nullptr;
static NimBLEClient* g_client = nullptr;
static NimBLERemoteCharacteristic* g_chr = nullptr;

static bool is_valid_mac(const String& mac) {
  if (mac.length() != 17) return false;
  for (int i = 0; i < 17; i++) {
    char c = mac.charAt(i);
    if (i % 3 == 2) {
      if (c != ':') return false;
    } else {
      if (!isxdigit(c)) return false;
    }
  }
  return true;
}

// ------------------------------------------------------------------------
// SCAN CALLBACKS
// ------------------------------------------------------------------------
class PrinterScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* dev) override {
    String mac = dev->getAddress().toString().c_str();
    String name = dev->getName().c_str();
    
    // If the device has no name, give it a placeholder
    if (name.isEmpty()) name = "Unknown Device";

    // --- Auto-connect path: target MAC matched ---
    if (g_target_mac != "" && mac.equalsIgnoreCase(g_target_mac) && g_state == PrinterState::SCANNING) {
      Serial.printf("[PRINTER] Target found: %s\n", mac.c_str());
      g_adv_device = (NimBLEAdvertisedDevice*)dev;
      g_state = PrinterState::CONNECTING;
      NimBLEDevice::getScan()->stop();
      return;
    }

    // 👉 THE HEURISTIC FILTER HAS BEEN REMOVED HERE 👈
    // Now EVERY discovered BLE device will be added to the UI list.

    // Deduplicate by MAC before appending to the UI list string
    if (g_scan_results.indexOf(mac) == -1) {
      if (g_scan_results.length() > 0) g_scan_results += "\n";
      g_scan_results += name + " (" + mac + ")";
      Serial.printf("[BLE SCAN] Device found: %s  %s\n", name.c_str(), mac.c_str());
    }
  }

  void onScanEnd(const NimBLEScanResults& /*results*/, int /*reason*/) override {
    Serial.println("[PRINTER] Scan ended.");
  }
};

static PrinterScanCallbacks g_scan_cbs;

// ------------------------------------------------------------------------
// CONNECTION LOGIC
// ------------------------------------------------------------------------
static bool connect_to_printer() {
  if (!g_adv_device) return false;

  Serial.println("[PRINTER] Initiating handshake...");

  g_client = NimBLEDevice::createClient();
  g_client->setConnectionParams(8, 16, 0, 500);
  g_client->setConnectTimeout(CONNECT_TIMEOUT_MS);

  if (!g_client->connect(g_adv_device)) {
    Serial.println("[PRINTER] ❌ Connection failed.");
    NimBLEDevice::deleteClient(g_client);
    g_client = nullptr;
    return false;
  }

  delay(500);               
  g_client->exchangeMTU();  
  delay(300);

  NimBLERemoteService* svc = g_client->getService(kServiceUUID);
  if (!svc) {
    Serial.println("[PRINTER] ❌ Service not found.");
    g_client->disconnect();
    NimBLEDevice::deleteClient(g_client);
    g_client = nullptr;
    return false;
  }

  g_chr = svc->getCharacteristic(kCharUUID);
  if (!g_chr || !g_chr->canWrite()) {
    Serial.println("[PRINTER] ❌ Writable characteristic not found.");
    g_client->disconnect();
    NimBLEDevice::deleteClient(g_client);
    g_client = nullptr;
    g_chr = nullptr;
    return false;
  }

  Serial.printf("[PRINTER] ✅ Ready! MTU=%d\n", g_client->getMTU());
  return true;
}

static void transition_to_idle() {
  g_state = PrinterState::IDLE;
  g_adv_device = nullptr;
}

static void transition_to_scanning() {
  g_adv_device = nullptr;
  g_state = PrinterState::SCANNING;
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (!scan->isScanning()) {
    scan->start(SCAN_DURATION_SECS, false);
    Serial.println("[PRINTER] BLE scan (re)started.");
  }
}

static void disconnect_client() {
  if (g_client) {
    if (g_client->isConnected()) g_client->disconnect();
    NimBLEDevice::deleteClient(g_client);
    g_client = nullptr;
  }
  g_chr = nullptr;
  g_adv_device = nullptr;
}

// ------------------------------------------------------------------------
// PUBLIC API
// ------------------------------------------------------------------------
void init_printer() {
  if (SPIFFS.exists(PRINTER_CONFIG_FILE)) {
    File f = SPIFFS.open(PRINTER_CONFIG_FILE, FILE_READ);
    if (f) {
      g_target_mac = f.readStringUntil('\n');
      g_target_mac.trim();
      f.close();
      Serial.printf("[PRINTER] Loaded MAC: %s\n", g_target_mac.c_str());
    }
  }

  NimBLEDevice::init("ESP32-S3-KIOSK");
  NimBLEDevice::setMTU(512);               
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);  

  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&g_scan_cbs, false);
  scan->setActiveScan(true);
  scan->setInterval(97);  
  scan->setWindow(48);

  if (g_target_mac != "") {
    transition_to_scanning();
  } else {
    transition_to_idle();
  }
}

void force_printer_reconnect() {
  Serial.println("[PRINTER] Force reconnect requested.");
  NimBLEDevice::getScan()->stop();
  disconnect_client();

  if (g_target_mac != "") {
    transition_to_scanning();
  } else {
    transition_to_idle();
  }
}

void save_printer_mac(String new_mac) {
  new_mac.trim();
  new_mac.toUpperCase();

  if (!is_valid_mac(new_mac)) {
    Serial.printf("[PRINTER] ⚠️ Invalid MAC format rejected: '%s'\n", new_mac.c_str());
    return;
  }

  g_target_mac = new_mac;

  File f = SPIFFS.open(PRINTER_CONFIG_FILE, FILE_WRITE);
  if (f) {
    f.print(g_target_mac);
    f.close();
    Serial.printf("[PRINTER] MAC saved: %s\n", g_target_mac.c_str());
  } else {
    Serial.println("[PRINTER] ❌ Could not write MAC to SPIFFS.");
  }

  force_printer_reconnect();  
}

void start_ble_scan() {
  g_scan_results = "";  
  NimBLEDevice::getScan()->stop();
  vTaskDelay(pdMS_TO_TICKS(200));
  NimBLEDevice::getScan()->start(SCAN_DURATION_SECS, false);
  Serial.println("[PRINTER] UI scan started.");
}

const char* get_ble_scan_results() {
  return g_scan_results.c_str();
}

bool is_printer_connected() {
  return (g_state == PrinterState::READY) && (g_client != nullptr) && g_client->isConnected();
}

const char* get_printer_mac() {
  return (g_target_mac == "") ? "Not Configured" : g_target_mac.c_str();
}

PrinterState get_printer_state() {
  return g_state;
}

const char* get_printer_state_str() {
  switch (g_state) {
    case PrinterState::IDLE: return "Idle";
    case PrinterState::SCANNING: return "Scanning";
    case PrinterState::CONNECTING: return "Connecting";
    case PrinterState::READY: return "Ready";
    case PrinterState::ERROR: return "Error";
  }
  return "Unknown";
}

void printer_task(void* /*pvParameters*/) {
  vTaskDelay(pdMS_TO_TICKS(3000));  

  for (;;) {
    switch (g_state) {
      case PrinterState::IDLE:
        break;

      case PrinterState::SCANNING:
        if (g_target_mac != "" && !NimBLEDevice::getScan()->isScanning()) {
          Serial.println("[PRINTER] Scan stopped unexpectedly — restarting.");
          NimBLEDevice::getScan()->start(SCAN_DURATION_SECS, false);
        }
        break;

      case PrinterState::CONNECTING:
        NimBLEDevice::getScan()->stop();
        if (connect_to_printer()) {
          g_state = PrinterState::READY;
        } else {
          g_state = PrinterState::ERROR;
        }
        break;

      case PrinterState::READY:
        if (!g_client || !g_client->isConnected()) {
          Serial.println("[PRINTER] Link lost — reconnecting.");
          disconnect_client();
          g_state = PrinterState::ERROR;
        }
        break;

      case PrinterState::ERROR:
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_RETRY_MS));
        Serial.println("[PRINTER] Watchdog retry.");
        if (g_target_mac != "") {
          transition_to_scanning();
        } else {
          transition_to_idle();
        }
        break;
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

static void send_qr_to_printer(const String& data) {
  if (!g_chr) return;

  byte center_align[] = { 0x1B, 0x61, 0x01 };
  g_chr->writeValue(center_align, sizeof(center_align), false);

  byte size[] = { 0x1D, 0x28, 0x6B, 0x03, 0x00, 0x31, 0x43, 0x0A };
  g_chr->writeValue(size, sizeof(size), false);

  int store_len = data.length() + 3;
  byte pl = store_len & 0xFF;
  byte ph = (store_len >> 8) & 0xFF;
  byte header[] = { 0x1D, 0x28, 0x6B, pl, ph, 0x31, 0x50, 0x30 };
  g_chr->writeValue(header, sizeof(header), false);

  size_t offset = 0;
  while (offset < data.length()) {
    size_t chunk = min((size_t)20, data.length() - offset);
    g_chr->writeValue((uint8_t*)(data.c_str() + offset), chunk, false);
    offset += chunk;
    vTaskDelay(pdMS_TO_TICKS(20));
  }

  byte print_cmd[] = { 0x1D, 0x28, 0x6B, 0x03, 0x00, 0x31, 0x51, 0x30 };
  g_chr->writeValue(print_cmd, sizeof(print_cmd), false);

  byte left_align[] = { 0x1B, 0x61, 0x00 };
  g_chr->writeValue(left_align, sizeof(left_align), false);
}

void print_receipt(const char* order_id,
                   uint32_t    amount,
                   const char* bill_type,
                   const char* details) {
  if (!is_printer_connected() || g_chr == nullptr) {
    Serial.println("[PRINTER] ⚠️ Offline — receipt aborted.");
    return;
  }

  struct tm timeinfo;
  char time_str[24];
  if (!getLocalTime(&timeinfo)) {
    strncpy(time_str, "N/A", 24);
  } else {
    strftime(time_str, sizeof(time_str), "%d-%m-%Y %H:%M", &timeinfo);
  }

  char buf[768];
  snprintf(buf, sizeof(buf),
           "--------------------------------\n"
           "          Ticket Wave           \n"
           "--------------------------------\n"
           "Order ID: %s\n"
           "Time:     %s\n"
           "--------------------------------\n"
           "Category: %s\n"
           "  Items              Amount   \n"
           "%s\n"
           "--------------------------------\n"
           "  TOTAL:            Rs. %5u   \n"
           "--------------------------------\n"
           "Status:   PAID (SUCCESS)\n"
           "--------------------------------\n"
           "    Enjoy your visit! Have a    \n"
           "      wonderful day ahead.      \n"
           "--------------------------------\n\n\n",
           order_id, time_str, bill_type, details, (unsigned int)amount);

  size_t length = strlen(buf);
  size_t offset = 0;
  while (offset < length) {
    size_t chunk = min((size_t)20, length - offset);
    g_chr->writeValue((uint8_t*)(buf + offset), chunk, false);
    offset += chunk;
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  char qr_payload[512];
  snprintf(qr_payload, sizeof(qr_payload),
           "SIGNATURE: BXR-SEC-07\nORDER: %s\nITEMS: %s\nTOTAL: Rs. %u\nVERIFIED AT TICKETWAVE",
           order_id, details, (unsigned int)amount);

  send_qr_to_printer(String(qr_payload));

  byte feed[] = { 0x1B, 0x64, 0x06 };
  g_chr->writeValue(feed, sizeof(feed), false);

  Serial.println("[PRINTER] 🖨️ Receipt printed.");
}
