#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include "api.hpp"
#include "wifi_helper.h"
#include "gv.hpp"

// --- 1. INITIATE PAYMENT ---
UpiData fetch_upi_data(uint32_t amount, String order_id, String detail_data_json) {
  // Now matches the 4-argument constructor: status, upi, txid, orderid
  UpiData upi_data(false, "", "", "");
  String pos_name = "";
  String api_key = "";

  // 1. Try to read from SPIFFS
  if (SPIFFS.exists(POS_SETTING_FILE_NAME)) {
    File file = SPIFFS.open(POS_SETTING_FILE_NAME, FILE_READ);
    pos_name = decode_b64(file.readStringUntil('\n'));
    api_key = decode_b64(file.readStringUntil('\n'));
    file.close();

    pos_name.trim();
    api_key.trim();
  }

  // 2. BULLETPROOF FALLBACK: If SPIFFS is empty or failed to decode, use hardcoded keys
  if (api_key == "" || pos_name == "") {
    Serial.println("⚠️ Warning: SPIFFS keys are empty! Using hardcoded fallback.");
    pos_name = "P1";
    api_key = "asdasd324234wfsdrf234wdsfsf";
  }

  // WiFi Check
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ API ERROR: WiFi not connected.");
    return upi_data;
  }

  // Build JSON Payload
  JsonDocument doc;
  doc["ApiKey"] = api_key;
  doc["ApiUsername"] = "bonrix";
  doc["PosName"] = pos_name;
  doc["Amount"] = amount;
  doc["UserTxid"] = order_id;
  doc["PgName"] = "";
  doc["DetailData"] = detail_data_json;

  String json_payload;
  serializeJson(doc, json_payload);

  Serial.println("\n>>> OUTGOING REQUEST TO BONRIX >>>");
  Serial.println(json_payload);

  HTTPClient http;
  http.begin("http://api.kiosk.bonrix.in/api/1.0/android/initpayment");
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(15000);

  int httpResponseCode = http.POST(json_payload);
  String response = http.getString();

  Serial.println("--- SERVER RESPONSE ---");
  Serial.print("HTTP Code: ");
  Serial.println(httpResponseCode);
  Serial.println(response);

  if (httpResponseCode == HTTP_CODE_OK) {
    JsonDocument res_doc;
    DeserializationError error = deserializeJson(res_doc, response);

    if (!error) {
      bool isSuccess = res_doc["isresponse"] | false;
      if (isSuccess && res_doc.containsKey("data") && !res_doc["data"].isNull()) {
        JsonObject data = res_doc["data"];

        if (data.containsKey("upilink") && data["upilink"].as<String>().length() > 0) {
          upi_data.setStatus(true);
          upi_data.setUpiString(data["upilink"].as<String>());
          upi_data.setTransactionId(data["transactionid"].as<String>());
          Serial.println("✅ UPI QR Link Received Successfully.");
        }
      } else {
        String msg = res_doc["message"] | "Unknown Server Error";
        Serial.print("❌ API REJECTED: ");
        Serial.println(msg);
      }
    } else {
      Serial.print("❌ JSON PARSE ERROR: ");
      Serial.println(error.c_str());
    }
  } else {
    Serial.print("❌ HTTP ERROR: ");
    Serial.println(httpResponseCode);
  }

  http.end();
  return upi_data;
}

// --- 2. STATUS CHECK --- kaizz
PaymentStatus check_status(String tx_id) {
  PaymentStatus ps = { false, "", "", 0 };
  String pos_name = "";
  String api_key = "";

  // Try to read from SPIFFS
  if (SPIFFS.exists(POS_SETTING_FILE_NAME)) {
    File file = SPIFFS.open(POS_SETTING_FILE_NAME, FILE_READ);
    pos_name = decode_b64(file.readStringUntil('\n'));
    api_key = decode_b64(file.readStringUntil('\n'));
    file.close();
    api_key.trim();
  }

  // BULLETPROOF FALLBACK
  if (api_key == "") {
    api_key = "asdasd324234wfsdrf234wdsfsf";
  }

  String url = "http://api.kiosk.bonrix.in/api/1.0/android/statuscheck/transactionid";
  url += "?apikey=" + api_key;
  url += "&apiusername=bonrix";
  url += "&txid=" + tx_id;
  url += "&fromLiveStatus=true";

  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(url);
    int code = http.GET();

    if (code == HTTP_CODE_OK) {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, http.getString());
      if (!error && doc["message"] == "success") {
        JsonObject d = doc["data"];
        ps.status = true;
        ps.bank_rrn = d["bankrrn"].as<String>();
        ps.payment_date = d["paymentcompletiondate"].as<String>();
        ps.token_id = d["ticketid"].as<int>();
      }
    }
    http.end();
  }
  return ps;
}

// --- 3. VALIDATE CREDENTIALS --- kaizz
bool validate_api_cred(String api_key, String api_username, String pos_name) {
  String url = "http://api.kiosk.bonrix.in/api/1.0/android/validatepaymentgatewayapicredential";
  url += "?apikey=" + api_key;
  url += "&apiusername=" + api_username;
  url += "&posname=" + pos_name;

  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  bool success = (code == HTTP_CODE_OK);

  if (success) {
    JsonDocument doc;
    deserializeJson(doc, http.getString());
    success = (doc["message"] == "success");
  }

  http.end();
  return success;
}