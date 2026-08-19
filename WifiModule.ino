#include <time.h>
#include "WifiModule.h"
#include "ConfigModule.h"
#if __has_include(<esp_netif.h>)
#include <esp_netif.h>
#define WIFI_MODULE_HAS_ESP_NETIF 1
#else
#define WIFI_MODULE_HAS_ESP_NETIF 0
#endif

#ifndef WIFI_MODULE_USE_EXPLICIT_AP_DHCP_CONTROL
#define WIFI_MODULE_USE_EXPLICIT_AP_DHCP_CONTROL 0
#endif

//#define USE_DEBUG_WIFI_MODULE
#define USER_BUTTON_PIN 0

extern ConfigModule Config;
WifiModule* WifiModule::m_pInstance = nullptr;


WifiModule::WifiModule() : m_pSSID(nullptr), m_pPassword(nullptr), m_pSSID2(nullptr), m_pPassword2(nullptr), m_ApModeActive(false), m_StaConfigured(false), m_ApRestartRequested(false), m_ApTransitionInProgress(false), m_LastApStartAttempt(0), m_LastApDiagLog(0), m_LastReconnectAttempt(0), m_FailureCount(0), m_ReconnectDelay(5000)
{
    WifiModule::m_pInstance = this;
}

/**
* Initializes Wi-Fi.
* 
* - Reads SSID and password from some config module (not shown here).
* - Disables auto-reconnect for a more predictable manual approach.
* - Sets station mode, registers the event callback, and attempts the first connection.
*/
void WifiModule::Initialize()
{
    pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
    bool forceAP = (digitalRead(USER_BUTTON_PIN) == LOW);

    WiFi.onEvent(HandleWiFiEvent);
    RefreshStationCredentials();

    // --- Mode AP force by button USER ---
    if(forceAP)
    {
      #ifdef USE_DEBUG_WIFI_MODULE
        Serial.println("[WiFi] AP mode forced by user button.");
      #endif
        m_StaConfigured = false;
        StartAccessPointMode();
        return;
    }

    // --- Pas de SSID configuré ---
    if(!HasStationCredentials(m_pSSID) && !HasStationCredentials(m_pSSID2))
    {
      #ifdef USE_DEBUG_WIFI_MODULE
        Serial.println("[WiFi] No SSID configured. Starting setup AP mode.");
      #endif
        m_StaConfigured = false;
        StartAccessPointMode();
        return;
    }

    // --- Mode STA normal ---
    m_StaConfigured = true;
    m_ApModeActive = false;

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    bool connected = ConnectToStationWithFallback(8000);
    WiFi.macAddress(m_MacAddr);

  #ifdef USE_DEBUG_WIFI_MODULE
    Serial.println("[WiFi] Initialization complete. Attempting connection...");
  #endif

    if(connected)
    {
      #ifdef USE_DEBUG_WIFI_MODULE
        Serial.println("[WiFi] Connected to WiFi.");
      #endif

        // --- NTP resync at boot ---
        configTime(0, 0,
                   Config.GetNTP_Server1(),
                   Config.GetNTP_Server2(),
                   Config.GetNTP_Server3());

      #ifdef USE_DEBUG_WIFI_MODULE
        Serial.println("[WiFi] Boot-time NTP sync requested.");
      #endif

        // --- Wait for NTP sync ---
        for(int i = 0; i < 10; i++)
        {
            time_t now = time(nullptr);
            if(now > 1700000000)
            {
              #ifdef USE_DEBUG_WIFI_MODULE
                struct tm t;
                gmtime_r(&now, &t);
                Serial.printf("[WiFi] NTP sync OK: %04d-%02d-%02d %02d:%02d:%02d\n",
                              t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                              t.tm_hour, t.tm_min, t.tm_sec);
              #endif
                break;
            }

            delay(500);
        }

        return;
    }

  #ifdef USE_DEBUG_WIFI_MODULE
    Serial.println("[WiFi] Connection failed. Starting AP fallback.");
  #endif
    m_StaConfigured = false;
    StartAccessPointMode();
}

