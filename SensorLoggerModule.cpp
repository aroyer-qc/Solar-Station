//-------------------------------------------------------------------------------------------------
//
//  File : SensorLoggerModule.cpp
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

//-------------------------------------------------------------------------------------------------
// Include file(s)
//-------------------------------------------------------------------------------------------------

#include "SensorLoggerModule.h"
#include "SchedulerModule.h"
#include <math.h>

//-------------------------------------------------------------------------------------------------
// Define(s)
//-------------------------------------------------------------------------------------------------

#define SENSOR_LOGGER_DAY_TAG_LENGTH        6
#define SENSOR_LOGGER_FILE_EXTENSION        ".bin"
#define SENSOR_LOGGER_DEFAULT_RETENTION     80
#define SENSOR_LOGGER_SECONDS_PER_DAY       86400u
#define SENSOR_LOGGER_GAP_CHUNK_SLOT        256

//-------------------------------------------------------------------------------------------------
// Public method(s)
//-------------------------------------------------------------------------------------------------

SensorLoggerModule::SensorLoggerModule()
    : m_pStorage(nullptr)
    , m_Channel{}
    , m_ChannelCount(0)
    , m_CurrentDayKey(-1)
    , m_CurrentDaySeconds(0)
    , m_RetentionPercent(SENSOR_LOGGER_DEFAULT_RETENTION)
{
}

//-------------------------------------------------------------------------------------------------

void SensorLoggerModule::Begin(M95PxxModule* pStorage)
{
    uint32_t NowMs = millis();

    m_pStorage     = pStorage;
    m_CurrentDayKey = -1;
    m_CurrentDayTag = "";
    m_CurrentDaySeconds = 0;

    for(uint8_t i = 0; i < m_ChannelCount; i++)
    {
        m_Channel[i].LastSampleMs = NowMs;
        m_Channel[i].LastFlushMs  = NowMs;
        m_Channel[i].Sum          = 0.0f;
        m_Channel[i].Count        = 0;
    }
}

//-------------------------------------------------------------------------------------------------

int8_t SensorLoggerModule::RegisterChannel(const char* pFilePrefix,
                                           const char* pTypeName,
                                           const char* pLabel,
                                           const char* pUnit,
                                           uint32_t SampleIntervalMs,
                                           uint32_t AverageWindowMs,
                                           SensorLogProvider_t Provider)
{
    if((pFilePrefix == nullptr) || (pTypeName == nullptr) || (m_ChannelCount >= SENSOR_LOGGER_MAX_CHANNEL))
    {
        return SENSOR_LOGGER_INVALID_CHANNEL;
    }

    uint32_t   NowMs   = millis();
    Channel_t& Channel = m_Channel[m_ChannelCount];

    Channel.pFilePrefix      = pFilePrefix;
    Channel.pTypeName        = pTypeName;
    Channel.pLabel           = (pLabel != nullptr) ? pLabel : pTypeName;
    Channel.pUnit            = (pUnit != nullptr) ? pUnit : "";
    Channel.SampleIntervalMs = SampleIntervalMs;
    Channel.AverageWindowMs  = AverageWindowMs;
    Channel.Provider         = Provider;
    Channel.LastSampleMs     = NowMs;
    Channel.LastFlushMs      = NowMs;
    Channel.Sum              = 0.0f;
    Channel.Count            = 0;

    return (int8_t)(m_ChannelCount++);
}

//-------------------------------------------------------------------------------------------------

void SensorLoggerModule::Update(uint32_t NowMs)
{
    for(uint8_t i = 0; i < m_ChannelCount; i++)
    {
        Channel_t& Channel = m_Channel[i];

        if((Channel.Provider != nullptr) && ((NowMs - Channel.LastSampleMs) >= Channel.SampleIntervalMs))
        {
            Channel.LastSampleMs = NowMs;

            // A provider returns a non-finite value when it has no reading; the slot stays a gap.
            float Value = Channel.Provider();

            if(isfinite(Value))
            {
                Channel.Sum += Value;
                Channel.Count++;
            }
        }

        FlushChannel(Channel, NowMs);
    }
}

