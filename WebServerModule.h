#ifndef MODULE_WEBSERVER_H
#define MODULE_WEBSERVER_H

#include <Arduino.h>
#include <WebServer.h>

class WebServerModule
{
    public:

                        WebServerModule                     ();
    
        void            begin                               ();
        void            Loop                                ();
        bool            isRestartRequested                  ()                              { return restartRequested; }

    private:


        void            initSPIFFS                          ();
        void            setupServer                         ();
		void 			send404								(const String &msg);
		bool            handleFileRead						(String path);
		String 			getContentType						(String filename);
        void            handleRestartSystem                 ();
        void            handleConfigPage                    ();
        void            handleDiagnosticsPage               ();
        void            handleSensorLogsPage                ();
        void            HandleSaveSolarTrackingConfig       ();
        void            HandleResetSolarTrackingConfig      ();
        void            HandleSaveAzimuthConfig             ();
        void            HandleResetAzimuthConfig            ();
        void            HandleSaveElevationConfig           ();
        void            HandleResetElevationConfig          ();
        void            HandleSaveStepperConfig             ();
        void            HandleResetStepperConfig            ();
        void            HandleSaveWiFiConfig                ();
        void            HandleResetWiFiConfig               ();
        void            HandleSaveNTPConfig                 ();
        void            HandleResetNTPConfig                ();
        void            HandleSetOutputControl              ();
        void            HandleSaveOutputNames               ();
        void            HandleSaveScheduleConfig            ();
        void            HandleResetScheduleConfig           ();
        void            HandleSetTrackingOverride           ();
        void            HandleCancelTrackingOverride        ();
        void            HandleJogTracking                  ();
        void            HandleGetJogSessionDelta           ();
        void            HandleGetTrackingOverrideStatus     ();
        void            HandleGetPortalStatus               ();
        void            HandleGetApiConfig                  ();
        void            HandleSaveApiConfig                 ();
        void            HandleGetApiStatus                  ();
        void            HandleApiOutput                     ();
        bool            ExecuteOutputActionFromRequest      (String& errorMessage);
        void            HandleGetSensorLogsManifest         ();
        void            HandleDownloadSensorLog             ();
        void            HandleNotFound                      ();
        String          formatOutputState                   (uint8_t outputIndex);
        String          getWifiModeLabel                    ();
        String          getWifiConnectionStatusLabel        ();
        String          getWifiDisplayValue                 (const char* value, bool automaticWhenDhcp) const;
        String          getCurrentWifiRssi                  ();
        String          getCurrentWifiIp                    ();
        String          getCurrentWifiGateway               ();
        String          getCurrentWifiSubnet                ();
        String          getCurrentWifiDns                   ();
        String          selectTemplatePath                  (const char* desktopPath);
        void            sendEmbeddedConfigFallbackPage      ();
        void            sendEmbeddedDiagnosticsFallbackPage ();
        void            initializeJogSession                ();
        void            resetJogSession                     ();
        void            getJogSessionDelta                  (float& azDeltaDeg, float& elDeltaDeg, bool& isActive);

        volatile bool   restartRequested = false;
        bool            serverStarted = false;
        
        // Jog session tracking
        bool            jogSessionActive = false;
        float           jogSessionStartAzimuth = 0.0f;
        float           jogSessionStartElevation = 0.0f;
        float           jogSessionAzimuthDelta = 0.0f;
        float           jogSessionElevationDelta = 0.0f;

        WebServer               server;
};

#endif // MODULE_WEBSERVER_H