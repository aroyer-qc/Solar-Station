#include "WebServerModule.h"
#include <SPIFFS.h>
#include <WiFi.h>
#include "ConfigModule.h"

// #define USE_DEBUG_WEB_SERVER

extern ConfigModule        Config;
extern AzimuthController   azimuthController;
extern ElevationController elevationController;
extern float 	GetLoad1CurrentA					    ();
extern float 	GetLoad2CurrentA					    ();
extern uint16_t GetLoad1AdcRaw                          ();
extern uint16_t GetLoad2AdcRaw                          ();
extern float 	GetAmbientLightLux					    ();
extern float 	GetPanelVoltage					        ();
extern float 	GetChargingCurrent				        ();
extern float 	GetBatterySoc					        ();
extern float 	GetBatteryVoltage				        ();
extern float 	GetPanelCurrent					        ();
extern float 	GetPanelChargingPower			        ();
extern String 	GetMpptLinkStatusSummary			    ();
extern bool 	SetMosfetOutput						    (uint8_t OutputIndex, bool IsOn);
extern bool 	ToggleMosfetOutput					    (uint8_t OutputIndex);
extern bool 	SetMosfetPwmPercent					    (uint8_t OutputIndex, uint8_t DutyPercent);
extern uint8_t 	GetMosfetPwmPercent				    	(uint8_t OutputIndex);
extern bool 	IsMosfetOutputOn					    (uint8_t OutputIndex);
extern bool 	IsOutputAutomaticMode				    (uint8_t OutputIndex);
extern void 	ApplyOutputSchedules			    	();
extern String 	GetOutputScheduleSummary		    	(uint8_t outputIndex);
extern bool 	HasOutputScheduleConflict               (uint8_t outputIndex);
extern String 	GetOutputControlStatus				    (uint8_t outputIndex);
extern String 	GetOutputNextEventTimeSummary		    (uint8_t outputIndex);
extern String 	GetOutputNextEventReasonSummary	        (uint8_t outputIndex);
extern String 	GetLocalDateTimeSummary				    ();
extern String 	GetNtpSyncStatusSummary				    ();
extern bool 	IsSystemTimeValid					    ();
extern String 	GetCalculatedAzimuthSummary			    ();
extern String 	GetCalculatedElevationSummary		    ();
extern String 	GetLimitSwitchSummary				    ();
extern String 	GetStepperDiagSummary				    ();
extern String 	GetLimitSwitchStateLabel			    (uint8_t limitIndex);
extern String 	GetStepperDiagStateLabel			    (uint8_t diagIndex);
extern String 	GetTodaySunriseSummary				    ();
extern String 	GetTodaySunsetSummary				    ();
extern String 	GetTodayAzimuthMinSummary			    ();
extern String 	GetTodayAzimuthMaxSummary			    ();
extern String 	GetTodayElevationMaxSummary			    ();
extern bool 	SetTrackingTestOverride				    (float targetAzimuth, float targetElevation);
extern bool 	JogMotorDirect						    (const char* axis, int8_t direction, float incrementDegrees);
extern bool 	CancelTrackingTestOverrideAndReturn	    ();
extern bool 	IsTrackingTestOverrideActive		    ();
extern String 	GetTrackingOverrideStatusSummary	    ();
extern String 	GetTrackingOverrideLastFailureSummary	();
extern String 	GetSensorLogsManifestJson			    ();
extern String 	GetStorageSelfTestReport			    ();
extern bool 	GetSensorLogFileInfo				    (const String& requestedName, uint32_t& outSizeBytes);
extern bool 	ReadSensorLogFileRange				    (const String& requestedName, uint32_t offsetBytes, uint8_t* pBuffer, size_t lengthBytes);
extern void 	RequestMicrostepConfigApply		        ();

static String GetOutputStatusClass(const String& Status)
{
    if(Status == "AUTO ACTIVE")
    {
        return "badge badge-auto-active";
    }

    if(Status == "AUTO IDLE")
    {
        return "badge badge-auto-idle";
    }

    if(Status == "AUTO WAIT NTP" || Status == "AUTO WAIT SUN")
    {
        return "badge badge-auto-wait";
    }

    if(Status == "AUTO DISABLED")
    {
        return "badge badge-auto-disabled";
    }

    if(Status == "AUTO CONFLICT")
    {
        return "badge badge-signal-fault";
    }

    return "badge badge-manual";
}

static String GetSignalStatusClass(const String& Status)
{
    if(Status == "FAULT")
    {
        return "badge badge-signal-fault";
    }

    if(Status == "ACTIVE")
    {
        return "badge badge-signal-active";
    }

    if(Status == "OK")
    {
        return "badge badge-signal-ok";
    }

    return "badge badge-signal-idle";
}

static String JsonEscape(const String& input)
{
    String output;
    output.reserve(input.length() + 8);

    for(size_t i = 0; i < input.length(); i++)
    {
        char c = input[i];
        if(c == '\\' || c == '"')
        {
            output += '\\';
            output += c;
            continue;
        }

        if(c == '\n')
        {
            output += "\\n";
            continue;
        }

        if(c == '\r')
        {
            output += "\\r";
            continue;
        }

        if(c == '\t')
        {
            output += "\\t";
            continue;
        }

        output += c;
    }

    return output;
}

static String HtmlEscape(const String& input)
{
    String output;
    output.reserve(input.length() + 8);

    for(size_t i = 0; i < input.length(); i++)
    {
        char c = input[i];
        switch(c)
        {
            case '&':   output += "&amp;";  break;
            case '<':   output += "&lt;";   break;
            case '>':   output += "&gt;";   break;
            case '"':   output += "&quot;"; break;
            case '\'':  output += "&#39;";  break;
            default:    output += c;        break;
        }
    }

    return output;
}

String WebServerModule::getWifiModeLabel()
{
    switch(WiFi.getMode())
    {
        case WIFI_STA:      return "STA";
        case WIFI_AP:       return "AP";
        case WIFI_AP_STA:   return "AP + STA";
        default:			return "OFF";
    }
}

String WebServerModule::getWifiConnectionStatusLabel()
{
    if((WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) && !WiFi.isConnected())
    {
        return "ACCESS POINT ACTIVE";
    }

    wl_status_t status = WiFi.status();
    switch(status)
    {
        case WL_CONNECTED:          return "CONNECTED";
        case WL_IDLE_STATUS:        return "IDLE";
        case WL_NO_SSID_AVAIL:      return "SSID NOT FOUND";
        case WL_SCAN_COMPLETED:     return "SCAN COMPLETED";
        case WL_CONNECT_FAILED:     return "AUTH FAILED";
        case WL_CONNECTION_LOST:    return "CONNECTION LOST";
        case WL_DISCONNECTED:       return "DISCONNECTED";
        default:		            return String("STATUS ") + String((int)status);
    }
}

String WebServerModule::getWifiDisplayValue(const char* value, bool automaticWhenDHCP) const
{
    if(automaticWhenDHCP && Config.GetWIFI_UseDHCP())
    {
        return "Automatic (DHCP)";
    }

    if(value == nullptr || strlen(value) == 0)
    {
        return "Not set";
    }

    return String(value);
}

String WebServerModule::getCurrentWifiRssi()
{
    if(WiFi.isConnected())
    {
        return String((int)WiFi.RSSI()) + " dBm";
    }

    return "Not connected";
}

String WebServerModule::getCurrentWifiIp()
{
    if(WiFi.isConnected())
    {
        return WiFi.localIP().toString();
    }

    if(WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA)
    {
        return WiFi.softAPIP().toString() + " (AP)";
    }

    return "Not connected";
}

String WebServerModule::getCurrentWifiGateway()
{
    if(WiFi.isConnected())
    {
        return WiFi.gatewayIP().toString();
    }

    if(WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA)
    {
        return WiFi.softAPIP().toString() + " (AP)";
    }

    return "Not connected";
}

String WebServerModule::getCurrentWifiSubnet()
{
    if(WiFi.isConnected())
    {
        return WiFi.subnetMask().toString();
    }

    if(WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA)
    {
        return "255.255.255.0 (AP default)";
    }

    return "Not connected";
}

String WebServerModule::getCurrentWifiDns()
{
    if(WiFi.isConnected())
    {
        IPAddress dnsIp = WiFi.dnsIP();
        if(dnsIp == IPAddress((uint32_t)0u))
        {
            return "Not provided";
        }

        return dnsIp.toString();
    }

    if(WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA)
    {
        return "Not available in AP mode";
    }

    return "Not connected";
}

WebServerModule::WebServerModule()
    : server(80)
{
}

void WebServerModule::begin()
{
    if(serverStarted)
    {
        return;
    }

    initSPIFFS();
    setupServer();
    server.begin();
    serverStarted = true;
  #ifdef USE_DEBUG_WEB_SERVER
    Serial.println("[Web] Web server started (sync mode).");
  #endif
}

void WebServerModule::Loop()
{
    if(serverStarted == false)
    {
        return;
    }

    server.handleClient();
}

void WebServerModule::initSPIFFS()
{
    if(!SPIFFS.begin(true))
    {
	  #ifdef USE_DEBUG_WEB_SERVER
        Serial.println("[Web] SPIFFS mount failed.");
	  #endif	
    }
    else
    {
	  #ifdef USE_DEBUG_WEB_SERVER
        Serial.println("SPIFFS mounted successfully");
	  #endif	
    }
}