//-------------------------------------------------------------------------------------------------

bool SensorLoggerModule::PushSample(int8_t ChannelID, float Value)
{
    if(!IsChannelIdValid(ChannelID) || !isfinite(Value))
    {
        return false;
    }

    Channel_t& Channel = m_Channel[ChannelID];
    uint32_t   NowMs   = millis();

    if((NowMs - Channel.LastSampleMs) < Channel.SampleIntervalMs)
    {
        return false;
    }

    Channel.LastSampleMs = NowMs;
    Channel.Sum         += Value;
    Channel.Count++;
    return true;
}

//-------------------------------------------------------------------------------------------------

String SensorLoggerModule::GetManifestJson()
{
    String Payload;
    Payload.reserve(4096);

    bool StorageReady = IsStorageReady();

    Payload += "{\"usedBytes\":";
    Payload += String(StorageReady ? (uint32_t)m_pStorage->LittleFsUsedBytes() : 0u);
    Payload += ",\"totalBytes\":";
    Payload += String(StorageReady ? (uint32_t)m_pStorage->LittleFsTotalBytes() : 0u);
    Payload += ",\"storageReady\":";
    Payload += (StorageReady ? "true" : "false");
    Payload += ",\"clockValid\":";
    Payload += (IsSystemTimeValid() ? "true" : "false");
    Payload += ",\"currentDay\":\"";
    Payload += m_CurrentDayTag;
    Payload += "\",\"storageDiag\":\"";
    Payload += (m_pStorage != nullptr) ? m_pStorage->GetDiagnosticSummary() : String("no storage bound");
    Payload += "\",\"channels\":[";

    for(uint8_t i = 0; i < m_ChannelCount; i++)
    {
        if(i != 0)
        {
            Payload += ",";
        }

        Payload += "{";
        AppendChannelFields(Payload, m_Channel[i]);
        Payload += "}";
    }

    Payload += "],\"files\":[";

    if(StorageReady)
    {
        String   FilesJson = m_pStorage->LittleFsListFilesJson();
        int      Cursor    = 0;
        bool     First     = true;
        String   Name;
        uint32_t SizeBytes = 0;

        while(ExtractFileEntry(FilesJson, Cursor, Name, SizeBytes))
        {
            String DayTag;
            int8_t ChannelID = ResolveFileName(Name, DayTag);

            if(ChannelID < 0)
            {
                continue;
            }

            if(!First)
            {
                Payload += ",";
            }
            First = false;

            Payload += "{\"name\":\"";
            Payload += Name;
            Payload += "\",";
            AppendChannelFields(Payload, m_Channel[ChannelID]);
            Payload += ",\"date\":\"";
            Payload += DayTag;
            Payload += "\",\"bytes\":";
            Payload += String(SizeBytes);
            Payload += "}";
        }
    }

    Payload += "]}";
    return Payload;
}

//-------------------------------------------------------------------------------------------------

bool SensorLoggerModule::GetFileInfo(const String& RequestedName, uint32_t& OutSizeBytes)
{
    OutSizeBytes = 0;

    if((RequestedName.length() == 0) ||
       (RequestedName.indexOf('/') >= 0) ||
       (RequestedName.indexOf('\\') >= 0) ||
       (RequestedName.indexOf("..") >= 0))
    {
        return false;
    }

    String DayTag;

    if((ResolveFileName(RequestedName, DayTag) < 0) || !IsStorageReady())
    {
        return false;
    }

    size_t SizeBytes = 0;

    if(!m_pStorage->LittleFsGetFileSize(String("/") + RequestedName, SizeBytes))
    {
        return false;
    }

    OutSizeBytes = (uint32_t)SizeBytes;
    return true;
}

//-------------------------------------------------------------------------------------------------

