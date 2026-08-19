#ifndef MODULE_WIFI_H
#define MODULE_WIFI_H

#include <Arduino.h>
#include <WiFi.h>

/**
 * WifiModule
 * 
 * A class to manage Wi-Fi connection on ESP32 with manual (explicit) reconnection
 * logic, optional exponential backoff, and a maximum reconnect threshold to
 * fall back to setup AP mode if Wi-Fi fails consistently.
 */
class WifiModule
{
    public:
                        WifiModule          ();

        void            Initialize          ();
        void            Loop                ();
        uint8_t*        getMacAddr          () { return m_MacAddr; }
        
    private:

        static void     HandleWiFiEvent     (WiFiEvent_t event, WiFiEventInfo_t info);
        void            RefreshStationCredentials();
        bool            HasStationCredentials(const char* ssid) const;
        bool            ConnectToStationWithFallback(uint32_t timeoutMs);
        bool            BeginStationConnectionForSlot(uint8_t slot, uint32_t timeoutMs);
        bool            ApplyStationNetworkConfig();
        bool            ConfigureStationDhcp();
        bool            ParseIPAddressString(const char* value, IPAddress& address);
        void            StartAccessPointMode();
        bool            ConfigureAccessPointNetwork();
        bool            EnsureAccessPointDhcpServer();
        
        const char*             m_pSSID;
        const char*             m_pPassword;
        const char*             m_pSSID2;
        const char*             m_pPassword2;
        bool                    m_ApModeActive;
        bool                    m_StaConfigured;
        bool                    m_ApRestartRequested;
        bool                    m_ApTransitionInProgress;

        uint8_t                 m_MacAddr[6];
        uint32_t                m_LastApStartAttempt;
        uint32_t                m_LastApDiagLog;
        uint32_t                m_LastReconnectAttempt;
        uint32_t                m_ReconnectDelay;
        int                     m_FailureCount;
		uint32_t 				m_LastNtpSync = 0;
        uint8_t                 m_LastAttemptedWifiSlot = 0;

        static WifiModule*      m_pInstance;
        static constexpr int    m_MaxReconnectAttempts = 10;  // After x fails, do ESP.restart()
        static constexpr uint32_t m_ApRetryDelayMs = 2000;
        static constexpr uint32_t m_ApDiagIntervalMs = 2000;
        static constexpr const char* m_DefaultApSsid = "CtrlStation";

};

#endif // MODULE_WIFI_H