/**
* Must be called regularly (e.g., from the Arduino loop() or a dedicated FreeRTOS task).
* 
* - Checks Wi-Fi status.
* - If disconnected, attempts reconnection at intervals determined by an internal timer.
* - Supports an exponential backoff (optional) to avoid spamming the network.
* - Falls back to setup AP mode after too many consecutive failures.
*/
void WifiModule::Loop()
{
    uint32_t now = millis();
    if(now - m_LastApDiagLog >= m_ApDiagIntervalMs)
    {
        m_LastApDiagLog = now;
      #ifdef USE_DEBUG_WIFI_MODULE
        Serial.printf("[WiFi][DIAG] mode=%d status=%d apActive=%d staCfg=%d apIP=%s clients=%u\n",
                       (int)WiFi.getMode(),
                       (int)WiFi.status(),
                       m_ApModeActive ? 1 : 0,
                       m_StaConfigured ? 1 : 0,
                       WiFi.softAPIP().toString().c_str(),
                       WiFi.softAPgetStationNum());
      #endif
    }

    if(!m_StaConfigured && m_ApRestartRequested && !m_ApModeActive)
    {
        if(millis() - m_LastApStartAttempt < m_ApRetryDelayMs)
        {
            return;
        }

        m_ApRestartRequested = false;
      #ifdef USE_DEBUG_WIFI_MODULE
        Serial.println("[WiFi] AP restart requested after AP stop event.");
      #endif    
        StartAccessPointMode();
        return;
    }

    if(m_ApModeActive)
    {
        return;
    }

    if(WiFi.status() == WL_CONNECTED)
    {
        if(m_FailureCount != 0)
        {
            m_FailureCount = 0;
            m_ReconnectDelay = 5000;
          #ifdef USE_DEBUG_WIFI_MODULE
            Serial.println("[WiFi] Connection stable. Failure count reset.");
          #endif
        }
        
        return;
    }

      RefreshStationCredentials();

    uint32_t Now = millis();
    
    if(Now - m_LastReconnectAttempt >= m_ReconnectDelay)
    {
        m_LastReconnectAttempt = Now;
        m_FailureCount++;

      #ifdef USE_DEBUG_WIFI_MODULE
        char Buff[100];
        snprintf(Buff, sizeof(Buff), "[WiFi] Not connected. Attempting reconnection #%d...", m_FailureCount);
        Serial.println(Buff);
      #endif
      
        bool reconnectStarted = ConnectToStationWithFallback(0);

      #ifdef USE_DEBUG_WIFI_MODULE
        if(!reconnectStarted)
        {
            Serial.println("[WiFi] Reconnect skipped: no valid SSID configured.");
        }
      #endif

        m_ReconnectDelay = (m_ReconnectDelay < 60000) ? m_ReconnectDelay * 2 : 60000; // cap at 1 minute

        if(m_FailureCount >= m_MaxReconnectAttempts)
        {
          #ifdef USE_DEBUG_WIFI_MODULE
            Serial.println("[WiFi] Maximum reconnect attempts reached. Switching to AP fallback mode.");
          #endif          
            m_FailureCount = 0;
            m_ReconnectDelay = 5000;
            m_StaConfigured = false;
            StartAccessPointMode();
            return;
        }
    }
	
		// --- NTP periodic resync ---
		if (WiFi.status() == WL_CONNECTED)
	{
		if (now - m_LastNtpSync > 6UL * 3600UL * 1000UL)  // toutes les 6 heures
		{
			m_LastNtpSync = now;

			configTime(0, 0,
					   Config.GetNTP_Server1(),
					   Config.GetNTP_Server2(),
					   Config.GetNTP_Server3());

			#ifdef USE_DEBUG_WIFI_MODULE
			Serial.println("[WiFi] Periodic NTP resync requested.");
			#endif
		}
	}
}

void WifiModule::RefreshStationCredentials()
{
    m_pSSID = Config.GetWIFI_SSID();
    m_pPassword = Config.GetWIFI_Password();
  m_pSSID2 = Config.GetWIFI_SSID2();
  m_pPassword2 = Config.GetWIFI_Password2();
}

bool WifiModule::HasStationCredentials(const char* ssid) const
{
  return ssid != nullptr && strlen(ssid) > 0;
}

bool WifiModule::BeginStationConnectionForSlot(uint8_t slot, uint32_t timeoutMs)
{
  const char* ssid = nullptr;
  const char* password = nullptr;

  if(slot == 1u)
  {
    ssid = m_pSSID;
    password = m_pPassword;
  }
  else if(slot == 2u)
  {
    ssid = m_pSSID2;
    password = m_pPassword2;
  }
  else
  {
    return false;
  }

  if(!HasStationCredentials(ssid))
  {
    return false;
  }

  WiFi.disconnect();
  ApplyStationNetworkConfig();
  WiFi.begin(ssid, password);
  m_LastAttemptedWifiSlot = slot;

  if(timeoutMs == 0u)
  {
    #ifdef USE_DEBUG_WIFI_MODULE
    Serial.printf("[WiFi] Reconnect attempt started on SSID slot %u (%s).\n", slot, ssid);
    #endif
    return true;
  }

  unsigned long start = millis();
  while(WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs)
  {
    delay(200);
  }

  if(WiFi.status() == WL_CONNECTED)
  {
    #ifdef USE_DEBUG_WIFI_MODULE
    Serial.printf("[WiFi] Connected using SSID slot %u (%s).\n", slot, ssid);
    #endif
    return true;
  }

  #ifdef USE_DEBUG_WIFI_MODULE
  Serial.printf("[WiFi] Connection timeout on SSID slot %u (%s).\n", slot, ssid);
  #endif
  return false;
}