bool SensorLoggerModule::ReadFileRange(const String& RequestedName, uint32_t OffsetBytes, uint8_t* pBuffer, size_t LengthBytes)
{
    uint32_t TotalSizeBytes = 0;

    if((pBuffer == nullptr) || (LengthBytes == 0))
    {
        return false;
    }

    if(!GetFileInfo(RequestedName, TotalSizeBytes))
    {
        return false;
    }

    if(((uint64_t)OffsetBytes + (uint64_t)LengthBytes) > (uint64_t)TotalSizeBytes)
    {
        return false;
    }

    size_t OutRead = 0;

    if(!m_pStorage->LittleFsReadRange(String("/") + RequestedName, OffsetBytes, pBuffer, LengthBytes, OutRead))
    {
        return false;
    }

    return OutRead == LengthBytes;
}

//-------------------------------------------------------------------------------------------------
// Private method(s)
//-------------------------------------------------------------------------------------------------

bool SensorLoggerModule::IsStorageReady()
{
    if(m_pStorage == nullptr)
    {
        return false;
    }

    return m_pStorage->IsLittleFsMounted() || m_pStorage->MountLittleFs(true);
}

//-------------------------------------------------------------------------------------------------

bool SensorLoggerModule::IsChannelIdValid(int8_t ChannelID) const
{
    return (ChannelID >= 0) && (ChannelID < (int8_t)m_ChannelCount);
}

//-------------------------------------------------------------------------------------------------

bool SensorLoggerModule::BuildCurrentDay(String& OutDayTag, int32_t& OutDayKey, uint32_t& OutSecondsOfDay) const
{
    if(!IsSystemTimeValid())
    {
        return false;
    }

    time_t    LocalNow = GetLocalTimeFromConfigOffset();
    struct tm LocalTimeInfo;

    if(gmtime_r(&LocalNow, &LocalTimeInfo) == nullptr)
    {
        return false;
    }

    int  Year  = (LocalTimeInfo.tm_year + 1900) % 100;
    int  Month = LocalTimeInfo.tm_mon + 1;
    int  Day   = LocalTimeInfo.tm_mday;
    char Buffer[SENSOR_LOGGER_DAY_TAG_LENGTH + 1];

    snprintf(Buffer, sizeof(Buffer), "%02d%02d%02d", Year, Month, Day);
    OutDayTag       = String(Buffer);
    OutDayKey       = (Year * 10000) + (Month * 100) + Day;
    OutSecondsOfDay = (uint32_t)((LocalTimeInfo.tm_hour * 3600) + (LocalTimeInfo.tm_min * 60) + LocalTimeInfo.tm_sec);
    return true;
}

//-------------------------------------------------------------------------------------------------

bool SensorLoggerModule::EnsureDayReady()
{
    String   DayTag;
    int32_t  DayKey        = -1;
    uint32_t SecondsOfDay  = 0;

    if(!IsStorageReady() || !BuildCurrentDay(DayTag, DayKey, SecondsOfDay))
    {
        return false;
    }

    m_CurrentDaySeconds = SecondsOfDay;

    if(m_CurrentDayKey != DayKey)
    {
        EnforceRetention();
        m_CurrentDayKey = DayKey;
        m_CurrentDayTag = DayTag;
      #ifdef USE_DEBUG_APP
        Serial.printf("[LOG] New log day: %s\n", m_CurrentDayTag.c_str());
      #endif
    }

    return true;
}

//-------------------------------------------------------------------------------------------------

void SensorLoggerModule::FlushChannel(Channel_t& Channel, uint32_t NowMs)
{
    if((NowMs - Channel.LastFlushMs) < Channel.AverageWindowMs)
    {
        return;
    }

    Channel.LastFlushMs = NowMs;

    if(Channel.Count == 0)
    {
        return;
    }

    if(EnsureDayReady())
    {
        float Average = Channel.Sum / (float)Channel.Count;

        if(!WriteValueAtCurrentSlot(Channel, Average))
        {
          #ifdef USE_DEBUG_APP
            Serial.printf("[LOG] Write failed: %s_%s.bin\n", Channel.pFilePrefix, m_CurrentDayTag.c_str());
          #endif
        }
    }

    Channel.Sum   = 0.0f;
    Channel.Count = 0;
}

