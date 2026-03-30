#include "printer_manager.hpp"
#include <NimBLEDevice.h>
#include <time.h>
#include <SPIFFS.h>

/* ================== CONFIGURATION ================== */
#define PRINTER_CONFIG_FILE "/printer_mac.txt"

String target_printer_mac = ""; 
String scan_results_str = ""; 

static NimBLEUUID serviceUUID("49535343-fe7d-4ae5-8fa9-9fafd205e455");
static NimBLEUUID charUUID("49535343-8841-43f4-a8d4-ecbe34729bb3");

static NimBLEAdvertisedDevice* advDevice = nullptr;
static NimBLEClient* pClient = nullptr;
static NimBLERemoteCharacteristic* pChr = nullptr;

static bool deviceFound = false;
static bool deviceConnected = false;
static bool scanning = false;

/* ================== SCAN CALLBACKS (REAL SCANNING) ================== */

class ScanCallbacks : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
    String found_mac = advertisedDevice->getAddress().toString().c_str();
    String found_name = advertisedDevice->getName().c_str();
    if (found_name == "") found_name = "Unknown Printer";

    // 1. Add to the list for the UI Roller (Real devices, no mock data)
    if (scan_results_str.indexOf(found_mac) == -1) { 
        if (scan_results_str != "") scan_results_str += "\n";
        scan_results_str += found_name + " (" + found_mac + ")";
    }

    // 2. Auto-connect logic
    if (target_printer_mac != "" && found_mac.equalsIgnoreCase(target_printer_mac)) {
      Serial.println("🎯 [PRINTER] Target Found: " + found_mac);
      advDevice = (NimBLEAdvertisedDevice*)advertisedDevice;
      deviceFound = true;
      NimBLEDevice::getScan()->stop();
    }
  }

  void onScanEnd(const NimBLEScanResults& results, int reason) {
    scanning = false;
  }
};

/* ================== CONNECTION LOGIC (STABILITY FIX) ================== */

bool connectToPrinter() {
  Serial.println("⏳ [PRINTER] Attempting Handshake...");
  pClient = NimBLEDevice::createClient();
  pClient->setClientCallbacks(new NimBLEClientCallbacks(), false);
  
  pClient->setConnectionParams(32, 64, 0, 500); 
  pClient->setConnectTimeout(10000);

  if (!pClient->connect(advDevice)) {
    Serial.println("❌ [PRINTER] Connection Failed");
    NimBLEDevice::deleteClient(pClient);
    return false;
  }

  // --- Handshake Stability ---
  delay(1000); 
  pClient->exchangeMTU(); 
  delay(500);

  NimBLERemoteService* pService = pClient->getService(serviceUUID);
  if (!pService) {
    Serial.println("❌ Service Not Found");
    return false;
  }

  pChr = pService->getCharacteristic(charUUID);
  if (!pChr || !pChr->canWrite()) {
    Serial.println("❌ Characteristic Not Found");
    return false;
  }

  deviceConnected = true;
  Serial.println("🔥 [PRINTER] Ready!");
  Serial.println("✅ LET's PRINT!!");
  return true;
}

/* ================== STATE MANAGEMENT ================== */

void force_printer_reconnect() {
  if (pClient && pClient->isConnected()) pClient->disconnect();
  deviceConnected = false;
  deviceFound = false;
  advDevice = nullptr; 
  NimBLEDevice::getScan()->stop();
  scanning = false;
}

void save_printer_mac(String new_mac) {
  new_mac.trim();
  target_printer_mac = new_mac;
  File file = SPIFFS.open(PRINTER_CONFIG_FILE, FILE_WRITE);
  if (file) {
    file.print(target_printer_mac);
    file.close();
  }
  force_printer_reconnect(); 
}

const char* get_ble_scan_results() { return scan_results_str.c_str(); }

void start_ble_scan() {
    scan_results_str = ""; 
    if (!scanning) {
        NimBLEDevice::getScan()->start(0, false);
        scanning = true;
    }
}

/* ================== QR CODE HELPER (CENTERED) ================== */