bool WifiModule::ConnectToStationWithFallback(uint32_t timeoutMs)
{
  RefreshStationCredentials();

  bool hasPrimary = HasStationCredentials(m_pSSID);
  bool hasSecondary = HasStationCredentials(m_pSSID2);

  if(!hasPrimary && !hasSecondary)
  {
    return false;
  }

  if(timeoutMs == 0u)
  {
    uint8_t firstSlot = (m_LastAttemptedWifiSlot == 1u) ? 2u : 1u;
    uint8_t secondSlot = (firstSlot == 1u) ? 2u : 1u;

    if(BeginStationConnectionForSlot(firstSlot, 0u))
    {
      return true;
    }

    return BeginStationConnectionForSlot(secondSlot, 0u);
  }

  if(BeginStationConnectionForSlot(1u, timeoutMs))
  {
    return true;
  }

  return BeginStationConnectionForSlot(2u, timeoutMs);
}

bool WifiModule::ParseIPAddressString(const char* value, IPAddress& address)
{
    if(value == nullptr || strlen(value) == 0)
    {
      return false;
    }

    return address.fromString(value);
}

bool WifiModule::ConfigureStationDhcp()
{
    const IPAddress autoAddress((uint32_t)0u);
    // Temporary override to force a known DNS while investigating NTP resolution.
    // Expected DHCP-driven behavior would be:
    // return WiFi.config(autoAddress, autoAddress, autoAddress, autoAddress, autoAddress);
    return WiFi.config(autoAddress, autoAddress, autoAddress, IPAddress(8,8,8,8), autoAddress);
}

bool WifiModule::ApplyStationNetworkConfig()
{
    if(Config.GetWIFI_UseDHCP())
    {
        return ConfigureStationDhcp();
    }

    IPAddress localIp;
    IPAddress gateway;
    IPAddress subnet;

    bool hasLocalIp = ParseIPAddressString(Config.GetWIFI_StaticIP(), localIp);
    bool hasGateway = ParseIPAddressString(Config.GetWIFI_Gateway(), gateway);
    bool hasSubnet = ParseIPAddressString(Config.GetWIFI_SubnetMask(), subnet);

    if(!hasLocalIp || !hasGateway || !hasSubnet)
    {
        #ifdef USE_DEBUG_WIFI_MODULE
        Serial.println("[WiFi] Static STA config is incomplete or invalid. Falling back to DHCP.");
        #endif
        ConfigureStationDhcp();
        return false;
    }

    const IPAddress emptyAddress((uint32_t)0u);
    // Temporary override to force a known DNS while investigating NTP resolution.
    // Expected config-driven behavior would be:
    IPAddress dns1;
    bool hasDns1 = ParseIPAddressString(Config.GetWIFI_DNS1(), dns1);
    bool configured = WiFi.config(localIp,
                     gateway,
                     subnet,
                     hasDns1 ? dns1 : emptyAddress);
    //bool configured = WiFi.config(localIp,
    //                  gateway,
    //                  subnet,
  	//	      		  IPAddress(8,8,8,8), emptyAddress);

    #ifdef USE_DEBUG_WIFI_MODULE
    Serial.printf("[WiFi] STA network config applied. dhcp=%d ip=%s gw=%s mask=%s dns1=%s result=%d\n",
             Config.GetWIFI_UseDHCP() ? 1 : 0,
             Config.GetWIFI_StaticIP(),
             Config.GetWIFI_Gateway(),
             Config.GetWIFI_SubnetMask(),
             Config.GetWIFI_DNS1(),
             configured ? 1 : 0);
    #endif

    return configured;
}