void WebServerModule::setupServer()
{
    const char* headerKeys[] = {"User-Agent"};
    server.collectHeaders(headerKeys, 1);

    server.on("/", HTTP_GET, [this]() { this->handleConfigPage(); });
    server.on("/portal", HTTP_GET, [this]() { this->handleConfigPage(); });
    server.on("/portal/", HTTP_GET, [this]() { this->handleConfigPage(); });
    server.on("/portal/config", HTTP_GET, [this]() { this->handleConfigPage(); });
    server.on("/portal/config/", HTTP_GET, [this]() { this->handleConfigPage(); });
    server.on("/portal/config.html", HTTP_GET, [this]() { this->handleConfigPage(); });
    server.on("/portal/network", HTTP_GET, [this]() { this->handleConfigPage(); });
    server.on("/portal/network/", HTTP_GET, [this]() { this->handleConfigPage(); });
    server.on("/portal/network.html", HTTP_GET, [this]() { this->handleConfigPage(); });
    server.on("/portal/outputs", HTTP_GET, [this]() { this->handleConfigPage(); });
    server.on("/portal/outputs/", HTTP_GET, [this]() { this->handleConfigPage(); });
    server.on("/portal/outputs.html", HTTP_GET, [this]() { this->handleConfigPage(); });
    server.on("/portal/diag", HTTP_GET, [this]() { this->handleDiagnosticsPage(); });
    server.on("/portal/diag/", HTTP_GET, [this]() { this->handleDiagnosticsPage(); });
    server.on("/portal/logs", HTTP_GET, [this]() { this->handleSensorLogsPage(); });
    server.on("/portal/logs/", HTTP_GET, [this]() { this->handleSensorLogsPage(); });
    server.on("/portal/logs.html", HTTP_GET, [this]() { this->handleSensorLogsPage(); });
    server.on("/portal/logs.js", HTTP_GET, [this]() {
        File file = SPIFFS.open("/portal/logs.js", "r");
        if(!file)
        {
            send404("portal/logs.js not found");
            return;
        }
        server.streamFile(file, "application/javascript");
        file.close();
    });
    server.on("/portal/app.js", HTTP_GET, [this]() {
        File file = SPIFFS.open("/portal/app.js", "r");
        if(!file)
        {
            send404("portal/app.js not found");
            return;
        }
        server.streamFile(file, "application/javascript");
        file.close();
    });

    server.on("/console", HTTP_GET, [this]() {
        File file = SPIFFS.open("/console/index.html", "r");
        if(!file)
        {
			send404("console/index.html not found");
            return;
        }
        server.streamFile(file, "text/html");
        file.close();
    });
    server.on("/console/", HTTP_GET, [this]() {
        File file = SPIFFS.open("/console/index.html", "r");
        if(!file)
        {
            send404("console/index.html not found");
            return;
        }
        server.streamFile(file, "text/html");
        file.close();
    });
    server.on("/console/index.html", HTTP_GET, [this]() {
        File file = SPIFFS.open("/console/index.html", "r");
        if(!file)
        {
            send404("console/index.html not found");
            return;
        }
        server.streamFile(file, "text/html");
        file.close();
    });
    server.on("/console/styles.css", HTTP_GET, [this]() {
        File file = SPIFFS.open("/console/styles.css", "r");
        if(!file)
        {
            send404("console/styles.css not found");
            return;
        }
        server.streamFile(file, "text/css");
        file.close();
    });
    server.on("/console/app.js", HTTP_GET, [this]() {
        File file = SPIFFS.open("/console/app.js", "r");
        if(!file)
        {
            send404("console/app.js not found");
            return;
        }
        server.streamFile(file, "application/javascript");
        file.close();
    });

    server.on("/diag",        				HTTP_GET, [this]() { this->handleDiagnosticsPage(); });
    server.on("/diag/",       				HTTP_GET, [this]() { this->handleDiagnosticsPage(); });
    server.on("/Diag",        				HTTP_GET, [this]() { this->handleDiagnosticsPage(); });
    server.on("/Diag/",       				HTTP_GET, [this]() { this->handleDiagnosticsPage(); });
    server.on("/diag/status", 				HTTP_GET, [this]() { this->HandleGetTrackingOverrideStatus(); });
    server.on("/Diag/status", 				HTTP_GET, [this]() { this->HandleGetTrackingOverrideStatus(); });
    server.on("/portal/status", 			HTTP_GET, [this]() { this->HandleGetPortalStatus(); });
    server.on("/portal/status/", 		HTTP_GET, [this]() { this->HandleGetPortalStatus(); });

    server.on("/api/logs",    	    		HTTP_GET, [this]() { this->HandleGetSensorLogsManifest(); });
    server.on("/api/storage/test",			HTTP_GET, [this]() { this->HandleStorageSelfTest(); });
    server.on("/api/logs/download", 		HTTP_GET, [this]() { this->HandleDownloadSensorLog(); });
    server.on("/api/config", 				HTTP_GET, [this]() { this->HandleGetApiConfig(); });
    server.on("/api/config", 				HTTP_POST, [this]() { this->HandleSaveApiConfig(); });
    server.on("/api/status", 				HTTP_GET, [this]() { this->HandleGetApiStatus(); });
    server.on("/api/output", 				HTTP_POST, [this]() { this->HandleApiOutput(); });

    server.on("/api/restart", 				HTTP_POST, [this]() { this->handleRestartSystem(); });
    server.on("/restartSystem", 			HTTP_POST, [this]() { this->handleRestartSystem(); });

    server.on("/saveSolarTrackingConfig", 	HTTP_POST, [this]() { this->HandleSaveSolarTrackingConfig(); });
    server.on("/resetSolarTrackingConfig", 	HTTP_POST, [this]() { this->HandleResetSolarTrackingConfig(); });
    server.on("/saveAzimuthConfig", 		HTTP_POST, [this]() { this->HandleSaveAzimuthConfig(); });
    server.on("/resetAzimuthConfig", 		HTTP_POST, [this]() { this->HandleResetAzimuthConfig(); });
    server.on("/saveElevationConfig", 		HTTP_POST, [this]() { this->HandleSaveElevationConfig(); });
    server.on("/resetElevationConfig", 		HTTP_POST, [this]() { this->HandleResetElevationConfig(); });
    server.on("/saveStepperConfig", 		HTTP_POST, [this]() { this->HandleSaveStepperConfig(); });
    server.on("/resetStepperConfig", 		HTTP_POST, [this]() { this->HandleResetStepperConfig(); });
    server.on("/saveWiFiConfig", 			HTTP_POST, [this]() { this->HandleSaveWiFiConfig(); });
    server.on("/resetWiFiConfig", 			HTTP_POST, [this]() { this->HandleResetWiFiConfig(); });
    server.on("/saveNTPConfig", 			HTTP_POST, [this]() { this->HandleSaveNTPConfig(); });
    server.on("/resetNTPConfig", 			HTTP_POST, [this]() { this->HandleResetNTPConfig(); });
    server.on("/setOutputControl", 			HTTP_POST, [this]() { this->HandleSetOutputControl(); });
    server.on("/saveOutputNames", 			HTTP_POST, [this]() { this->HandleSaveOutputNames(); });
    server.on("/saveScheduleConfig", 		HTTP_POST, [this]() { this->HandleSaveScheduleConfig(); });
    server.on("/resetScheduleConfig", 		HTTP_POST, [this]() { this->HandleResetScheduleConfig(); });
    server.on("/setTrackingOverride", 		HTTP_POST, [this]() { this->HandleSetTrackingOverride(); });
    server.on("/cancelTrackingOverride", 	HTTP_POST, [this]() { this->HandleCancelTrackingOverride(); });
    server.on("/jogTracking", 				HTTP_POST, [this]() { this->HandleJogTracking(); });
    server.on("/getJogSessionDelta", 		HTTP_GET, [this]() { this->HandleGetJogSessionDelta(); });

    server.onNotFound([this]()
	{
		if (!handleFileRead(server.uri()))
		{
			HandleNotFound();
		}
	});
}

String WebServerModule::formatOutputState(uint8_t outputIndex)
{
    return IsMosfetOutputOn(outputIndex) ? "ON" : "OFF";
}

String WebServerModule::selectTemplatePath(const char* desktopPath)
{
    return String(desktopPath);
}

void WebServerModule::sendEmbeddedConfigFallbackPage()
{
    String html;
    html.reserve(1700);
    html += "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>SolarStation Fallback</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:18px;background:#f3f5f7;color:#222;}";
    html += ".card{max-width:760px;margin:auto;background:#fff;border:1px solid #ccd4dc;border-radius:10px;padding:16px;}";
    html += "h1{margin-top:0;font-size:1.35rem;} .row{margin:8px 0;} .muted{color:#5f6b77;}";
    html += "a,button{display:inline-block;margin-right:8px;margin-top:8px;padding:10px 12px;border-radius:8px;border:0;background:#0b7a75;color:#fff;text-decoration:none;}";
    html += "button.warn{background:#b65f32;} pre{white-space:pre-wrap;background:#f6f8fa;border:1px solid #e4e8ec;padding:10px;border-radius:8px;}";
    html += "</style></head><body><div class='card'>";
    html += "<h1>SolarStation Emergency Page</h1>";
    html += "<div class='row muted'>SPIFFS template unavailable or empty. This fallback is served from firmware.</div>";
    html += "<div class='row'><strong>Time:</strong> " + GetLocalDateTimeSummary() + "</div>";
    html += "<div class='row'><strong>NTP:</strong> " + GetNtpSyncStatusSummary() + "</div>";
    html += "<div class='row'><strong>WiFi SSID:</strong> " + String(Config.GetWIFI_SSID()) + "</div>";
    html += "<div class='row'><strong>IP:</strong> " + WiFi.softAPIP().toString() + "</div>";
    html += "<div class='row'><strong>Tracking:</strong> " + GetTrackingOverrideStatusSummary() + "</div>";
    html += "<div class='row'><a href='/portal/diag'>Open portal diagnostics</a><a href='/console'>Open console</a><a href='/diag/status'>Raw diag status</a></div>";
    html += "<div class='row'><button class='warn' onclick=\"fetch('/restartSystem',{method:'POST'}).then(()=>alert('Restart requested')).catch(()=>alert('Restart failed'));\">Restart device</button></div>";
    html += "<pre>Action required: re-upload filesystem from data folder.</pre>";
    html += "</div></body></html>";

    server.send(200, "text/html", html);
}

void WebServerModule::sendEmbeddedDiagnosticsFallbackPage()
{
    String html;
    html.reserve(2200);
    html += "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    html += "<title>SolarStation Diag Fallback</title>";
    html += "<style>body{font-family:Arial,sans-serif;margin:18px;background:#f3f5f7;color:#222;}";
    html += ".card{max-width:760px;margin:auto;background:#fff;border:1px solid #ccd4dc;border-radius:10px;padding:16px;}";
    html += "h1{margin-top:0;font-size:1.35rem;} .row{margin:8px 0;} .muted{color:#5f6b77;}";
    html += "input{padding:8px;border:1px solid #c7d0da;border-radius:8px;width:120px;}";
    html += "button,a{display:inline-block;margin-right:8px;margin-top:8px;padding:10px 12px;border-radius:8px;border:0;background:#0b7a75;color:#fff;text-decoration:none;cursor:pointer;}";
    html += "button.warn{background:#b65f32;} pre{white-space:pre-wrap;background:#f6f8fa;border:1px solid #e4e8ec;padding:10px;border-radius:8px;}";
    html += "</style></head><body><div class='card'>";
    html += "<h1>Diagnostics Fallback Page</h1>";
    html += "<div class='row muted'>SPIFFS diag template unavailable. This page is firmware embedded.</div>";
    html += "<div class='row'><strong>Time:</strong> <span id='timeVal'>" + GetLocalDateTimeSummary() + "</span></div>";
    html += "<div class='row'><strong>Override:</strong> <span id='overrideVal'>" + GetTrackingOverrideStatusSummary() + "</span></div>";
    html += "<div class='row'>";
    html += "Azimuth <input id='azimuth' type='number' step='0.1' value='180'> ";
    html += "Elevation <input id='elevation' type='number' step='0.1' value='30'>";
    html += "</div>";
    html += "<div class='row'><button onclick='applyOverride()'>Apply Override</button><button class='warn' onclick='cancelOverride()'>Cancel Override</button><a href='/portal'>Back to portal</a><a href='/console'>Open console</a></div>";
    html += "<pre id='result'>Ready</pre>";
    html += "</div><script>";
    html += "async function refresh(){try{const r=await fetch('/diag/status',{cache:'no-store'});if(!r.ok)return;const t=await r.text();";
    html += "const l=t.split('\\n');const d={};l.forEach(x=>{const i=x.indexOf('=');if(i>0)d[x.substring(0,i).trim()]=x.substring(i+1).trim();});";
    html += "if(d.time)document.getElementById('timeVal').textContent=d.time;";
    html += "if(d.override)document.getElementById('overrideVal').textContent=d.override;";
    html += "}catch(e){}}";
    html += "async function applyOverride(){const a=document.getElementById('azimuth').value;const e=document.getElementById('elevation').value;";
    html += "const b=new URLSearchParams({azimuth:a,elevation:e});const r=await fetch('/setTrackingOverride',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:b});";
    html += "document.getElementById('result').textContent=await r.text();refresh();}";
    html += "async function cancelOverride(){const r=await fetch('/cancelTrackingOverride',{method:'POST'});document.getElementById('result').textContent=await r.text();refresh();}";
    html += "setInterval(refresh,2000);refresh();";
    html += "</script></body></html>";

    server.send(200, "text/html", html);
}