//-------------------------------------------------------------------------------------------------

bool SensorLoggerModule::WriteValueAtCurrentSlot(const Channel_t& Channel, float Value)
{
    if((m_pStorage == nullptr) || (m_CurrentDayTag.length() != SENSOR_LOGGER_DAY_TAG_LENGTH))
    {
        return false;
    }

    uint32_t SlotCount = GetSlotCountPerDay(Channel);
    uint32_t Slot      = (m_CurrentDaySeconds * 1000u) / Channel.AverageWindowMs;

    if(Slot >= SlotCount)
    {
        Slot = SlotCount - 1u;
    }

    String Path         = BuildPath(Channel.pFilePrefix, m_CurrentDayTag);
    size_t CurrentBytes = 0;

    m_pStorage->LittleFsGetFileSize(Path, CurrentBytes);
    uint32_t ExistingSlots = (uint32_t)(CurrentBytes / sizeof(float));

    if((Slot > ExistingSlots) && !FillGapSlots(Path, ExistingSlots, Slot))
    {
        return false;
    }

    return m_pStorage->LittleFsWriteAt(Path, Slot * sizeof(float), (const uint8_t*)&Value, sizeof(Value));
}

//-------------------------------------------------------------------------------------------------

// Unrecorded slots must hold NaN: LittleFS pads a seek beyond EOF with zeros, a legitimate reading.
bool SensorLoggerModule::FillGapSlots(const String& Path, uint32_t FromSlot, uint32_t ToSlot)
{
    float GapChunk[SENSOR_LOGGER_GAP_CHUNK_SLOT];

    for(size_t i = 0; i < SENSOR_LOGGER_GAP_CHUNK_SLOT; i++)
    {
        GapChunk[i] = NAN;
    }

    uint32_t Slot = FromSlot;

    while(Slot < ToSlot)
    {
        uint32_t Remaining = ToSlot - Slot;
        uint32_t Count     = (Remaining < SENSOR_LOGGER_GAP_CHUNK_SLOT) ? Remaining : SENSOR_LOGGER_GAP_CHUNK_SLOT;

        if(!m_pStorage->LittleFsWriteAt(Path, Slot * sizeof(float), (const uint8_t*)GapChunk, Count * sizeof(float)))
        {
            return false;
        }

        Slot += Count;
    }

    return true;
}

//-------------------------------------------------------------------------------------------------

int8_t SensorLoggerModule::ResolveFileName(const String& RawName, String& OutDayTag) const
{
    String Name = RawName;

    if(Name.startsWith("/"))
    {
        Name = Name.substring(1);
    }

    if(!Name.endsWith(SENSOR_LOGGER_FILE_EXTENSION))
    {
        return SENSOR_LOGGER_INVALID_CHANNEL;
    }

    for(uint8_t i = 0; i < m_ChannelCount; i++)
    {
        String Prefix = String(m_Channel[i].pFilePrefix) + "_";

        if(!Name.startsWith(Prefix))
        {
            continue;
        }

        String Day = Name.substring(Prefix.length(), Name.length() - (sizeof(SENSOR_LOGGER_FILE_EXTENSION) - 1));

        if(!IsSixDigits(Day))
        {
            continue;
        }

        OutDayTag = Day;
        return (int8_t)i;
    }

    return SENSOR_LOGGER_INVALID_CHANNEL;
}

//-------------------------------------------------------------------------------------------------

void SensorLoggerModule::AppendChannelFields(String& Payload, const Channel_t& Channel) const
{
    Payload += "\"type\":\"";
    Payload += Channel.pTypeName;
    Payload += "\",\"label\":\"";
    Payload += Channel.pLabel;
    Payload += "\",\"unit\":\"";
    Payload += Channel.pUnit;
    Payload += "\",\"periodMs\":";
    Payload += String(Channel.AverageWindowMs);
}

//-------------------------------------------------------------------------------------------------

