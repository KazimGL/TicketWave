#include "web_manager.hpp"
#include <WebServer.h>
#include <SPIFFS.h>

// Initialize the web server on port 80
WebServer recoveryServer(80);

// ===============================================================
// 1. HTML & CSS UI TEMPLATES (Raw String Literals)
// ===============================================================

// Main Dashboard UI
const char index_html_template[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>TicketWave Kiosk Admin</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background-color: #f8fafc; color: #334155; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; }
        .container { background: #ffffff; padding: 30px; border-radius: 14px; box-shadow: 0 4px 6px rgba(0,0,0,0.05), 0 10px 15px rgba(0,0,0,0.1); width: 100%; max-width: 400px; text-align: center; box-sizing: border-box; }
        h2 { color: #0f172a; margin-top: 0; font-size: 24px; }
        .pin-display { font-size: 36px; font-weight: bold; color: #e11d48; margin: 10px 0 25px 0; padding: 15px; background: #ffe4e6; border-radius: 10px; letter-spacing: 4px; }
        .form-group { text-align: left; margin-bottom: 20px; }
        label { font-size: 14px; font-weight: 600; color: #64748b; margin-bottom: 8px; display: block; }
        input[type="number"] { width: 100%; padding: 14px; font-size: 18px; border: 2px solid #cbd5e1; border-radius: 10px; box-sizing: border-box; transition: all 0.2s; text-align: center; letter-spacing: 2px;}
        input[type="number"]:focus { outline: none; border-color: #0d9488; box-shadow: 0 0 0 4px rgba(13, 148, 136, 0.15); }
        .btn { width: 100%; padding: 14px; font-size: 16px; font-weight: bold; border: none; border-radius: 10px; cursor: pointer; transition: background-color 0.2s; margin-bottom: 12px; }
        .btn-primary { background-color: #0d9488; color: white; }
        .btn-primary:hover { background-color: #0f766e; }
        .btn-danger { background-color: #ef4444; color: white; }
        .btn-danger:hover { background-color: #dc2626; }
        .footer { margin-top: 20px; font-size: 12px; color: #94a3b8; }
        hr { border: 0; height: 1px; background: #e2e8f0; margin: 25px 0; }
    </style>
</head>
<body>
    <div class="container">
        <h2>⚙️ Kiosk Security</h2>
        <label>Current Admin PIN</label>
        <div class="pin-display">%CURRENT_PIN%</div>
        
        <form action="/set" method="POST">
            <div class="form-group">
                <label for="pin">Set New PIN</label>
                <input type="number" id="pin" name="pin" placeholder="Enter Digits Only" required>
            </div>
            <button type="submit" class="btn btn-primary">Update PIN</button>
        </form>
        
        <hr>
        
        <form action="/reboot" method="POST">
            <button type="submit" class="btn btn-danger" onclick="return confirm('WARNING: Are you sure you want to reboot the Kiosk? This will interrupt any active user sessions.');">🔄 Reboot Kiosk</button>
        </form>
        <div class="footer">TicketWave Web Manager v2.0</div>
    </div>
</body>
</html>
)rawliteral";

// Success Message UI
const char success_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Success</title>
    <style>
        body { font-family: -apple-system, sans-serif; background: #f8fafc; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; text-align: center; }
        .card { background: #fff; padding: 40px; border-radius: 14px; box-shadow: 0 10px 15px rgba(0,0,0,0.1); }
        h1 { color: #0d9488; margin-top: 0; font-size: 32px;}
        p { color: #334155; font-size: 18px; margin-bottom: 30px;}
        a { display: inline-block; padding: 12px 24px; background: #0d9488; color: #fff; text-decoration: none; border-radius: 10px; font-weight: bold; transition: 0.2s;}
        a:hover { background: #0f766e; }
    </style>
</head>
<body>
    <div class="card">
        <h1>✅ Success!</h1>
        <p>The Admin PIN has been updated and saved.</p>
        <a href="/">Return to Dashboard</a>
    </div>
</body>
</html>
)rawliteral";

// Reboot Message UI
const char reboot_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body { font-family: -apple-system, sans-serif; background: #0f172a; color: white; display: flex; justify-content: center; align-items: center; height: 100vh; margin: 0; text-align: center; }
        h2 { color: #38bdf8; }
        p { color: #94a3b8; }
    </style>
</head>
<body>
    <div>
        <h2>🔄 Rebooting Kiosk...</h2>
        <p>The hardware is restarting. Please wait 15 seconds, then refresh this page.</p>
    </div>
</body>
</html>
)rawliteral";

// ===============================================================
// 2. HELPER FUNCTIONS
// ===============================================================

String get_saved_pin() {
  if (SPIFFS.exists("/pin.txt")) {
    File f = SPIFFS.open("/pin.txt", FILE_READ);
    String p = f.readStringUntil('\n');
    p.trim();
    f.close();
    if(p.length() > 0) return p;
  }
  return "NOT SET";
}

void save_new_pin(String new_pin) {
  new_pin.trim();
  File f = SPIFFS.open("/pin.txt", FILE_WRITE);
  if (f) {
    f.print(new_pin);
    f.close();
    Serial.println("🔒 [WEB] PIN Updated via Browser Portal!");
  } else {
    Serial.println("❌ [WEB] Error: Could not open pin.txt for writing.");
  }
}

// ===============================================================
// 3. SERVER INITIALIZATION & ROUTING
// ===============================================================

void init_web_server() {
  // Route 1: The Main Dashboard
  recoveryServer.on("/", HTTP_GET, []() {
    String html = String(index_html_template);
    String currentPin = get_saved_pin();
    html.replace("%CURRENT_PIN%", currentPin); // Injects the live PIN into the HTML
    recoveryServer.send(200, "text/html", html);
  });

  // Route 2: Handle PIN Submission
  recoveryServer.on("/set", HTTP_POST, []() {
    if (recoveryServer.hasArg("pin")) {
      save_new_pin(recoveryServer.arg("pin"));
      recoveryServer.send(200, "text/html", success_html);
    } else {
      recoveryServer.send(400, "text/plain", "Bad Request: Missing PIN parameter.");
    }
  });

  // Route 3: Remote Hardware Reboot
  recoveryServer.on("/reboot", HTTP_POST, []() {
    recoveryServer.send(200, "text/html", reboot_html);
    Serial.println("🔄 [WEB] Remote reboot triggered via Web Admin.");
    delay(1000); // Allow time for the HTTP response to reach the browser
    ESP.restart();
  });

  // Route 4: Handle 404 Not Found
  recoveryServer.onNotFound([]() {
    recoveryServer.send(404, "text/plain", "404: Web Manager Page Not Found");
  });

  // --- SAFE BOOT LOGIC ---
  Serial.println("🌐 [WEB] Activating Network Interface...");
  
  // 1. Force the Wi-Fi into Station mode so the WebServer has a radio to bind to
  WiFi.mode(WIFI_STA); 
  
  // 2. Start the server
  recoveryServer.begin();
  
  // 3. Print a safe success message (No WiFi.localIP() call here to prevent bootloop)
  Serial.println("🌐 [WEB] Local Server Started Successfully on Port 80.");
}

void handle_web_server() {
  recoveryServer.handleClient();
}