void WebServerModule::handleConfigPage()
{
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");

    String templatePath = "/portal/index.html";
    String requestUri = server.uri();
    if(requestUri == "/portal/config" || requestUri == "/portal/config/" || requestUri == "/portal/config.html")
    {
        templatePath = "/portal/config.html";
    }
    else if(requestUri == "/portal/network" || requestUri == "/portal/network/" || requestUri == "/portal/network.html")
    {
        templatePath = "/portal/network.html";
    }
    else if(requestUri == "/portal/outputs" || requestUri == "/portal/outputs/" || requestUri == "/portal/outputs.html")
    {
        templatePath = "/portal/outputs.html";
    }
    templatePath = selectTemplatePath(templatePath.c_str());
    File file = SPIFFS.open(templatePath, "r");

    if(!file && templatePath != "/portal/index.html")
    {
        Serial.printf("[Web] %s missing, fallback to /portal/index.html\n", templatePath.c_str());
        templatePath = "/portal/index.html";
        file = SPIFFS.open(templatePath, "r");
    }

    if(file && file.size() == 0 && templatePath != "/portal/index.html")
    {
        Serial.printf("[Web] %s is empty, fallback to /portal/index.html\n", templatePath.c_str());
        file.close();
        templatePath = "/portal/index.html";
        file = SPIFFS.open(templatePath, "r");
    }

    if(!file)
    {
        Serial.printf("[Web] GET / failed: %s not found in SPIFFS\n", templatePath.c_str());
        sendEmbeddedConfigFallbackPage();
        return;
    }

    if(file.size() == 0)
    {
        Serial.printf("[Web] GET / failed: %s is empty. Serving embedded fallback.\n", templatePath.c_str());
        file.close();
        sendEmbeddedConfigFallbackPage();
        return;
    }

    size_t fileSize = file.size();
    String html;
    html.reserve(fileSize + 1024u);
    char readBuffer[512];
    while(file.available())
    {
        size_t bytesToRead = file.available();
        if(bytesToRead > sizeof(readBuffer))
        {
            bytesToRead = sizeof(readBuffer);
        }

        size_t bytesRead = file.readBytes(readBuffer, bytesToRead);
        if(bytesRead == 0u)
        {
            break;
        }

        html.concat(readBuffer, bytesRead);
    }
    file.close();

    if(html.length() == 0)
    {
	  #ifdef USE_DEBUG_WEB_SERVER		
        Serial.printf("[Web] GET / dynamic render skipped: readString returned 0 bytes (file=%u, template=%s). Serving static file.\n",
                      (uint32_t)fileSize,
                      templatePath.c_str());
	  #endif
        File fallback = SPIFFS.open(templatePath, "r");
        
		if(!fallback || fallback.size() == 0)
        {
            if(fallback)
            {
                fallback.close();
            }
			
		  #ifdef USE_DEBUG_WEB_SERVER
            Serial.println("[Web] GET / static fallback unavailable. Serving embedded fallback.");
		  #endif	
            sendEmbeddedConfigFallbackPage();
            return;
        }

        server.streamFile(fallback, "text/html");
        fallback.close();
        return;
    }

    html.replace("{{wifiSSID}}", 							Config.GetWIFI_SSID());
    html.replace("{{wifiStatus}}", 							getWifiConnectionStatusLabel());
    html.replace("{{wifiMode}}", 							getWifiModeLabel());
    html.replace("{{wifiDhcpStatus}}", 						Config.GetWIFI_UseDHCP() ? "Enabled" : "Disabled (static)");
    html.replace("{{wifiRssi}}", 							getCurrentWifiRssi());
    html.replace("{{wifiIp}}", 								getCurrentWifiIp());
    html.replace("{{wifiGateway}}", 						getCurrentWifiGateway());
    html.replace("{{wifiSubnet}}", 							getCurrentWifiSubnet());
    html.replace("{{wifiDns1}}", 							getCurrentWifiDns());
    html.replace("{{localDateTime}}", 						GetLocalDateTimeSummary());
    html.replace("{{localTimeValid}}", 						IsSystemTimeValid() ? "1" : "0");
    html.replace("{{ntpStatus}}", 							GetNtpSyncStatusSummary());
    html.replace("{{todaySunrise}}", 						GetTodaySunriseSummary());
    html.replace("{{todaySunset}}", 						GetTodaySunsetSummary());
    html.replace("{{todayAzMin}}", 					GetTodayAzimuthMinSummary());
    html.replace("{{todayAzMax}}", 					GetTodayAzimuthMaxSummary());
    html.replace("{{todayElevMax}}", 					GetTodayElevationMaxSummary());
    html.replace("{{trackingOverrideStatus}}", 				GetTrackingOverrideStatusSummary());
    html.replace("{{mpptLinkStatus}}", 					GetMpptLinkStatusSummary());
    html.replace("{{load1CurrentA}}", 						String(GetLoad1CurrentA(), 3));
    html.replace("{{load2CurrentA}}", 						String(GetLoad2CurrentA(), 3));
    html.replace("{{ambientLightLux}}", 					String(GetAmbientLightLux(), 1));
    html.replace("{{pnlVolt}}", 						String(GetPanelVoltage(), 1));
    html.replace("{{chargingCurrent}}", 					String(GetChargingCurrent(), 1));
    html.replace("{{battSoc}}", 						String(GetBatterySoc(), 0));
    html.replace("{{battVolt}}", 					String(GetBatteryVoltage(), 1));
    html.replace("{{pnlCurrent}}", 					String(GetPanelCurrent(), 2));
    html.replace("{{pnlChargingPwr}}", 				String(GetPanelChargingPower(), 0));
    html.replace("{{calcAz}}", 					GetCalculatedAzimuthSummary());
    html.replace("{{calcElev}}", 				GetCalculatedElevationSummary());
    html.replace("{{out1State}}",						formatOutputState(0));
    html.replace("{{out2State}}", 						formatOutputState(1));
    html.replace("{{out3State}}", 						formatOutputState(2));
    html.replace("{{out1Status}}", 						GetOutputControlStatus(0));
    html.replace("{{out2Status}}", 						GetOutputControlStatus(1));
    html.replace("{{out3Status}}", 						GetOutputControlStatus(2));
    html.replace("{{out1StatusClass}}", 					GetOutputStatusClass(GetOutputControlStatus(0)));
    html.replace("{{out2StatusClass}}", 					GetOutputStatusClass(GetOutputControlStatus(1)));
    html.replace("{{out3StatusClass}}", 					GetOutputStatusClass(GetOutputControlStatus(2)));
    html.replace("{{out1Mode}}", 						Config.GetOutputAutomaticMode(0) ? "AUTO" : "MANUAL");
    html.replace("{{out2Mode}}", 						Config.GetOutputAutomaticMode(1) ? "AUTO" : "MANUAL");
    html.replace("{{out3Mode}}", 						Config.GetOutputAutomaticMode(2) ? "AUTO" : "MANUAL");
    html.replace("{{out1Name}}", 						HtmlEscape(String(Config.GetOutputName(0))));
    html.replace("{{out2Name}}", 						HtmlEscape(String(Config.GetOutputName(1))));
    html.replace("{{out3Name}}", 						HtmlEscape(String(Config.GetOutputName(2))));
    html.replace("{{out1DutyPercent}}",					String(GetMosfetPwmPercent(0)));
    html.replace("{{out2DutyPercent}}",	 				String(GetMosfetPwmPercent(1)));
    html.replace("{{out3DutyPercent}}", 					String(GetMosfetPwmPercent(2)));
    html.replace("{{out1NextEvtTime}}", 				GetOutputNextEventTimeSummary(0));
    html.replace("{{out2NextEvtTime}}", 				GetOutputNextEventTimeSummary(1));
    html.replace("{{out3NextEvtTime}}", 				GetOutputNextEventTimeSummary(2));
    html.replace("{{out1NextEvtReason}}", 			GetOutputNextEventReasonSummary(0));
    html.replace("{{out2NextEvtReason}}", 			GetOutputNextEventReasonSummary(1));
    html.replace("{{out3NextEvtReason}}", 			GetOutputNextEventReasonSummary(2));
    for(uint8_t outputIndex = 0u; outputIndex < ConfigModule::OUTPUT_COUNT; outputIndex++)
    {
        String outputPrefix = "schedule" + String(outputIndex + 1u);
        html.replace("{{" + outputPrefix + "ResolvedWindow}}", GetOutputScheduleSummary(outputIndex));

        for(uint8_t slotIndex = 0u; slotIndex < ConfigModule::OUTPUT_SCHEDULE_SLOT_COUNT; slotIndex++)
        {
            const OutputSchedule_t& schedule = Config.GetOutputSchedule(outputIndex, slotIndex);
            String prefix = outputPrefix;
            if(slotIndex > 0u)
            {
                prefix += "Slot";
                prefix += String(slotIndex + 1u);
            }

            html.replace("{{" + prefix + "EnabledChecked}}", schedule.Enabled ? "checked" : "");
            html.replace("{{" + prefix + "StartTypeFixedSelected}}", schedule.StartType == 0u ? "selected" : "");
            html.replace("{{" + prefix + "StartTypeSunriseSelected}}", schedule.StartType == 1u ? "selected" : "");
            html.replace("{{" + prefix + "StartTypeSunsetSelected}}", schedule.StartType == 2u ? "selected" : "");
            html.replace("{{" + prefix + "EndTypeFixedSelected}}", schedule.EndType == 0u ? "selected" : "");
            html.replace("{{" + prefix + "EndTypeSunriseSelected}}", schedule.EndType == 1u ? "selected" : "");
            html.replace("{{" + prefix + "EndTypeSunsetSelected}}", schedule.EndType == 2u ? "selected" : "");
            html.replace("{{" + prefix + "StartHour}}", String(schedule.StartHour));
            html.replace("{{" + prefix + "StartMinute}}", String(schedule.StartMinute));
            html.replace("{{" + prefix + "StartOffsetMinutes}}", String(schedule.StartOffsetMinutes));
            html.replace("{{" + prefix + "EndHour}}", String(schedule.EndHour));
            html.replace("{{" + prefix + "EndMinute}}", String(schedule.EndMinute));
            html.replace("{{" + prefix + "EndOffsetMinutes}}", String(schedule.EndOffsetMinutes));
            html.replace("{{" + prefix + "DutyPercent}}", String(schedule.DutyPercent));
            html.replace("{{" + prefix + "ActiveDaysMask}}", String(Config.GetOutputScheduleActiveDaysMask(outputIndex, slotIndex)));
        }
    }
    html.replace("{{stLatitude}}", 							String(Config.GetST_Latitude(), 6));
    html.replace("{{stLongitude}}", 						String(Config.GetST_Longitude(), 6));
    html.replace("{{stAltitude}}", 							String(Config.GetST_Altitude(), 1));
    html.replace("{{stTimeZoneOffset}}", 					String(Config.GetST_TimeZoneOffset(), 2));
    html.replace("{{stUseDSTChecked}}", 					Config.GetST_UseDST() ? "checked" : "");
    html.replace("{{stPressure}}", 							String(Config.GetST_Pressure(), 2));
    html.replace("{{stTemperature}}", 						String(Config.GetST_Temperature() - 273.15, 2));
    html.replace("{{azDegMax}}", 						String(Config.GetAzimuthDegMax(), 2));
    html.replace("{{azDegMin}}", 						String(Config.GetAzimuthDegMin(), 2));
    html.replace("{{azStepsPerDegree}}", 				String(Config.GetAzimuthStepsPerDegree(), 3));
    html.replace("{{azStepSpeedHz}}", 					String(Config.GetAzimuthStepSpeedHz()));
    html.replace("{{azStepAcceleration}}", 			String(Config.GetAzimuthStepAcceleration()));
    html.replace("{{azTimeThreshold}}", 				String(Config.GetAzimuthTimeThreshold()));
    html.replace("{{azGearReduction}}", 				String(Config.GetAzimuthGearReduction(), 5));
    html.replace("{{elevDegMax}}", 					String(Config.GetElevationDegMax(), 2));
    html.replace("{{elevDegMin}}", 					String(Config.GetElevationDegMin(), 2));
    html.replace("{{elevStepsPerDegree}}", 			String(Config.GetElevationStepsPerDegree(), 3));
    html.replace("{{elevStepSpeedHz}}", 				String(Config.GetElevationStepSpeedHz()));
    html.replace("{{elevStepAcceleration}}", 			String(Config.GetElevationStepAcceleration()));
    html.replace("{{elevTimeThreshold}}", 				String(Config.GetElevationTimeThreshold()));
    html.replace("{{elevGearReduction}}", 			String(Config.GetElevationGearReduction(), 5));
    html.replace("{{stepper1MotorStepsPerRevolution}}", 	String(Config.GetAzimuthMotorStepsPerRevolution()));
    html.replace("{{stepper2MotorStepsPerRevolution}}", 	String(Config.GetElevationMotorStepsPerRevolution()));
    html.replace("{{stepper3MotorStepsPerRevolution}}", 	String(Config.GetStepper3MotorStepsPerRevolution()));
    html.replace("{{stepperMicrostep8Selected}}", 		Config.GetStepperMicrostepMode() == 8u ? "selected" : "");
    html.replace("{{stepperMicrostep16Selected}}", 	Config.GetStepperMicrostepMode() == 16u ? "selected" : "");
    html.replace("{{stepperMicrostep32Selected}}", 	Config.GetStepperMicrostepMode() == 32u ? "selected" : "");
    html.replace("{{stepperMicrostep64Selected}}", 	Config.GetStepperMicrostepMode() == 64u ? "selected" : "");
    html.replace("{{azStepsPerDegreeComputed}}", 	String(Config.GetAzimuthStepsPerDegree(), 3));
    html.replace("{{elevStepsPerDegreeComputed}}", String(Config.GetElevationStepsPerDegree(), 3));
    html.replace("{{ntpServer1}}", 							Config.GetNTP_Server1());
    html.replace("{{ntpServer2}}", 							Config.GetNTP_Server2());
    html.replace("{{ntpServer3}}", 							Config.GetNTP_Server3());
    html.replace("{{wifiUseDHCPChecked}}", 					Config.GetWIFI_UseDHCP() ? "checked" : "");
    html.replace("{{wifiStaticIp}}", 						Config.GetWIFI_StaticIP());
    html.replace("{{wifiGatewayConfig}}", 					Config.GetWIFI_Gateway());
    html.replace("{{wifiSubnetConfig}}", 					Config.GetWIFI_SubnetMask());
    html.replace("{{wifiDns1Config}}", 						Config.GetWIFI_DNS1());
    html.replace("{{wifiStaticIpDisplay}}", 				getWifiDisplayValue(Config.GetWIFI_StaticIP(), true));
    html.replace("{{wifiGatewayConfigDisplay}}", 			getWifiDisplayValue(Config.GetWIFI_Gateway(), true));
    html.replace("{{wifiSubnetConfigDisplay}}", 			getWifiDisplayValue(Config.GetWIFI_SubnetMask(), true));
    html.replace("{{wifiDns1ConfigDisplay}}", 				getWifiDisplayValue(Config.GetWIFI_DNS1(), true));
    html.replace("{{wifiSSID2}}", 							Config.GetWIFI_SSID2());
    html.replace("{{wifiPassword}}", "");
    html.replace("{{wifiPassword2}}", "");

  #ifdef USE_DEBUG_WEB_SERVER
    Serial.printf("[Web] GET / served dynamic page: template=%s source=%u rendered=%u freeHeap=%u\n",
                   templatePath.c_str(), (uint32_t)fileSize, (uint32_t)html.length(), (uint32_t)ESP.getFreeHeap());
  #endif

        server.setContentLength(html.length());
        server.send(200, "text/html", "");
        constexpr size_t responseChunkSize = 1024u;
        for(size_t offset = 0u; offset < html.length(); offset += responseChunkSize)
        {
                size_t chunkLength = html.length() - offset;
                if(chunkLength > responseChunkSize)
                {
                        chunkLength = responseChunkSize;
                }

                server.sendContent(html.c_str() + offset, chunkLength);
        }
}