void send_qr_to_printer(String data) {
  if (!pChr) return;
  byte center_align[] = { 0x1B, 0x61, 0x01 };
  pChr->writeValue(center_align, sizeof(center_align), false);
  byte size[] = { 0x1D, 0x28, 0x6B, 0x03, 0x00, 0x31, 0x43, 0x0A };
  pChr->writeValue(size, sizeof(size), false);
  int store_len = data.length() + 3;
  byte pl = store_len & 0xFF;
  byte ph = (store_len >> 8) & 0xFF;
  byte header[] = { 0x1D, 0x28, 0x6B, pl, ph, 0x31, 0x50, 0x30 };
  pChr->writeValue(header, sizeof(header), false);
  size_t offset = 0;
  while (offset < data.length()) {
    size_t chunk = (data.length() - offset > 20) ? 20 : (data.length() - offset);
    pChr->writeValue((uint8_t*)(data.c_str() + offset), chunk, false);
    offset += chunk;
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  byte print_cmd[] = { 0x1D, 0x28, 0x6B, 0x03, 0x00, 0x31, 0x51, 0x30 };
  pChr->writeValue(print_cmd, sizeof(print_cmd), false);
  byte left_align[] = { 0x1B, 0x61, 0x00 };
  pChr->writeValue(left_align, sizeof(left_align), false);
}

/* ================== CORE TASKS ================== */

void init_printer() {
  if (SPIFFS.exists(PRINTER_CONFIG_FILE)) {
    File file = SPIFFS.open(PRINTER_CONFIG_FILE, FILE_READ);
    if (file) {
      target_printer_mac = file.readStringUntil('\n');
      target_printer_mac.trim();
      file.close();
    }
  }
  NimBLEDevice::init("ESP32-S3-KIOSK");
  NimBLEScan* pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(new ScanCallbacks(), false);
  pScan->setActiveScan(true);
  pScan->start(0, false);
  scanning = true;
}

void handle_printer_loop() {
  if (deviceFound && !deviceConnected) {
    connectToPrinter();
  }
  if (!deviceFound && !scanning && !deviceConnected && target_printer_mac != "") {
    NimBLEDevice::getScan()->start(0, false);
    scanning = true;
  }
}

bool is_printer_connected() { return deviceConnected; }
const char* get_printer_mac() { 
    return (target_printer_mac == "") ? "Not Configured" : target_printer_mac.c_str(); 
}

/* ================== COMPACT TICKET PRINTING ================== */

void print_receipt(const char* order_id, uint32_t amount, const char* bill_type, const char* details) {
  if (!deviceConnected || pChr == nullptr) {
    Serial.println("⚠️ [PRINTER] Offline. Aborted.");
    return;
  }

  struct tm timeinfo;
  char time_str[24];
  if (!getLocalTime(&timeinfo)) strncpy(time_str, "27-03-2026 14:50", 24);
  else strftime(time_str, sizeof(time_str), "%d-%m-%Y %H:%M", &timeinfo);

  char buf[768];
  snprintf(buf, sizeof(buf),
           "--------------------------------\n"
           "          Ticket Wave           \n"
           "--------------------------------\n"
           "Order ID: %s\n"
           "Time:     %s\n"
           "--------------------------------\n"
           "Category: %s\n"
           "   Items               Amount   \n" 
           "%s\n"                                 
           "--------------------------------\n"
           "   TOTAL:            Rs. %5u   \n" 
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
    size_t chunkSize = (length - offset > 20) ? 20 : (length - offset);
    pChr->writeValue((uint8_t*)(buf + offset), chunkSize, false);
    offset += chunkSize;
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  char qr_payload[512];
  snprintf(qr_payload, sizeof(qr_payload),
           "SIGNATURE: BXR-SEC-07\nORDER: %s\nITEMS: %s\nTOTAL: Rs. %u\nVERIFIED AT TICKETWAVE",
           order_id, details, (unsigned int)amount);

  send_qr_to_printer(String(qr_payload));

  byte feed[] = { 0x1B, 0x64, 0x06 };
  pChr->writeValue(feed, sizeof(feed), false);
  Serial.println("🖨️ [PRINTER] Receipt Printed!");
}

void printer_task(void* pvParameters) {
  vTaskDelay(pdMS_TO_TICKS(5000));
  for (;;) {
    handle_printer_loop();
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}