void SensorLoggerModule::EnforceRetention()
{
    if(!IsStorageReady())
    {
        return;
    }

    const size_t ThresholdBytes = (m_pStorage->LittleFsTotalBytes() * m_RetentionPercent) / 100u;

    while(m_pStorage->LittleFsUsedBytes() > ThresholdBytes)
    {
        String   FilesJson    = m_pStorage->LittleFsListFilesJson();
        int      Cursor       = 0;
        int32_t  OldestDayKey = -1;
        String   Name;
        uint32_t SizeBytes    = 0;

        while(ExtractFileEntry(FilesJson, Cursor, Name, SizeBytes))
        {
            String DayTag;

            if(ResolveFileName(Name, DayTag) >= 0)
            {
                int32_t DayKey = (int32_t)DayTag.toInt();

                if((OldestDayKey < 0) || (DayKey < OldestDayKey))
                {
                    OldestDayKey = DayKey;
                }
            }
        }

        if(OldestDayKey < 0)
        {
            break;
        }

        String OldestTag = DayKeyToTag(OldestDayKey);

        for(uint8_t i = 0; i < m_ChannelCount; i++)
        {
            m_pStorage->LittleFsRemove(BuildPath(m_Channel[i].pFilePrefix, OldestTag));
        }
    }
}

//-------------------------------------------------------------------------------------------------
// Private static method(s)
//-------------------------------------------------------------------------------------------------

uint32_t SensorLoggerModule::GetSlotCountPerDay(const Channel_t& Channel)
{
    return (SENSOR_LOGGER_SECONDS_PER_DAY * 1000u) / Channel.AverageWindowMs;
}

//-------------------------------------------------------------------------------------------------

// Walks the {"name":"..","size":N} entries produced by LittleFsListFilesJson(), one call per entry.
bool SensorLoggerModule::ExtractFileEntry(const String& FilesJson, int& Cursor, String& OutName, uint32_t& OutSizeBytes)
{
    const int NameTagLength = 8;        // strlen("\"name\":\"")
    const int SizeTagLength = 7;        // strlen("\"size\":")

    int NamePos = FilesJson.indexOf("\"name\":\"", Cursor);

    if(NamePos < 0)
    {
        return false;
    }

    int NameStart = NamePos + NameTagLength;
    int NameEnd   = FilesJson.indexOf('"', NameStart);

    if(NameEnd < 0)
    {
        return false;
    }

    int SizePos = FilesJson.indexOf("\"size\":", NameEnd);

    if(SizePos < 0)
    {
        return false;
    }

    int SizeStart = SizePos + SizeTagLength;
    int SizeEnd   = FilesJson.indexOf('}', SizeStart);

    if(SizeEnd < 0)
    {
        return false;
    }

    OutName      = FilesJson.substring(NameStart, NameEnd);
    OutSizeBytes = (uint32_t)FilesJson.substring(SizeStart, SizeEnd).toInt();
    Cursor       = SizeEnd + 1;
    return true;
}

//-------------------------------------------------------------------------------------------------

String SensorLoggerModule::BuildPath(const char* pFilePrefix, const String& DayTag)
{
    return String("/") + pFilePrefix + "_" + DayTag + SENSOR_LOGGER_FILE_EXTENSION;
}

//-------------------------------------------------------------------------------------------------

String SensorLoggerModule::DayKeyToTag(int32_t DayKey)
{
    char Buffer[SENSOR_LOGGER_DAY_TAG_LENGTH + 1];

    snprintf(Buffer, sizeof(Buffer), "%06ld", (long)DayKey);
    return String(Buffer);
}

//-------------------------------------------------------------------------------------------------

bool SensorLoggerModule::IsSixDigits(const String& Value)
{
    if(Value.length() != SENSOR_LOGGER_DAY_TAG_LENGTH)
    {
        return false;
    }

    for(size_t i = 0; i < Value.length(); i++)
    {
        if((Value[i] < '0') || (Value[i] > '9'))
        {
            return false;
        }
    }

    return true;
}

//-------------------------------------------------------------------------------------------------