void WebServerModule::handleDiagnosticsPage()
{
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");

    String templatePath = selectTemplatePath("/portal/diag.html");
    File file = SPIFFS.open(templatePath, "r");

    if(!file && templatePath != "/portal/diag.html")
    {
	  #ifdef USE_DEBUG_WEB_SERVER
        Serial.printf("[Web] %s missing, fallback to /portal/diag.html\n", templatePath.c_str());
	  #endif	
        templatePath = "/portal/diag.html";
        file = SPIFFS.open(templatePath, "r");
    }

    if(file && file.size() == 0 && templatePath != "/portal/diag.html")
    {
	  #ifdef USE_DEBUG_WEB_SERVER
        Serial.printf("[Web] %s is empty, fallback to /portal/diag.html\n", templatePath.c_str());
	  #endif	
        file.close();
        templatePath = "/portal/diag.html";
        file = SPIFFS.open(templatePath, "r");
    }

    if(!file)
    {
	  #ifdef USE_DEBUG_WEB_SERVER
        Serial.printf("[Web] GET /diag failed: %s not found in SPIFFS\n", templatePath.c_str());
	  #endif	
        sendEmbeddedDiagnosticsFallbackPage();
        return;
    }

    if(file.size() == 0)
    {
	  #ifdef USE_DEBUG_WEB_SERVER		
        Serial.printf("[Web] GET /diag failed: %s is empty. Serving embedded fallback.\n", templatePath.c_str());
	  #endif	
        file.close();
        sendEmbeddedDiagnosticsFallbackPage();
        return;
    }

    size_t fileSize = file.size();
    String html = file.readString();
    file.close();

    if(html.length() == 0)
    {
	  #ifdef USE_DEBUG_WEB_SERVER		
        Serial.printf("[Web] GET /diag dynamic render skipped: readString returned 0 bytes (file=%u, template=%s). Serving static file.\n",
                      (uint32_t)fileSize,
                      templatePath.c_str());
      #endif					  
        File fallback = SPIFFS.open(templatePath, "r");
        if(!fallback || fallback.size() == 0)
        {
            if(fallback)
            {
                fallback.close();
            }
			
		  #ifdef USE_DEBUG_WEB_SERVER
            Serial.println("[Web] GET /diag static fallback unavailable. Serving embedded fallback.");
		  #endif	
            sendEmbeddedDiagnosticsFallbackPage();
            return;
        }

        server.streamFile(fallback, "text/html");
        fallback.close();
        return;
    }

    html.replace("{{trackingOverrideStatus}}", 	GetTrackingOverrideStatusSummary());
    html.replace("{{localDateTime}}", 			GetLocalDateTimeSummary());
    html.replace("{{limitSwitchSummary}}", 		GetLimitSwitchSummary());
    html.replace("{{stepperDiagSummary}}", 		GetStepperDiagSummary());
    html.replace("{{limit1State}}", 			GetLimitSwitchStateLabel(0));
    html.replace("{{limit2State}}", 			GetLimitSwitchStateLabel(1));
    html.replace("{{limit3State}}", 			GetLimitSwitchStateLabel(2));
    html.replace("{{limit4State}}", 			GetLimitSwitchStateLabel(3));
    html.replace("{{diag1State}}", 				GetStepperDiagStateLabel(0));
    html.replace("{{diag2State}}", 				GetStepperDiagStateLabel(1));
    html.replace("{{diag3State}}", 				GetStepperDiagStateLabel(2));
    html.replace("{{limit1Class}}", 			GetSignalStatusClass(GetLimitSwitchStateLabel(0)));
    html.replace("{{limit2Class}}", 			GetSignalStatusClass(GetLimitSwitchStateLabel(1)));
    html.replace("{{limit3Class}}", 			GetSignalStatusClass(GetLimitSwitchStateLabel(2)));
    html.replace("{{limit4Class}}", 			GetSignalStatusClass(GetLimitSwitchStateLabel(3)));
    html.replace("{{diag1Class}}", 				GetSignalStatusClass(GetStepperDiagStateLabel(0)));
    html.replace("{{diag2Class}}", 				GetSignalStatusClass(GetStepperDiagStateLabel(1)));
    html.replace("{{diag3Class}}", 				GetSignalStatusClass(GetStepperDiagStateLabel(2)));

  #ifdef USE_DEBUG_WEB_SERVER
    Serial.printf("[Web] GET /diag served dynamic page: template=%s source=%u rendered=%u freeHeap=%u\n",
                   templatePath.c_str(), (uint32_t)fileSize, (uint32_t)html.length(), (uint32_t)ESP.getFreeHeap());
  #endif

    server.send(200, "text/html", html);
}

void WebServerModule::HandleGetTrackingOverrideStatus()
{
  #ifdef USE_DEBUG_WEB_SERVER
    Serial.println("[Web] GET /diag/status");
  #endif

    String payload;
    payload.reserve(180);
    payload += "override=";
    payload += GetTrackingOverrideStatusSummary();
    payload += "\n";
    payload += "limits=";
    payload += GetLimitSwitchSummary();
    payload += "\n";
    payload += "stepperDiag=";
    payload += GetStepperDiagSummary();
    payload += "\n";
    payload += "time=";
    payload += GetLocalDateTimeSummary();

    server.send(200, "text/plain", payload);
}

void WebServerModule::HandleGetPortalStatus()
{
    String payload;
    payload.reserve(640);

payload += "wifiMode=";
payload += getWifiModeLabel();
payload += "\n";

payload += "wifiStatus=";
payload += getWifiConnectionStatusLabel();
payload += "\n";

payload += "wifiDhcpStatus=";
payload += (Config.GetWIFI_UseDHCP() ? "Enabled" : "Disabled (static)");
payload += "\n";

payload += "wifiRssi=";
payload += getCurrentWifiRssi();
payload += "\n";

payload += "wifiIp=";
payload += getCurrentWifiIp();
payload += "\n";

payload += "wifiGateway=";
payload += getCurrentWifiGateway();
payload += "\n";

payload += "wifiSubnet=";
payload += getCurrentWifiSubnet();
payload += "\n";

payload += "wifiDns1=";
payload += getCurrentWifiDns();
payload += "\n";

payload += "localDateTime=";
payload += GetLocalDateTimeSummary();
payload += "\n";

payload += "timeValid=";
payload += (IsSystemTimeValid() ? "1" : "0");
payload += "\n";

payload += "ntpStatus=";
payload += GetNtpSyncStatusSummary();
payload += "\n";

payload += "todaySunrise=";
payload += GetTodaySunriseSummary();
payload += "\n";

payload += "todaySunset=";
payload += GetTodaySunsetSummary();
payload += "\n";

payload += "todayAzMin=";
payload += GetTodayAzimuthMinSummary();
payload += "\n";

payload += "todayAzMax=";
payload += GetTodayAzimuthMaxSummary();
payload += "\n";

payload += "todayElevMax=";
payload += GetTodayElevationMaxSummary();
payload += "\n";

payload += "trackingOverrideStatus=";
payload += GetTrackingOverrideStatusSummary();
payload += "\n";

payload += "mpptLinkStatus=";
payload += GetMpptLinkStatusSummary();
payload += "\n";

payload += "load1CurrentA=";
payload += String(GetLoad1CurrentA(), 3);
payload += " A\n";

payload += "load2CurrentA=";
payload += String(GetLoad2CurrentA(), 3);
payload += " A\n";

payload += "load1AdcRaw=";
payload += String((uint32_t)GetLoad1AdcRaw());
payload += "\n";

payload += "load2AdcRaw=";
payload += String((uint32_t)GetLoad2AdcRaw());
payload += "\n";

payload += "ambientLightLux=";
payload += String(GetAmbientLightLux(), 1);
payload += " lx\n";

payload += "pnlVolt=";
payload += String(GetPanelVoltage(), 1);
payload += " V\n";

payload += "chargingCurrent=";
payload += String(GetChargingCurrent(), 1);
payload += " A\n";

payload += "battSoc=";
payload += String(GetBatterySoc(), 0);
payload += " %\n";

payload += "battVolt=";
payload += String(GetBatteryVoltage(), 1);
payload += " V\n";

payload += "pnlCurrent=";
payload += String(GetPanelCurrent(), 2);
payload += " A\n";

payload += "pnlChargingPwr=";
payload += String(GetPanelChargingPower(), 0);
payload += " W\n";

payload += "calcAz=";
payload += GetCalculatedAzimuthSummary();
payload += "\n";

payload += "calcElev=";
payload += GetCalculatedElevationSummary();
payload += "\n";

payload += "out1Status=";
payload += GetOutputControlStatus(0);
payload += "\n";

payload += "out2Status=";
payload += GetOutputControlStatus(1);
payload += "\n";

payload += "out3Status=";
payload += GetOutputControlStatus(2);
payload += "\n";

payload += "out1State=";
payload += formatOutputState(0);
payload += "\n";

payload += "out2State=";
payload += formatOutputState(1);
payload += "\n";

payload += "out3State=";
payload += formatOutputState(2);
payload += "\n";

payload += "out1Mode=";
payload += (Config.GetOutputAutomaticMode(0) ? "AUTO" : "MANUAL");
payload += "\n";

payload += "out2Mode=";
payload += (Config.GetOutputAutomaticMode(1) ? "AUTO" : "MANUAL");
payload += "\n";

payload += "out3Mode=";
payload += (Config.GetOutputAutomaticMode(2) ? "AUTO" : "MANUAL");
payload += "\n";

payload += "out1NextEvtTime=";
payload += GetOutputNextEventTimeSummary(0);
payload += "\n";

payload += "out2NextEvtTime=";
payload += GetOutputNextEventTimeSummary(1);
payload += "\n";

payload += "out3NextEvtTime=";
payload += GetOutputNextEventTimeSummary(2);
payload += "\n";

payload += "out1NextEvtReason=";
payload += GetOutputNextEventReasonSummary(0);
payload += "\n";

payload += "out2NextEvtReason=";
payload += GetOutputNextEventReasonSummary(1);
payload += "\n";

payload += "out3NextEvtReason=";
payload += GetOutputNextEventReasonSummary(2);
payload += "\n";

server.send(200, "text/plain", payload);
}