void WifiModule::StartAccessPointMode()
{
    if(m_ApModeActive || m_ApTransitionInProgress)
    {
        return;
    }

    m_ApTransitionInProgress = true;
    m_ApRestartRequested = false;
    m_LastApStartAttempt = millis();

    WiFi.persistent(false);
    WiFi.setSleep(false);

    // Clear both STA and AP state before rebuilding AP mode.
    WiFi.disconnect();
    WiFi.softAPdisconnect(true);
    delay(80);

    WiFi.mode(WIFI_MODE_NULL);
    delay(80);

    bool networkOk = false;
    bool apOk = false;
    bool dhcpOk = true;
    bool ipReady = false;

    // Try AP-only first.
    WiFi.mode(WIFI_AP);
    delay(220);
    networkOk = ConfigureAccessPointNetwork();
    apOk = WiFi.softAP(m_DefaultApSsid);

    if(!apOk)
    {
      // Retry by rebuilding netifs and allowing AP+STA dual mode fallback.
        WiFi.mode(WIFI_MODE_NULL);
        delay(80);
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_AP_STA);
        delay(220);
        networkOk = ConfigureAccessPointNetwork();
        apOk = WiFi.softAP(m_DefaultApSsid);
    }

    if(!apOk)
    {
      // Final retry back on AP-only.
      WiFi.mode(WIFI_MODE_NULL);
      delay(80);
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_AP);
      delay(220);
      networkOk = ConfigureAccessPointNetwork();
      apOk = WiFi.softAP(m_DefaultApSsid);
    }

    if(apOk)
    {
        for(int i = 0; i < 20; i++)
        {
            if(WiFi.softAPIP() != IPAddress((uint32_t)0))
            {
                ipReady = true;
                break;
            }

            delay(50);
        }

        dhcpOk = EnsureAccessPointDhcpServer();
    }

    // Keep AP active if softAP is up, even if extra DHCP introspection is not available.
    m_ApModeActive = apOk;
    m_ApTransitionInProgress = false;

    if(m_ApModeActive == true)
    {
        m_LastApDiagLog = 0;
      #ifdef USE_DEBUG_WIFI_MODULE
        Serial.printf("[WiFi] AP mode started. SSID=%s IP=%s net=%d dhcp=%d\n",
                       m_DefaultApSsid,
                       WiFi.softAPIP().toString().c_str(),
                       networkOk ? 1 : 0,
                       dhcpOk ? 1 : 0);
      #endif
    }
    else
    {
      #ifdef USE_DEBUG_WIFI_MODULE
        Serial.printf("[WiFi] Failed to start AP mode. softAP=%d net=%d dhcp=%d\n",
                       apOk ? 1 : 0,
                       networkOk ? 1 : 0,
                       dhcpOk ? 1 : 0);
      #endif

        if(!m_StaConfigured)
        {
            // AP-only mode: request another try from Loop() if startup failed.
            m_ApRestartRequested = true;
        }
    }
}

bool WifiModule::ConfigureAccessPointNetwork()
{
    const IPAddress apIp(192, 168, 4, 1);
    const IPAddress apGateway(192, 168, 4, 1);
    const IPAddress apSubnet(255, 255, 255, 0);

    bool configured = WiFi.softAPConfig(apIp, apGateway, apSubnet);

    if(!configured)
    {
      #ifdef USE_DEBUG_WIFI_MODULE
        Serial.println("[WiFi] softAPConfig failed.");
      #endif    
        return false;
    }

    return true;
}

