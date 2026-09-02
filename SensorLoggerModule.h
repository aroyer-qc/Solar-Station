//-------------------------------------------------------------------------------------------------
//
//  File : SensorLoggerModule.h
//
//-------------------------------------------------------------------------------------------------
//
// Copyright(c) 2025 Alain Royer.
// Email: aroyer.qc@gmail.com
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to Use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
// AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
//-------------------------------------------------------------------------------------------------
//
//  Generic day-sliced data logger. Every registered channel averages its samples over a window and
//  stores the result as a raw float in "/<Prefix>_<YYMMDD>.bin". The slot index is derived from the
//  local time of day, so sample N always maps to N * AverageWindowMs after midnight. Slots with no
//  recorded value hold a NaN.
//
//-------------------------------------------------------------------------------------------------

#ifndef SENSOR_LOGGER_MODULE_H
#define SENSOR_LOGGER_MODULE_H

//-------------------------------------------------------------------------------------------------
// Include file(s)
//-------------------------------------------------------------------------------------------------

#include <Arduino.h>
#include "M95PxxModule.h"

//-------------------------------------------------------------------------------------------------
// Define(s)
//-------------------------------------------------------------------------------------------------

#ifndef SENSOR_LOGGER_MAX_CHANNEL
#define SENSOR_LOGGER_MAX_CHANNEL           16
#endif

#define SENSOR_LOGGER_INVALID_CHANNEL       -1

//-------------------------------------------------------------------------------------------------
// Typedef(s)
//-------------------------------------------------------------------------------------------------

typedef float (*SensorLogProvider_t)();

//-------------------------------------------------------------------------------------------------
// Class definition(s)
//-------------------------------------------------------------------------------------------------

class SensorLoggerModule
{
    public:

                        SensorLoggerModule          ();

        void            Begin                       (M95PxxModule* pStorage);

        // Provider is polled by Update(); pass nullptr and feed the channel with PushSample().
        int8_t          RegisterChannel             (const char* pFilePrefix,
                                                     const char* pTypeName,
                                                     const char* pLabel,
                                                     const char* pUnit,
                                                     uint32_t SampleIntervalMs,
                                                     uint32_t AverageWindowMs,
                                                     SensorLogProvider_t Provider = nullptr);

        void            Update                      (uint32_t NowMs);
        bool            PushSample                  (int8_t ChannelID, float Value);

        String          GetManifestJson             ();
        bool            GetFileInfo                 (const String& RequestedName, uint32_t& OutSizeBytes);
        bool            ReadFileRange               (const String& RequestedName, uint32_t OffsetBytes, uint8_t* pBuffer, size_t LengthBytes);

        uint8_t         GetChannelCount             () const                    { return m_ChannelCount;      }
        void            SetRetentionThreshold       (uint8_t Percent)           { m_RetentionPercent = Percent; }

    private:

        struct Channel_t
        {
            const char*         pFilePrefix;
            const char*         pTypeName;
            const char*         pLabel;
            const char*         pUnit;
            uint32_t            SampleIntervalMs;
            uint32_t            AverageWindowMs;
            SensorLogProvider_t Provider;
            uint32_t            LastSampleMs;
            uint32_t            LastFlushMs;
            float               Sum;
            uint16_t            Count;
        };

        bool            IsStorageReady              ();
        bool            IsChannelIdValid            (int8_t ChannelID) const;
        bool            EnsureDayReady              ();
        bool            BuildCurrentDay             (String& OutDayTag, int32_t& OutDayKey, uint32_t& OutSecondsOfDay) const;
        void            FlushChannel                (Channel_t& Channel, uint32_t NowMs);
        bool            WriteValueAtCurrentSlot     (const Channel_t& Channel, float Value);
        bool            FillGapSlots                (const String& Path, uint32_t FromSlot, uint32_t ToSlot);
        int8_t          ResolveFileName             (const String& RawName, String& OutDayTag) const;
        void            AppendChannelFields         (String& Payload, const Channel_t& Channel) const;
        void            EnforceRetention            ();

        static uint32_t GetSlotCountPerDay          (const Channel_t& Channel);
        static bool     ExtractFileEntry            (const String& FilesJson, int& Cursor, String& OutName, uint32_t& OutSizeBytes);
        static String   BuildPath                   (const char* pFilePrefix, const String& DayTag);
        static String   DayKeyToTag                 (int32_t DayKey);
        static bool     IsSixDigits                 (const String& Value);

        M95PxxModule*   m_pStorage;
        Channel_t       m_Channel[SENSOR_LOGGER_MAX_CHANNEL];
        uint8_t         m_ChannelCount;
        int32_t         m_CurrentDayKey;
        String          m_CurrentDayTag;
        uint32_t        m_CurrentDaySeconds;
        uint8_t         m_RetentionPercent;
};

//-------------------------------------------------------------------------------------------------

#endif // SENSOR_LOGGER_MODULE_H