void WebServerModule::HandleGetApiConfig()
{
    String payload;
    payload.reserve(3600);

    payload += "{";
    payload += "\"solarTracking\":{";
    payload += "\"latitude\":" + String(Config.GetST_Latitude(), 6) + ",";
    payload += "\"longitude\":" + String(Config.GetST_Longitude(), 6) + ",";
    payload += "\"altitude\":" + String(Config.GetST_Altitude(), 1) + ",";
    payload += "\"timeZoneOffset\":" + String(Config.GetST_TimeZoneOffset(), 2) + ",";
    payload += "\"useDST\":" + String(Config.GetST_UseDST() ? "true" : "false") + ",";
    payload += "\"pressure\":" + String(Config.GetST_Pressure(), 2) + ",";
    payload += "\"temperature\":" + String(Config.GetST_Temperature() - 273.15, 2);
    payload += "},";

    payload += "\"azimuth\":{";
    payload += "\"degMax\":" + String(Config.GetAzimuthDegMax(), 2) + ",";
    payload += "\"degMin\":" + String(Config.GetAzimuthDegMin(), 2) + ",";
    payload += "\"stepsPerDegree\":" + String(Config.GetAzimuthStepsPerDegree(), 3) + ",";
    payload += "\"motorStepsPerRevolution\":" + String(Config.GetAzimuthMotorStepsPerRevolution()) + ",";
    payload += "\"gearReduction\":" + String(Config.GetAzimuthGearReduction(), 5) + ",";
    payload += "\"stepSpeedHz\":" + String(Config.GetAzimuthStepSpeedHz()) + ",";
    payload += "\"stepAcceleration\":" + String(Config.GetAzimuthStepAcceleration()) + ",";
    payload += "\"timeThreshold\":" + String(Config.GetAzimuthTimeThreshold());
    payload += "},";

    payload += "\"elevation\":{";
    payload += "\"degMax\":" + String(Config.GetElevationDegMax(), 2) + ",";
    payload += "\"degMin\":" + String(Config.GetElevationDegMin(), 2) + ",";
    payload += "\"stepsPerDegree\":" + String(Config.GetElevationStepsPerDegree(), 3) + ",";
    payload += "\"motorStepsPerRevolution\":" + String(Config.GetElevationMotorStepsPerRevolution()) + ",";
    payload += "\"gearReduction\":" + String(Config.GetElevationGearReduction(), 5) + ",";
    payload += "\"stepSpeedHz\":" + String(Config.GetElevationStepSpeedHz()) + ",";
    payload += "\"stepAcceleration\":" + String(Config.GetElevationStepAcceleration()) + ",";
    payload += "\"timeThreshold\":" + String(Config.GetElevationTimeThreshold());
    payload += "},";

    payload += "\"stepper\":{";
    payload += "\"microstepMode\":" + String(Config.GetStepperMicrostepMode()) + ",";
    payload += "\"motor1StepsPerRevolution\":" + String(Config.GetAzimuthMotorStepsPerRevolution()) + ",";
    payload += "\"motor2StepsPerRevolution\":" + String(Config.GetElevationMotorStepsPerRevolution()) + ",";
    payload += "\"motor3StepsPerRevolution\":" + String(Config.GetStepper3MotorStepsPerRevolution());
    payload += "},";

    payload += "\"ntp\":{";
    payload += "\"server1\":\"" + JsonEscape(String(Config.GetNTP_Server1())) + "\",";
    payload += "\"server2\":\"" + JsonEscape(String(Config.GetNTP_Server2())) + "\",";
    payload += "\"server3\":\"" + JsonEscape(String(Config.GetNTP_Server3())) + "\"";
    payload += "},";

    payload += "\"wifi\":{";
    payload += "\"ssid\":\"" + JsonEscape(String(Config.GetWIFI_SSID())) + "\"";
    payload += "},";

    payload += "\"outputs\":[";
    for(uint8_t i = 0; i < 3; i++)
    {
        const OutputSchedule_t& schedule1 = Config.GetOutputSchedule(i, 0u);
        const OutputSchedule_t& schedule2 = Config.GetOutputSchedule(i, 1u);
        if(i > 0)
        {
            payload += ",";
        }

        payload += "{";
        payload += "\"index\":" + String(i + 1) + ",";
        payload += "\"enabled\":" + String(schedule1.Enabled ? "true" : "false") + ",";
        payload += "\"automaticMode\":" + String(Config.GetOutputAutomaticMode(i) ? "true" : "false") + ",";
        payload += "\"scheduleConflict\":" + String(HasOutputScheduleConflict(i) ? "true" : "false") + ",";
        payload += "\"startType\":" + String(schedule1.StartType) + ",";
        payload += "\"startHour\":" + String(schedule1.StartHour) + ",";
        payload += "\"startMinute\":" + String(schedule1.StartMinute) + ",";
        payload += "\"startOffsetMinutes\":" + String(schedule1.StartOffsetMinutes) + ",";
        payload += "\"endType\":" + String(schedule1.EndType) + ",";
        payload += "\"endHour\":" + String(schedule1.EndHour) + ",";
        payload += "\"endMinute\":" + String(schedule1.EndMinute) + ",";
        payload += "\"endOffsetMinutes\":" + String(schedule1.EndOffsetMinutes) + ",";
        payload += "\"dutyPercent\":" + String(schedule1.DutyPercent) + ",";
        payload += "\"schedules\":[";
        payload += "{";
        payload += "\"slot\":1,";
        payload += "\"enabled\":" + String(schedule1.Enabled ? "true" : "false") + ",";
        payload += "\"startType\":" + String(schedule1.StartType) + ",";
        payload += "\"startHour\":" + String(schedule1.StartHour) + ",";
        payload += "\"startMinute\":" + String(schedule1.StartMinute) + ",";
        payload += "\"startOffsetMinutes\":" + String(schedule1.StartOffsetMinutes) + ",";
        payload += "\"endType\":" + String(schedule1.EndType) + ",";
        payload += "\"endHour\":" + String(schedule1.EndHour) + ",";
        payload += "\"endMinute\":" + String(schedule1.EndMinute) + ",";
        payload += "\"endOffsetMinutes\":" + String(schedule1.EndOffsetMinutes) + ",";
        payload += "\"dutyPercent\":" + String(schedule1.DutyPercent);
        payload += "},";
        payload += "{";
        payload += "\"slot\":2,";
        payload += "\"enabled\":" + String(schedule2.Enabled ? "true" : "false") + ",";
        payload += "\"startType\":" + String(schedule2.StartType) + ",";
        payload += "\"startHour\":" + String(schedule2.StartHour) + ",";
        payload += "\"startMinute\":" + String(schedule2.StartMinute) + ",";
        payload += "\"startOffsetMinutes\":" + String(schedule2.StartOffsetMinutes) + ",";
        payload += "\"endType\":" + String(schedule2.EndType) + ",";
        payload += "\"endHour\":" + String(schedule2.EndHour) + ",";
        payload += "\"endMinute\":" + String(schedule2.EndMinute) + ",";
        payload += "\"endOffsetMinutes\":" + String(schedule2.EndOffsetMinutes) + ",";
        payload += "\"dutyPercent\":" + String(schedule2.DutyPercent);
        payload += "}]";
        payload += "}";
    }
    payload += "]";

    payload += "}";
    server.send(200, "application/json", payload);
}

void WebServerModule::HandleSaveApiConfig()
{
    if(server.hasArg("stLatitude"))       { Config.SetST_Latitude(server.arg("stLatitude").toFloat()); }
    if(server.hasArg("stLongitude"))      { Config.SetST_Longitude(server.arg("stLongitude").toFloat()); }
    if(server.hasArg("stAltitude"))       { Config.SetST_Altitude(server.arg("stAltitude").toFloat()); }
    if(server.hasArg("stTimeZoneOffset")) { Config.SetST_TimeZoneOffset(server.arg("stTimeZoneOffset").toFloat()); }
    Config.SetST_UseDST(server.hasArg("stUseDST"));
    if(server.hasArg("stPressure"))                 { Config.SetST_Pressure(server.arg("stPressure").toFloat()); }
    if(server.hasArg("stTemperature"))              { Config.SetST_Temperature(server.arg("stTemperature").toFloat() + 273.15); }

    if(server.hasArg("azimuthDegMax")) { Config.SetAzimuthDegMax(server.arg("azimuthDegMax").toFloat()); }
    if(server.hasArg("azimuthDegMin")) { Config.SetAzimuthDegMin(server.arg("azimuthDegMin").toFloat()); }
    if(server.hasArg("azimuthGearReduction")) { Config.SetAzimuthGearReduction(server.arg("azimuthGearReduction").toFloat()); }
    if(server.hasArg("azimuthStepSpeedHz")) { Config.SetAzimuthStepSpeedHz(server.arg("azimuthStepSpeedHz").toInt()); }
    if(server.hasArg("azimuthStepAcceleration")) { Config.SetAzimuthStepAcceleration(server.arg("azimuthStepAcceleration").toInt()); }
    if(server.hasArg("azimuthTimeThreshold")) { Config.SetAzimuthTimeThreshold(server.arg("azimuthTimeThreshold").toInt()); }

    if(server.hasArg("elevationDegMax")) { Config.SetElevationDegMax(server.arg("elevationDegMax").toFloat()); }
    if(server.hasArg("elevationDegMin")) { Config.SetElevationDegMin(server.arg("elevationDegMin").toFloat()); }
    if(server.hasArg("elevationGearReduction")) { Config.SetElevationGearReduction(server.arg("elevationGearReduction").toFloat()); }
    if(server.hasArg("elevationStepSpeedHz")) { Config.SetElevationStepSpeedHz(server.arg("elevationStepSpeedHz").toInt()); }
    if(server.hasArg("elevationStepAcceleration")) { Config.SetElevationStepAcceleration(server.arg("elevationStepAcceleration").toInt()); }
    if(server.hasArg("elevationTimeThreshold")) { Config.SetElevationTimeThreshold(server.arg("elevationTimeThreshold").toInt()); }

    if(server.hasArg("stepper1MotorStepsPerRevolution")) { Config.SetAzimuthMotorStepsPerRevolution((uint16_t)server.arg("stepper1MotorStepsPerRevolution").toInt()); }
    if(server.hasArg("stepper2MotorStepsPerRevolution")) { Config.SetElevationMotorStepsPerRevolution((uint16_t)server.arg("stepper2MotorStepsPerRevolution").toInt()); }
    if(server.hasArg("stepper3MotorStepsPerRevolution")) { Config.SetStepper3MotorStepsPerRevolution((uint16_t)server.arg("stepper3MotorStepsPerRevolution").toInt()); }
    if(server.hasArg("stepperMicrostepMode")) { Config.SetStepperMicrostepMode((uint8_t)server.arg("stepperMicrostepMode").toInt()); }

    if(server.hasArg("ntpServer1")) { Config.SetNTP_Server1(server.arg("ntpServer1").c_str()); }
    if(server.hasArg("ntpServer2")) { Config.SetNTP_Server2(server.arg("ntpServer2").c_str()); }
    if(server.hasArg("ntpServer3")) { Config.SetNTP_Server3(server.arg("ntpServer3").c_str()); }

    if(server.hasArg("wifiSSID"))
    {
        Config.SetWIFI_SSID(server.arg("wifiSSID").c_str());
    }

    if(server.hasArg("wifiPassword"))
    {
        String password = server.arg("wifiPassword");
        if(password.length() > 0)
        {
            Config.SetWIFI_Password(password.c_str());
        }
    }

    for(uint8_t i = 0; i < 3; i++)
    {
        for(uint8_t slot = 0u; slot < ConfigModule::OUTPUT_SCHEDULE_SLOT_COUNT; slot++)
        {
            String legacyPrefix = "schedule" + String(i + 1);
            String slotPrefix = "output" + String(i + 1) + "Schedule" + String(slot + 1);
            bool useLegacyNames = (slot == 0u);

            OutputSchedule_t schedule = Config.GetOutputSchedule(i, slot);

            String enabledName = slotPrefix + "Enabled";
            if(useLegacyNames && !server.hasArg(enabledName)) { enabledName = legacyPrefix + "Enabled"; }
            schedule.Enabled = server.hasArg(enabledName);

            String startTypeName = slotPrefix + "StartType";
            if(useLegacyNames && !server.hasArg(startTypeName)) { startTypeName = legacyPrefix + "StartType"; }
            if(server.hasArg(startTypeName)) { schedule.StartType = (uint8_t)server.arg(startTypeName).toInt(); }

            String startHourName = slotPrefix + "StartHour";
            if(useLegacyNames && !server.hasArg(startHourName)) { startHourName = legacyPrefix + "StartHour"; }
            if(server.hasArg(startHourName)) { schedule.StartHour = (uint8_t)server.arg(startHourName).toInt(); }

            String startMinuteName = slotPrefix + "StartMinute";
            if(useLegacyNames && !server.hasArg(startMinuteName)) { startMinuteName = legacyPrefix + "StartMinute"; }
            if(server.hasArg(startMinuteName)) { schedule.StartMinute = (uint8_t)server.arg(startMinuteName).toInt(); }

            String startOffsetName = slotPrefix + "StartOffsetMinutes";
            if(useLegacyNames && !server.hasArg(startOffsetName)) { startOffsetName = legacyPrefix + "StartOffsetMinutes"; }
            if(server.hasArg(startOffsetName)) { schedule.StartOffsetMinutes = (int16_t)server.arg(startOffsetName).toInt(); }

            String endTypeName = slotPrefix + "EndType";
            if(useLegacyNames && !server.hasArg(endTypeName)) { endTypeName = legacyPrefix + "EndType"; }
            if(server.hasArg(endTypeName)) { schedule.EndType = (uint8_t)server.arg(endTypeName).toInt(); }

            String endHourName = slotPrefix + "EndHour";
            if(useLegacyNames && !server.hasArg(endHourName)) { endHourName = legacyPrefix + "EndHour"; }
            if(server.hasArg(endHourName)) { schedule.EndHour = (uint8_t)server.arg(endHourName).toInt(); }

            String endMinuteName = slotPrefix + "EndMinute";
            if(useLegacyNames && !server.hasArg(endMinuteName)) { endMinuteName = legacyPrefix + "EndMinute"; }
            if(server.hasArg(endMinuteName)) { schedule.EndMinute = (uint8_t)server.arg(endMinuteName).toInt(); }

            String endOffsetName = slotPrefix + "EndOffsetMinutes";
            if(useLegacyNames && !server.hasArg(endOffsetName)) { endOffsetName = legacyPrefix + "EndOffsetMinutes"; }
            if(server.hasArg(endOffsetName)) { schedule.EndOffsetMinutes = (int16_t)server.arg(endOffsetName).toInt(); }

            String dutyName = slotPrefix + "DutyPercent";
            if(useLegacyNames && !server.hasArg(dutyName)) { dutyName = legacyPrefix + "DutyPercent"; }
            if(server.hasArg(dutyName)) { schedule.DutyPercent = (uint8_t)server.arg(dutyName).toInt(); }

            if(schedule.StartType > 2) { schedule.StartType = 0; }
            if(schedule.EndType > 2) { schedule.EndType = 0; }
            if(schedule.StartOffsetMinutes < -720) { schedule.StartOffsetMinutes = -720; }
            if(schedule.StartOffsetMinutes > 720) { schedule.StartOffsetMinutes = 720; }
            if(schedule.EndOffsetMinutes < -720) { schedule.EndOffsetMinutes = -720; }
            if(schedule.EndOffsetMinutes > 720) { schedule.EndOffsetMinutes = 720; }
            if(schedule.StartHour > 23) { schedule.StartHour = 23; }
            if(schedule.EndHour > 23) { schedule.EndHour = 23; }
            if(schedule.StartMinute > 59) { schedule.StartMinute = 59; }
            if(schedule.EndMinute > 59) { schedule.EndMinute = 59; }
            if(schedule.DutyPercent > 100) { schedule.DutyPercent = 100; }

            Config.SetOutputSchedule(i, slot, schedule);
        }

        String automaticModeName = "output" + String(i + 1) + "AutomaticMode";
        Config.SetOutputAutomaticMode(i, server.hasArg(automaticModeName));
    }

    Config.SaveConfig();
    ApplyOutputSchedules();

    server.send(200, "application/json", "{\"message\":\"Configuration saved\"}");
}