bool WifiModule::EnsureAccessPointDhcpServer()
{
#if !WIFI_MODULE_USE_EXPLICIT_AP_DHCP_CONTROL
    return true;
#endif

#if WIFI_MODULE_HAS_ESP_NETIF
    esp_netif_t* apNetif = nullptr;
    for(int i = 0; i < 10; i++)
    {
        apNetif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        if(apNetif != nullptr)
        {
            break;
        }

        delay(20);
    }

    if(apNetif == nullptr)
    {
      #ifdef USE_DEBUG_WIFI_MODULE
        Serial.println("[WiFi] AP netif handle not found. Keeping AP without explicit DHCP control.");
	  #endif	
        return true;
    }

    esp_netif_dhcp_status_t status = ESP_NETIF_DHCP_INIT;
    esp_err_t statusErr = ESP_FAIL;
    for(int i = 0; i < 10; i++)
    {
        statusErr = esp_netif_dhcps_get_status(apNetif, &status);
        if(statusErr == ESP_OK)
        {
            break;
        }

        delay(20);
    }

    if(statusErr != ESP_OK)
    {
      #ifdef USE_DEBUG_WIFI_MODULE		
        Serial.println("[WiFi] Failed to read AP DHCP status. Keeping AP without explicit DHCP control.");
	  #endif	
        return true;
    }

    if(status != ESP_NETIF_DHCP_STARTED)
    {
        if(status == ESP_NETIF_DHCP_STOPPED)
        {
            if(esp_netif_dhcps_start(apNetif) != ESP_OK)
            {
			  #ifdef USE_DEBUG_WIFI_MODULE				
                Serial.println("[WiFi] Failed to start AP DHCP server.");
			  #endif	
                return false;
            }
        }
        else
        {
            // For transient INIT state, wait for internal startup before forcing changes.
            for(int i = 0; i < 20; i++)
            {
                if(esp_netif_dhcps_get_status(apNetif, &status) == ESP_OK && status == ESP_NETIF_DHCP_STARTED)
                {
                    break;
                }

                delay(25);
            }

            if(status != ESP_NETIF_DHCP_STARTED)
            {
                if(esp_netif_dhcps_start(apNetif) != ESP_OK)
                {
				  #ifdef USE_DEBUG_WIFI_MODULE					
                    Serial.println("[WiFi] Failed to force-start AP DHCP server.");
				  #endif	
                    return false;
                }
            }
        }

        esp_netif_dhcp_status_t startedStatus = ESP_NETIF_DHCP_INIT;
        if(esp_netif_dhcps_get_status(apNetif, &startedStatus) != ESP_OK || startedStatus != ESP_NETIF_DHCP_STARTED)
        {
          #ifdef USE_DEBUG_WIFI_MODULE			
            Serial.println("[WiFi] AP DHCP server did not reach STARTED state.");
		  #endif	
            return false;
        }
    }
#endif

    return true;
}

void WifiModule::HandleWiFiEvent(WiFiEvent_t Event, WiFiEventInfo_t Info)
{
    switch(Event)
    {
        case IP_EVENT_STA_GOT_IP:
        {
            IPAddress ip = IPAddress(Info.got_ip.ip_info.ip.addr);
		  #ifdef USE_DEBUG_WIFI_MODULE			
            Serial.printf("[WiFi] Connected with IP: %s (SSID slot %u)\n", ip.toString().c_str(), WifiModule::m_pInstance->m_LastAttemptedWifiSlot);
          #endif     
	        WifiModule::m_pInstance->m_ApModeActive = false;
            configTime(0, 0, Config.GetNTP_Server1(), Config.GetNTP_Server2(), Config.GetNTP_Server3());
		  #ifdef USE_DEBUG_WIFI_MODULE			
            Serial.println("[WiFi] NTP sync requested.");
          #endif     
        }
        break;
        
        case WIFI_EVENT_STA_DISCONNECTED:
        {
		  #ifdef USE_DEBUG_WIFI_MODULE			
            Serial.println("[WiFi] Disconnected from AP.");
		  #endif	
        }
        break;

        case WIFI_EVENT_AP_START:
        {
            WifiModule::m_pInstance->m_ApModeActive = true;
            WifiModule::m_pInstance->m_ApRestartRequested = false;
            WifiModule::m_pInstance->m_ApTransitionInProgress = false;
		  #ifdef USE_DEBUG_WIFI_MODULE			
            Serial.println("[WiFi] AP start event.");
		  #endif	
        }
        break;

        case WIFI_EVENT_AP_STOP:
        {
            WifiModule::m_pInstance->m_ApModeActive = false;
		  #ifdef USE_DEBUG_WIFI_MODULE			
            Serial.println("[WiFi] AP stop event.");
		  #endif

            if(WifiModule::m_pInstance->m_ApTransitionInProgress)
            {
     		  #ifdef USE_DEBUG_WIFI_MODULE			
                Serial.println("[WiFi] AP stop during transition (expected).");
     		  #endif
                break;
            }

            // In AP-only mode (no STA config), always try to bring AP back.
            if(!WifiModule::m_pInstance->m_StaConfigured)
            {
                WifiModule::m_pInstance->m_ApRestartRequested = true;
            }
        }
        break;

        case WIFI_EVENT_AP_STACONNECTED:
        {
   		  #ifdef USE_DEBUG_WIFI_MODULE			
            Serial.printf("[WiFi] AP station connected. aid=%u\n", Info.wifi_ap_staconnected.aid);
		  #endif
        }
        break;

        case WIFI_EVENT_AP_STADISCONNECTED:
        {
   		  #ifdef USE_DEBUG_WIFI_MODULE			
            Serial.printf("[WiFi] AP station disconnected. aid=%u reason=%u\n", Info.wifi_ap_stadisconnected.aid, Info.wifi_ap_stadisconnected.reason);
		  #endif
        }
        break;
        
        default: break;
    }
}