void WebServerModule::HandleGetApiStatus()
{
    String payload;
    payload.reserve(2600);

    payload += "{";
    payload += "\"network\":{";
    payload += "\"mode\":\"" + JsonEscape(getWifiModeLabel()) + "\",";
    payload += "\"ip\":\"" + JsonEscape(getCurrentWifiIp()) + "\",";
    payload += "\"localDateTime\":\"" + JsonEscape(GetLocalDateTimeSummary()) + "\",";
    payload += "\"ntpStatus\":\"" + JsonEscape(GetNtpSyncStatusSummary()) + "\"";
    payload += "},";

    payload += "\"solar\":{";
    payload += "\"sunrise\":\"" + JsonEscape(GetTodaySunriseSummary()) + "\",";
    payload += "\"sunset\":\"" + JsonEscape(GetTodaySunsetSummary()) + "\",";
    payload += "\"trackingOverrideStatus\":\"" + JsonEscape(GetTrackingOverrideStatusSummary()) + "\"";
    payload += "},";

    payload += "\"sensors\":{";
    payload += "\"load1CurrentA\":" + String(GetLoad1CurrentA(), 3) + ",";
    payload += "\"load2CurrentA\":" + String(GetLoad2CurrentA(), 3) + ",";
    payload += "\"load1AdcRaw\":" + String((uint32_t)GetLoad1AdcRaw()) + ",";
    payload += "\"load2AdcRaw\":" + String((uint32_t)GetLoad2AdcRaw());
    payload += "},";

    payload += "\"signals\":{";
    payload += "\"limitSwitchSummary\":\"" + JsonEscape(GetLimitSwitchSummary()) + "\",";
    payload += "\"stepperDiagSummary\":\"" + JsonEscape(GetStepperDiagSummary()) + "\"";
    payload += "},";

    payload += "\"outputs\":[";
    for(uint8_t i = 0; i < 3; i++)
    {
        if(i > 0)
        {
            payload += ",";
        }

        payload += "{";
        payload += "\"index\":" + String(i + 1) + ",";
        payload += "\"mode\":\"" + String(Config.GetOutputAutomaticMode(i) ? "AUTO" : "MANUAL") + "\",";
        payload += "\"status\":\"" + JsonEscape(GetOutputControlStatus(i)) + "\",";
        payload += "\"state\":\"" + formatOutputState(i) + "\",";
        payload += "\"pwmPercent\":" + String(GetMosfetPwmPercent(i)) + ",";
        payload += "\"scheduleConflict\":" + String(HasOutputScheduleConflict(i) ? "true" : "false") + ",";
        payload += "\"scheduleSummary\":\"" + JsonEscape(GetOutputScheduleSummary(i)) + "\"";
        payload += "}";
    }
    payload += "]";

    payload += "}";
    server.send(200, "application/json", payload);
}

void WebServerModule::HandleApiOutput()
{
    String errorMessage;
    if(!ExecuteOutputActionFromRequest(errorMessage))
    {
        server.send(400, "application/json", String("{\"message\":\"") + JsonEscape(errorMessage) + "\"}");
        return;
    }

    server.send(200, "application/json", "{\"message\":\"Output updated\"}");
}

bool WebServerModule::ExecuteOutputActionFromRequest(String& errorMessage)
{
    if(!server.hasArg("output") || !server.hasArg("action"))
    {
        errorMessage = "Missing output or action parameter";
        return false;
    }

    int outputIndex = server.arg("output").toInt() - 1;
    String action = server.arg("action");
    bool ok = false;

    if(outputIndex < 0 || outputIndex >= 3)
    {
        errorMessage = "Invalid output index";
        return false;
    }

    if(action == "auto")
    {
        Config.SetOutputAutomaticMode((uint8_t)outputIndex, true);
        Config.SaveConfig();
        ApplyOutputSchedules();
        return true;
    }

    if(action == "manual")
    {
        Config.SetOutputAutomaticMode((uint8_t)outputIndex, false);
        Config.SaveConfig();
        return true;
    }

    if(action == "on")
    {
        Config.SetOutputAutomaticMode((uint8_t)outputIndex, false);
        Config.SaveConfig();
        ok = SetMosfetOutput((uint8_t)outputIndex, true);
    }
    else if(action == "off")
    {
        Config.SetOutputAutomaticMode((uint8_t)outputIndex, false);
        Config.SaveConfig();
        ok = SetMosfetOutput((uint8_t)outputIndex, false);
    }
    else if(action == "toggle")
    {
        Config.SetOutputAutomaticMode((uint8_t)outputIndex, false);
        Config.SaveConfig();
        ok = ToggleMosfetOutput((uint8_t)outputIndex);
    }
    else if(action == "pwm" && server.hasArg("duty"))
    {
        int duty = server.arg("duty").toInt();
        if(duty < 0 || duty > 100)
        {
            errorMessage = "Invalid duty: expected 0..100";
            return false;
        }

        Config.SetOutputAutomaticMode((uint8_t)outputIndex, false);
        Config.SaveConfig();
        ok = SetMosfetPwmPercent((uint8_t)outputIndex, (uint8_t)duty);
    }

    if(!ok)
    {
        errorMessage = "Invalid output command";
        return false;
    }

    return true;
}

void WebServerModule::HandleSaveSolarTrackingConfig()
{
    if(server.hasArg("stLatitude"))       { Config.SetST_Latitude(server.arg("stLatitude").toFloat()); }
    if(server.hasArg("stLongitude"))      { Config.SetST_Longitude(server.arg("stLongitude").toFloat()); }
    if(server.hasArg("stAltitude"))       { Config.SetST_Altitude(server.arg("stAltitude").toFloat()); }
    if(server.hasArg("stTimeZoneOffset")) { Config.SetST_TimeZoneOffset(server.arg("stTimeZoneOffset").toFloat()); }
    Config.SetST_UseDST(server.hasArg("stUseDST"));
    if(server.hasArg("stPressure"))                 { Config.SetST_Pressure(server.arg("stPressure").toFloat()); }
    if(server.hasArg("stTemperature"))              { Config.SetST_Temperature(server.arg("stTemperature").toFloat() + 273.15); }

    Config.SaveConfig();
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebServerModule::HandleResetSolarTrackingConfig()
{
    Config.ResetSolarTrackingConfig();
    Config.SaveConfig();
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebServerModule::HandleSaveAzimuthConfig()
{
    if(server.hasArg("azimuthDegMax")) 						{ Config.SetAzimuthDegMax(server.arg("azimuthDegMax").toFloat()); }
    if(server.hasArg("azimuthDegMin")) 						{ Config.SetAzimuthDegMin(server.arg("azimuthDegMin").toFloat()); }
    if(server.hasArg("azimuthGearReduction")) 				{ Config.SetAzimuthGearReduction(server.arg("azimuthGearReduction").toFloat()); }
    if(server.hasArg("azimuthStepSpeedHz")) 				{ Config.SetAzimuthStepSpeedHz(server.arg("azimuthStepSpeedHz").toInt()); }
    if(server.hasArg("azimuthStepAcceleration")) 			{ Config.SetAzimuthStepAcceleration(server.arg("azimuthStepAcceleration").toInt()); }
    if(server.hasArg("azimuthTimeThreshold")) 				{ Config.SetAzimuthTimeThreshold(server.arg("azimuthTimeThreshold").toInt()); }

    Config.SaveConfig();
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebServerModule::HandleResetAzimuthConfig()
{
    Config.ResetAzimuthConfig();
    Config.SaveConfig();
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebServerModule::HandleSaveElevationConfig()
{
    if(server.hasArg("elevationDegMax")) 					{ Config.SetElevationDegMax(server.arg("elevationDegMax").toFloat()); }
    if(server.hasArg("elevationDegMin")) 					{ Config.SetElevationDegMin(server.arg("elevationDegMin").toFloat()); }
    if(server.hasArg("elevationGearReduction")) 			{ Config.SetElevationGearReduction(server.arg("elevationGearReduction").toFloat()); }
    if(server.hasArg("elevationStepSpeedHz")) 				{ Config.SetElevationStepSpeedHz(server.arg("elevationStepSpeedHz").toInt()); }
    if(server.hasArg("elevationStepAcceleration")) 		{ Config.SetElevationStepAcceleration(server.arg("elevationStepAcceleration").toInt()); }
    if(server.hasArg("elevationTimeThreshold")) 			{ Config.SetElevationTimeThreshold(server.arg("elevationTimeThreshold").toInt()); }

    Config.SaveConfig();
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebServerModule::HandleResetElevationConfig()
{
    Config.ResetElevationConfig();
    Config.SaveConfig();
    server.sendHeader("Location", "/");
    server.send(303);
}

#if 0
void WebServerModule::HandleResetAzimuthConfig()
{
    OutputSchedule_t previousSchedules[ConfigModule::OUTPUT_COUNT][ConfigModule::OUTPUT_SCHEDULE_SLOT_COUNT];
    uint8_t previousActiveDaysMasks[ConfigModule::OUTPUT_COUNT][ConfigModule::OUTPUT_SCHEDULE_SLOT_COUNT];

    for(uint8_t outputIndex = 0u; outputIndex < ConfigModule::OUTPUT_COUNT; outputIndex++)
    Config.SaveConfig();
        for(uint8_t slotIndex = 0u; slotIndex < ConfigModule::OUTPUT_SCHEDULE_SLOT_COUNT; slotIndex++)
        {
            previousSchedules[outputIndex][slotIndex] = Config.GetOutputSchedule(outputIndex, slotIndex);
            previousActiveDaysMasks[outputIndex][slotIndex] = Config.GetOutputScheduleActiveDaysMask(outputIndex, slotIndex);
        }
    }

    for(uint8_t outputIndex = 0u; outputIndex < ConfigModule::OUTPUT_COUNT; outputIndex++)
    {
        for(uint8_t slotIndex = 0u; slotIndex < ConfigModule::OUTPUT_SCHEDULE_SLOT_COUNT; slotIndex++)
        {
            String prefix = "schedule" + String(outputIndex + 1u);
            if(slotIndex > 0u)
            {
                prefix += "Slot";
                prefix += String(slotIndex + 1u);
            }

            OutputSchedule_t schedule = Config.GetOutputSchedule(outputIndex, slotIndex);
            schedule.Enabled = server.hasArg(prefix + "Enabled");
            if(server.hasArg(prefix + "StartType")) { schedule.StartType = (uint8_t)server.arg(prefix + "StartType").toInt(); }
            if(server.hasArg(prefix + "StartHour")) { schedule.StartHour = (uint8_t)server.arg(prefix + "StartHour").toInt(); }
            if(server.hasArg(prefix + "StartMinute")) { schedule.StartMinute = (uint8_t)server.arg(prefix + "StartMinute").toInt(); }
            if(server.hasArg(prefix + "StartOffsetMinutes")) { schedule.StartOffsetMinutes = (int16_t)server.arg(prefix + "StartOffsetMinutes").toInt(); }
            if(server.hasArg(prefix + "EndType")) { schedule.EndType = (uint8_t)server.arg(prefix + "EndType").toInt(); }
            if(server.hasArg(prefix + "EndHour")) { schedule.EndHour = (uint8_t)server.arg(prefix + "EndHour").toInt(); }
            if(server.hasArg(prefix + "EndMinute")) { schedule.EndMinute = (uint8_t)server.arg(prefix + "EndMinute").toInt(); }
            if(server.hasArg(prefix + "EndOffsetMinutes")) { schedule.EndOffsetMinutes = (int16_t)server.arg(prefix + "EndOffsetMinutes").toInt(); }
            if(server.hasArg(prefix + "DutyPercent")) { schedule.DutyPercent = (uint8_t)server.arg(prefix + "DutyPercent").toInt(); }

            if(schedule.StartType > 2u) { schedule.StartType = 0u; }
            if(schedule.EndType > 2u) { schedule.EndType = 0u; }
            if(schedule.StartHour > 23u) { schedule.StartHour = 23u; }
            if(schedule.EndHour > 23u) { schedule.EndHour = 23u; }
            if(schedule.StartMinute > 59u) { schedule.StartMinute = 59u; }
            if(schedule.EndMinute > 59u) { schedule.EndMinute = 59u; }
            if(schedule.StartOffsetMinutes < -720) { schedule.StartOffsetMinutes = -720; }
            if(schedule.StartOffsetMinutes > 720) { schedule.StartOffsetMinutes = 720; }
            if(schedule.EndOffsetMinutes < -720) { schedule.EndOffsetMinutes = -720; }
            if(schedule.EndOffsetMinutes > 720) { schedule.EndOffsetMinutes = 720; }
            if(schedule.DutyPercent > 100u) { schedule.DutyPercent = 100u; }

            Config.SetOutputSchedule(outputIndex, slotIndex, schedule);

            if(schedule.Enabled)
            {
                uint8_t activeDaysMask = 0u;
                for(uint8_t dayIndex = 0u; dayIndex < 7u; dayIndex++)
                {
                    if(server.hasArg(prefix + "Day" + String(dayIndex)))
                    {
                        activeDaysMask |= (uint8_t)(1u << dayIndex);
                    }
                }

                Config.SetOutputScheduleActiveDaysMask(outputIndex, slotIndex, activeDaysMask);
            }
        }
    }

    for(uint8_t outputIndex = 0u; outputIndex < ConfigModule::OUTPUT_COUNT; outputIndex++)
    {
        if(!HasOutputScheduleConflict(outputIndex))
        {
            continue;
        }

        for(uint8_t restoreOutputIndex = 0u; restoreOutputIndex < ConfigModule::OUTPUT_COUNT; restoreOutputIndex++)
        {
            for(uint8_t restoreSlotIndex = 0u; restoreSlotIndex < ConfigModule::OUTPUT_SCHEDULE_SLOT_COUNT; restoreSlotIndex++)
            {
                Config.SetOutputSchedule(restoreOutputIndex, restoreSlotIndex, previousSchedules[restoreOutputIndex][restoreSlotIndex]);
                Config.SetOutputScheduleActiveDaysMask(restoreOutputIndex, restoreSlotIndex, previousActiveDaysMasks[restoreOutputIndex][restoreSlotIndex]);
            }
        }

        server.send(409, "text/plain", "Schedule conflict: overlapping active periods are not allowed on the same selected day.");
        return;
    RequestMicrostepConfigApply();
    server.sendHeader("Location", "/");
    server.send(303);
}

#endif

void WebServerModule::HandleSaveStepperConfig()
{
    if(server.hasArg("stepper1MotorStepsPerRevolution"))
    {
        Config.SetAzimuthMotorStepsPerRevolution((uint16_t)server.arg("stepper1MotorStepsPerRevolution").toInt());
    }

    if(server.hasArg("stepper2MotorStepsPerRevolution"))
    {
        Config.SetElevationMotorStepsPerRevolution((uint16_t)server.arg("stepper2MotorStepsPerRevolution").toInt());
    }

    if(server.hasArg("stepper3MotorStepsPerRevolution"))
    {
        Config.SetStepper3MotorStepsPerRevolution((uint16_t)server.arg("stepper3MotorStepsPerRevolution").toInt());
    }

    if(server.hasArg("stepperMicrostepMode"))
    {
        Config.SetStepperMicrostepMode((uint8_t)server.arg("stepperMicrostepMode").toInt());
    }

    Config.SaveConfig();
    RequestMicrostepConfigApply();
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebServerModule::HandleResetStepperConfig()
{
    Config.SetAzimuthMotorStepsPerRevolution(200u);
    Config.SetElevationMotorStepsPerRevolution(200u);
    Config.SetStepper3MotorStepsPerRevolution(200u);
    Config.SetStepperMicrostepMode(8u);
    Config.SaveConfig();
    RequestMicrostepConfigApply();
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebServerModule::HandleSaveWiFiConfig()
{
    Config.SetWIFI_UseDHCP(server.hasArg("wifiUseDHCP"));

    if(server.hasArg("wifiSSID"))
    {
        Config.SetWIFI_SSID(server.arg("wifiSSID").c_str());
    }

    if(server.hasArg("wifiPassword"))
    {
        String password = server.arg("wifiPassword");
        if(password.length() > 0)
        {
            Config.SetWIFI_Password(password.c_str());
        }
    }

    if(server.hasArg("wifiSSID2"))
    {
        Config.SetWIFI_SSID2(server.arg("wifiSSID2").c_str());
    }

    if(server.hasArg("wifiPassword2"))
    {
        String password2 = server.arg("wifiPassword2");
        if(password2.length() > 0)
        {
            Config.SetWIFI_Password2(password2.c_str());
        }
    }

    if(server.hasArg("wifiStaticIp"))
    {
        Config.SetWIFI_StaticIP(server.arg("wifiStaticIp").c_str());
    }

    if(server.hasArg("wifiGateway"))
    {
        Config.SetWIFI_Gateway(server.arg("wifiGateway").c_str());
    }

    if(server.hasArg("wifiSubnet"))
    {
        Config.SetWIFI_SubnetMask(server.arg("wifiSubnet").c_str());
    }

    if(server.hasArg("wifiDns1"))
    {
        Config.SetWIFI_DNS1(server.arg("wifiDns1").c_str());
    }

    Config.SaveConfig();
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebServerModule::HandleResetWiFiConfig()
{
    Config.ResetWiFiConfig();
    Config.SaveConfig();
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebServerModule::HandleSaveNTPConfig()
{
    if(server.hasArg("ntpServer1")) { Config.SetNTP_Server1(server.arg("ntpServer1").c_str()); }
    if(server.hasArg("ntpServer2")) { Config.SetNTP_Server2(server.arg("ntpServer2").c_str()); }
    if(server.hasArg("ntpServer3")) { Config.SetNTP_Server3(server.arg("ntpServer3").c_str()); }

    Config.SaveConfig();
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebServerModule::HandleResetNTPConfig()
{
    Config.ResetNTP_Config();
    Config.SaveConfig();
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebServerModule::HandleSetOutputControl()
{
    String errorMessage;
    if(!ExecuteOutputActionFromRequest(errorMessage))
    {
        server.send(400, "text/plain", errorMessage);
        return;
    }

    server.sendHeader("Location", "/");
    server.send(303);
}

void WebServerModule::HandleSaveOutputNames()
{
    if(!server.hasArg("output"))
    {
        server.send(400, "text/plain", "Missing output parameter");
        return;
    }

    long outputNumber = server.arg("output").toInt();
    if(outputNumber < 1 || outputNumber > (long)ConfigModule::OUTPUT_COUNT)
    {
        server.send(400, "text/plain", "Invalid output index");
        return;
    }

    Config.SetOutputName((uint8_t)(outputNumber - 1), server.arg("name").c_str());
    Config.SaveConfig();

    server.sendHeader("Location", "/portal/outputs");
    server.send(303);
}

#if 0
void WebServerModule::HandleSaveScheduleConfig()
{
    for(uint8_t i = 0; i < 3; i++)
    {
        String prefix = "schedule" + String(i + 1);
        OutputSchedule_t schedule = Config.GetOutputSchedule(i);

        schedule.Enabled = server.hasArg(prefix + "Enabled");
        if(server.hasArg(prefix + "StartType")) 			{ schedule.StartType = 			(uint8_t)server.arg(prefix + "StartType").toInt(); }
        if(server.hasArg(prefix + "StartHour")) 			{ schedule.StartHour = 			(uint8_t)server.arg(prefix + "StartHour").toInt(); }
        if(server.hasArg(prefix + "StartMinute")) 			{ schedule.StartMinute = 		(uint8_t)server.arg(prefix + "StartMinute").toInt(); }
        if(server.hasArg(prefix + "StartOffsetMinutes")) 	{ schedule.StartOffsetMinutes = (int16_t)server.arg(prefix + "StartOffsetMinutes").toInt(); }
        if(server.hasArg(prefix + "EndType")) 				{ schedule.EndType = 			(uint8_t)server.arg(prefix + "EndType").toInt(); }
        if(server.hasArg(prefix + "EndHour")) 				{ schedule.EndHour = 			(uint8_t)server.arg(prefix + "EndHour").toInt(); }
        if(server.hasArg(prefix + "EndMinute")) 			{ schedule.EndMinute = 			(uint8_t)server.arg(prefix + "EndMinute").toInt(); }
        if(server.hasArg(prefix + "EndOffsetMinutes"))	 	{ schedule.EndOffsetMinutes = 	(int16_t)server.arg(prefix + "EndOffsetMinutes").toInt(); }
        if(server.hasArg(prefix + "DutyPercent")) 			{ schedule.DutyPercent = 		(uint8_t)server.arg(prefix + "DutyPercent").toInt(); }

        if(schedule.StartType > 2)				{ schedule.StartType = 0; }
        if(schedule.EndType > 2) 				{ schedule.EndType = 0; }
        if(schedule.StartOffsetMinutes < -720) 	{ schedule.StartOffsetMinutes = -720; }
        if(schedule.StartOffsetMinutes > 720) 	{ schedule.StartOffsetMinutes = 720; }
        if(schedule.EndOffsetMinutes < -720) 	{ schedule.EndOffsetMinutes = -720; }
        if(schedule.EndOffsetMinutes > 720) 	{ schedule.EndOffsetMinutes = 720; }
        if(schedule.StartHour > 23) 			{ schedule.StartHour = 23; }
        if(schedule.EndHour > 23) 				{ schedule.EndHour = 23; }
        if(schedule.StartMinute > 59) 			{ schedule.StartMinute = 59; }
        if(schedule.EndMinute > 59) 			{ schedule.EndMinute = 59; }
        if(schedule.DutyPercent > 100) 			{ schedule.DutyPercent = 100; }

        Config.SetOutputSchedule(i, schedule);
    }

    Config.SaveConfig();
    ApplyOutputSchedules();
    server.sendHeader("Location", "/");
    server.send(303);
}

#endif

void WebServerModule::HandleSaveScheduleConfig()
{
    OutputSchedule_t previousSchedules[ConfigModule::OUTPUT_COUNT][ConfigModule::OUTPUT_SCHEDULE_SLOT_COUNT];
    uint8_t previousActiveDaysMasks[ConfigModule::OUTPUT_COUNT][ConfigModule::OUTPUT_SCHEDULE_SLOT_COUNT];

    for(uint8_t outputIndex = 0u; outputIndex < ConfigModule::OUTPUT_COUNT; outputIndex++)
    {
        for(uint8_t slotIndex = 0u; slotIndex < ConfigModule::OUTPUT_SCHEDULE_SLOT_COUNT; slotIndex++)
        {
            previousSchedules[outputIndex][slotIndex] = Config.GetOutputSchedule(outputIndex, slotIndex);
            previousActiveDaysMasks[outputIndex][slotIndex] = Config.GetOutputScheduleActiveDaysMask(outputIndex, slotIndex);
        }
    }

    for(uint8_t outputIndex = 0u; outputIndex < ConfigModule::OUTPUT_COUNT; outputIndex++)
    {
        for(uint8_t slotIndex = 0u; slotIndex < ConfigModule::OUTPUT_SCHEDULE_SLOT_COUNT; slotIndex++)
        {
            String prefix = "schedule" + String(outputIndex + 1u);
            if(slotIndex > 0u)
            {
                prefix += "Slot";
                prefix += String(slotIndex + 1u);
            }

            OutputSchedule_t schedule = Config.GetOutputSchedule(outputIndex, slotIndex);
            schedule.Enabled = server.hasArg(prefix + "Enabled");
            if(server.hasArg(prefix + "StartType")) { schedule.StartType = (uint8_t)server.arg(prefix + "StartType").toInt(); }
            if(server.hasArg(prefix + "StartHour")) { schedule.StartHour = (uint8_t)server.arg(prefix + "StartHour").toInt(); }
            if(server.hasArg(prefix + "StartMinute")) { schedule.StartMinute = (uint8_t)server.arg(prefix + "StartMinute").toInt(); }
            if(server.hasArg(prefix + "StartOffsetMinutes")) { schedule.StartOffsetMinutes = (int16_t)server.arg(prefix + "StartOffsetMinutes").toInt(); }
            if(server.hasArg(prefix + "EndType")) { schedule.EndType = (uint8_t)server.arg(prefix + "EndType").toInt(); }
            if(server.hasArg(prefix + "EndHour")) { schedule.EndHour = (uint8_t)server.arg(prefix + "EndHour").toInt(); }
            if(server.hasArg(prefix + "EndMinute")) { schedule.EndMinute = (uint8_t)server.arg(prefix + "EndMinute").toInt(); }
            if(server.hasArg(prefix + "EndOffsetMinutes")) { schedule.EndOffsetMinutes = (int16_t)server.arg(prefix + "EndOffsetMinutes").toInt(); }
            if(server.hasArg(prefix + "DutyPercent")) { schedule.DutyPercent = (uint8_t)server.arg(prefix + "DutyPercent").toInt(); }

            if(schedule.StartType > 2u) { schedule.StartType = 0u; }
            if(schedule.EndType > 2u) { schedule.EndType = 0u; }
            if(schedule.StartHour > 23u) { schedule.StartHour = 23u; }
            if(schedule.EndHour > 23u) { schedule.EndHour = 23u; }
            if(schedule.StartMinute > 59u) { schedule.StartMinute = 59u; }
            if(schedule.EndMinute > 59u) { schedule.EndMinute = 59u; }
            if(schedule.StartOffsetMinutes < -720) { schedule.StartOffsetMinutes = -720; }
            if(schedule.StartOffsetMinutes > 720) { schedule.StartOffsetMinutes = 720; }
            if(schedule.EndOffsetMinutes < -720) { schedule.EndOffsetMinutes = -720; }
            if(schedule.EndOffsetMinutes > 720) { schedule.EndOffsetMinutes = 720; }
            if(schedule.DutyPercent > 100u) { schedule.DutyPercent = 100u; }

            Config.SetOutputSchedule(outputIndex, slotIndex, schedule);

            if(schedule.Enabled)
            {
                uint8_t activeDaysMask = 0u;
                for(uint8_t dayIndex = 0u; dayIndex < 7u; dayIndex++)
                {
                    if(server.hasArg(prefix + "Day" + String(dayIndex)))
                    {
                        activeDaysMask |= (uint8_t)(1u << dayIndex);
                    }
                }

                Config.SetOutputScheduleActiveDaysMask(outputIndex, slotIndex, activeDaysMask);
            }
        }
    }

    for(uint8_t outputIndex = 0u; outputIndex < ConfigModule::OUTPUT_COUNT; outputIndex++)
    {
        if(!HasOutputScheduleConflict(outputIndex))
        {
            continue;
        }

        for(uint8_t restoreOutputIndex = 0u; restoreOutputIndex < ConfigModule::OUTPUT_COUNT; restoreOutputIndex++)
        {
            for(uint8_t restoreSlotIndex = 0u; restoreSlotIndex < ConfigModule::OUTPUT_SCHEDULE_SLOT_COUNT; restoreSlotIndex++)
            {
                Config.SetOutputSchedule(restoreOutputIndex, restoreSlotIndex, previousSchedules[restoreOutputIndex][restoreSlotIndex]);
                Config.SetOutputScheduleActiveDaysMask(restoreOutputIndex, restoreSlotIndex, previousActiveDaysMasks[restoreOutputIndex][restoreSlotIndex]);
            }
        }

        server.send(409, "text/plain", "Schedule conflict: overlapping active periods are not allowed on the same selected day.");
        return;
    }

    // Keep the reported output mode in sync with the saved schedules.
    for(uint8_t outputIndex = 0u; outputIndex < ConfigModule::OUTPUT_COUNT; outputIndex++)
    {
        bool hasEnabledSchedule = false;

        for(uint8_t slotIndex = 0u; slotIndex < ConfigModule::OUTPUT_SCHEDULE_SLOT_COUNT; slotIndex++)
        {
            if(Config.GetOutputSchedule(outputIndex, slotIndex).Enabled)
            {
                hasEnabledSchedule = true;
                break;
            }
        }

        Config.SetOutputAutomaticMode(outputIndex, hasEnabledSchedule);
    }

    Config.SaveConfig();
    ApplyOutputSchedules();
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebServerModule::HandleResetScheduleConfig()
{
    Config.ResetOutputScheduleConfig();
    Config.ResetOutputAutomaticModeConfig();
    Config.SaveConfig();
    server.sendHeader("Location", "/");
    server.send(303);
}

void WebServerModule::HandleSetTrackingOverride()
{
    if(!server.hasArg("azimuth") || !server.hasArg("elevation"))
    {
        server.send(400, "text/plain", "Missing azimuth or elevation parameter");
        return;
    }

    float targetAzimuth = server.arg("azimuth").toFloat();
    float targetElevation = server.arg("elevation").toFloat();

    bool ok = SetTrackingTestOverride(targetAzimuth, targetElevation);
    if(!ok)
    {
        server.send(500, "text/plain", String("Tracking override failed: ") + GetTrackingOverrideLastFailureSummary());
        return;
    }

    server.send(200, "text/plain", "Tracking override active");
}

void WebServerModule::HandleCancelTrackingOverride()
{
    bool wasActive = IsTrackingTestOverrideActive();
    bool ok = CancelTrackingTestOverrideAndReturn();

    if(!ok)
    {
        server.send(500, "text/plain", wasActive ? "Override canceled, return failed" : "Return to current position failed");
        return;
    }

    server.send(200, "text/plain", wasActive ? "Override canceled, returned to current position" : "Returned to current position");
}

void WebServerModule::HandleJogTracking()
{
    // Direct motor jog: move motor by a specific angle increment.
    // Does NOT change the system's assumed panel position - for testing/diagnostics only.
    
    if(!server.hasArg("axis") || !server.hasArg("direction") || !server.hasArg("increment"))
    {
        server.send(400, "text/plain", "Missing axis, direction, or increment parameter");
        return;
    }

    String axis = server.arg("axis");
    int8_t direction = (int8_t)server.arg("direction").toInt();
    float increment = server.arg("increment").toFloat();

    if((axis != "azimuth" && axis != "elevation") || (direction != 1 && direction != -1) || increment <= 0.0f)
    {
        server.send(400, "text/plain", "Invalid parameter values");
        return;
    }

    bool ok = JogMotorDirect(axis.c_str(), direction, increment);
    if(!ok)
    {
        server.send(500, "text/plain", "Motor jog failed - stepper may still be moving or not initialized");
        return;
    }

    // Initialize jog session on first jog
    if(!jogSessionActive)
    {
        initializeJogSession();
    }

    if(axis == "azimuth")
    {
        jogSessionAzimuthDelta += (float)direction * increment;
    }
    else if(axis == "elevation")
    {
        jogSessionElevationDelta += (float)direction * increment;
    }

    // Get current delta from start position
    float azDelta = 0.0f;
    float elDelta = 0.0f;
    bool isActive = false;
    getJogSessionDelta(azDelta, elDelta, isActive);

    // Keep the status line simple and avoid repeating correction values here.
    String response = String("Motor jog: ") + axis + " " + (direction > 0 ? "+" : "-") + String(increment, 1) + "°";

    server.send(200, "text/plain", response);
}

void WebServerModule::initializeJogSession()
{
    jogSessionActive = true;
    jogSessionStartAzimuth = azimuthController.getCurrentAzimuth();
    jogSessionStartElevation = elevationController.getCurrentElevation();
    jogSessionAzimuthDelta = 0.0f;
    jogSessionElevationDelta = 0.0f;

  #ifdef USE_DEBUG_WEB_SERVER
    Serial.printf("[JOG_SESSION] Started at Az=%.1f El=%.1f\n", jogSessionStartAzimuth, jogSessionStartElevation);
  #endif
}

void WebServerModule::resetJogSession()
{
    jogSessionActive = false;
    jogSessionStartAzimuth = 0.0f;
    jogSessionStartElevation = 0.0f;
    jogSessionAzimuthDelta = 0.0f;
    jogSessionElevationDelta = 0.0f;

  #ifdef USE_DEBUG_WEB_SERVER
    Serial.println("[JOG_SESSION] Reset");
  #endif
}

void WebServerModule::getJogSessionDelta(float& azDeltaDeg, float& elDeltaDeg, bool& isActive)
{
    isActive = jogSessionActive;
    
    if(!jogSessionActive)
    {
        azDeltaDeg = 0.0f;
        elDeltaDeg = 0.0f;
        return;
    }

    azDeltaDeg = jogSessionAzimuthDelta;
    elDeltaDeg = jogSessionElevationDelta;
}

void WebServerModule::HandleGetJogSessionDelta()
{
    float azDelta = 0.0f;
    float elDelta = 0.0f;
    bool isActive = false;
    
    getJogSessionDelta(azDelta, elDelta, isActive);
    
    if(!isActive)
    {
        server.send(200, "text/plain", "inactive");
        return;
    }

    String response = "azimuth=" + String(azDelta, 1) + "\n";
    response += "elevation=" + String(elDelta, 1);
    
    server.send(200, "text/plain", response);
}

void WebServerModule::HandleGetSensorLogsManifest()
{
    server.send(200, "application/json", GetSensorLogsManifestJson());
}

void WebServerModule::HandleStorageSelfTest()
{
    server.send(200, "text/plain", GetStorageSelfTestReport());
}

void WebServerModule::handleSensorLogsPage()
{
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");

    File file = SPIFFS.open("/portal/logs.html", "r");
    if(!file)
    {
        send404("portal/logs.html not found");
        return;
    }

    server.streamFile(file, "text/html");
    file.close();
}

void WebServerModule::HandleDownloadSensorLog()
{
    if(!server.hasArg("name"))
    {
		send404("Missing name parameter");
        return;
    }

    String requestedName = server.arg("name");
    uint32_t fileSize = 0;
    if(!GetSensorLogFileInfo(requestedName, fileSize))
    {
		send404("Log file not found");
        return;
    }

    server.setContentLength(fileSize);
    server.sendHeader("Content-Type", "application/octet-stream");
    server.sendHeader("Content-Disposition", String("attachment; filename=") + requestedName);
    server.send(200);

    WiFiClient client = server.client();
    uint8_t buffer[512];
    uint32_t offset = 0;

    while(offset < fileSize && client.connected())
    {
        size_t chunk = sizeof(buffer);
        uint32_t remaining = fileSize - offset;
        if(chunk > remaining)
        {
            chunk = remaining;
        }

        if(!ReadSensorLogFileRange(requestedName, offset, buffer, chunk))
        {
            break;
        }

        size_t written = client.write(buffer, chunk);
        if(written != chunk)
        {
            break;
        }

        offset += (uint32_t)chunk;
    }
}

void WebServerModule::handleRestartSystem()
{
    server.send(200, "application/json", "{\"message\":\"System is restarting...\"}");
    restartRequested = true;
}

void WebServerModule::HandleNotFound()
{
  #ifdef USE_DEBUG_WEB_SERVER
    Serial.printf("[Web] 404 %s\n", server.uri().c_str());
  #endif
    send404(server.uri().c_str());
}

void WebServerModule::send404(const String &msg)
{
    File file = SPIFFS.open("/404.html", "r");
    if(!file){
        server.send(404, "text/plain", "404 file missing");
        return;
    }

    String html = file.readString();
    file.close();

    html.replace("{{msg}}", msg);

    server.send(404, "text/html", html);
}

bool WebServerModule::handleFileRead(String path)
{
    if(SPIFFS.exists(path))
	{
        File file = SPIFFS.open(path, "r");
        server.streamFile(file, getContentType(path));
        file.close();
        return true;
    }
	
    return false;
}

String WebServerModule::getContentType(String filename)
{
    if (filename.endsWith(".htm") || filename.endsWith(".html")) return "text/html";
    if (filename.endsWith(".css")) 	return "text/css";
    if (filename.endsWith(".js")) 	return "application/javascript";
    if (filename.endsWith(".png")) 	return "image/png";
    if (filename.endsWith(".jpg")) 	return "image/jpeg";
    if (filename.endsWith(".gif")) 	return "image/gif";
    if (filename.endsWith(".ico")) 	return "image/x-icon";
    return "text/plain";
}
