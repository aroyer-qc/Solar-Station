#include "M95PxxModule.h"

// #define USE_DEBUG_M95P

// M95Pxx SPI EEPROM mapping from ESP32-WROOM module pin numbers:
// module pin 30 -> GPIO18 (SCK)
// module pin 37 -> GPIO23 (MOSI)
// module pin 31 -> GPIO19 (MISO)
// module pin 29 -> GPIO5  (chip select)
#define M95PXX_SPI_SCK_PIN               18
#define M95PXX_SPI_MOSI_PIN              23
#define M95PXX_SPI_MISO_PIN              19
#define M95PXX_SPI_CS_PIN                5
#define M95PXX_SPI_CLOCK_SPEED			 1000000

extern "C" {
#include "lfs.h"
}

M95PxxModule::M95PxxModule()
    : m_Spi(VSPI),
      m_IsReady(false),
      m_LittleFsMounted(false)
{
    memset(&m_Lfs, 0, sizeof(m_Lfs));
    memset(&m_LfsCfg, 0, sizeof(m_LfsCfg));
    memset(m_ReadCache, 0, sizeof(m_ReadCache));
    memset(m_ProgCache, 0, sizeof(m_ProgCache));
    memset(m_Lookahead, 0, sizeof(m_Lookahead));
}

void M95PxxModule::Begin()
{
    m_Spi.begin(M95PXX_SPI_SCK_PIN, M95PXX_SPI_MOSI_PIN, M95PXX_SPI_MISO_PIN, M95PXX_SPI_CS_PIN);
    pinMode(M95PXX_SPI_CS_PIN, OUTPUT);
    digitalWrite(M95PXX_SPI_CS_PIN, HIGH);

    uint8_t status = ReadStatusRegister();

    uint8_t manufacturer = 0;
    uint8_t type = 0;
    uint8_t capacity = 0;
    
  #ifdef USE_DEBUG_M95P
    if(ReadJedecId(manufacturer, type, capacity))
    {
        Serial.printf("[M95Pxx] Init done. SCK=%u MOSI=%u MISO=%u CS=%u SR=0x%02X JEDEC=%02X %02X %02X\n",
                       M95PXX_SPI_SCK_PIN, M95PXX_SPI_MOSI_PIN, M95PXX_SPI_MISO_PIN, M95PXX_SPI_CS_PIN, status, manufacturer, type, capacity);
    }
    else
    {
        Serial.printf("[M95Pxx] Init done. SCK=%u MOSI=%u MISO=%u CS=%u SR=0x%02X JEDEC=NA\n",
                       M95PXX_SPI_SCK_PIN, M95PXX_SPI_MOSI_PIN, M95PXX_SPI_MISO_PIN, M95PXX_SPI_CS_PIN, status);
    }
  #endif

    m_LfsCfg.context = this;
    m_LfsCfg.read = &M95PxxModule::LfsBlockRead;
    m_LfsCfg.prog = &M95PxxModule::LfsBlockProg;
    m_LfsCfg.erase = &M95PxxModule::LfsBlockErase;
    m_LfsCfg.sync = &M95PxxModule::LfsBlockSync;
    m_LfsCfg.read_size = LFS_READ_SIZE;
    m_LfsCfg.prog_size = LFS_PROG_SIZE;
    m_LfsCfg.block_size = LFS_BLOCK_SIZE;
    m_LfsCfg.block_count = LFS_BLOCK_COUNT;
    m_LfsCfg.block_cycles = LFS_BLOCK_CYCLES;
    m_LfsCfg.cache_size = LFS_CACHE_SIZE;
    m_LfsCfg.lookahead_size = LFS_LOOKAHEAD_SIZE;
    m_LfsCfg.read_buffer = m_ReadCache;
    m_LfsCfg.prog_buffer = m_ProgCache;
    m_LfsCfg.lookahead_buffer = m_Lookahead;

    m_IsReady = true;

	if(MountLittleFs(true) == false)
	{
        ReadJedecId(manufacturer, type, capacity);
        Serial.printf("[M95Pxx] LittleFS bootstrap failed. SR=0x%02X JEDEC=%02X %02X %02X (a dead SPI link reads 00 00 00 or FF FF FF)\n",
                      status, manufacturer, type, capacity);
		m_IsReady = false;
    }
}

bool M95PxxModule::IsReady()
{
    if(m_IsReady == false)
    {
        Serial.println("[M95Pxx] LittleFS not ready.");
    }

	return m_IsReady;
}

bool M95PxxModule::ReadData(uint32_t Address, uint8_t* pBuffer, size_t Length)
{
    if(!m_IsReady || pBuffer == nullptr || Length == 0)
    {
        return false;
    }

	StartTransaction(CMD_READ);
    m_Spi.transfer((uint8_t)(Address >> 16));
    m_Spi.transfer((uint8_t)(Address >> 8));
    m_Spi.transfer((uint8_t)(Address));

    for(size_t i = 0; i < Length; i++)
    {
        pBuffer[i] = m_Spi.transfer(0xFF);
    }

	EndTransaction();
    return true;
}

bool M95PxxModule::ErasePage(uint32_t Address)
{
    Address &= ~(uint32_t)(LFS_BLOCK_SIZE - 1);

    WriteEnable();
    StartTransaction(CMD_PAGE_ERASE);
    m_Spi.transfer((Address >> 16) & 0xFF);
    m_Spi.transfer((Address >> 8) & 0xFF);
    m_Spi.transfer(Address & 0xFF);
    EndTransaction();
    WaitWriteComplete(100); // timeout à ajuster si besoin

    return true;
}

bool M95PxxModule::WriteData(uint32_t Address, const uint8_t* pData, size_t Length)
{
    if(!m_IsReady || pData == nullptr || Length == 0 || (Address % 512 != 0) || (Length > 512))
    {
        return false;
    }

    WriteEnable();
	StartTransaction(CMD_WRITE);
    m_Spi.transfer((uint8_t)(Address >> 16));
    m_Spi.transfer((uint8_t)(Address >> 8));
    m_Spi.transfer((uint8_t)(Address));

    for(size_t i = 0; i < Length; i++)
    {
        m_Spi.transfer(pData[i]);
    }

	EndTransaction();
    WaitWriteComplete();
    return true;
}

uint8_t M95PxxModule::ReadStatusRegister()
{
    uint8_t status;

	StartTransaction(CMD_RDSR);
    status = m_Spi.transfer(0xFF);
	EndTransaction();

    return status;
}

void M95PxxModule::WriteEnable()
{
	StartTransaction(CMD_WREN);
	EndTransaction();
}

void M95PxxModule::WaitWriteComplete(uint32_t TimeoutMs)
{
    uint32_t start = millis();

    while((millis() - start) < TimeoutMs)
    {
        if((ReadStatusRegister() & 0x01u) == 0)
        {
            return;
        }
    }

  #ifdef USE_DEBUG_M95P
    Serial.println("[M95Pxx] Write timeout.");
  #endif	
}

bool M95PxxModule::ReadJedecId(uint8_t& Manufacturer, uint8_t& Type, uint8_t& Capacity)
{
    if(!m_IsReady)
    {
        return false;
    }

	StartTransaction(CMD_RDID);
    Manufacturer = m_Spi.transfer(0xFF);
    Type = m_Spi.transfer(0xFF);
    Capacity = m_Spi.transfer(0xFF);
	EndTransaction();
    return true;
}

bool M95PxxModule::LfsPathIsValid(const String& Path) const
{
    if(Path.length() == 0)
    {
        return false;
    }

    if(Path.indexOf("..") >= 0 || Path.indexOf('\\') >= 0)
    {
        return false;
    }

    return true;
}

int M95PxxModule::LfsBlockRead(const lfs_config* pCfg, lfs_block_t Block, lfs_off_t Off, void* pBuffer, lfs_size_t Size)
{
    if(pCfg == nullptr || pCfg->context == nullptr || pBuffer == nullptr)
    {
        return LFS_ERR_IO;
    }

    M95PxxModule* pThis = reinterpret_cast<M95PxxModule*>(pCfg->context);
    uint32_t address = ((uint32_t)Block * LFS_BLOCK_SIZE) + (uint32_t)Off;

    if(address + Size > STORAGE_SIZE_BYTES)
    {
        return LFS_ERR_IO;
    }

    return pThis->ReadData(address, reinterpret_cast<uint8_t*>(pBuffer), Size) ? 0 : LFS_ERR_IO;
}

int M95PxxModule::LfsBlockProg(const lfs_config* pCfg, lfs_block_t Block, lfs_off_t Off, const void* pBuffer, lfs_size_t Size)
{
    if(pCfg == nullptr || pCfg->context == nullptr || pBuffer == nullptr)
    {
        return LFS_ERR_IO;
    }

    M95PxxModule* pThis = reinterpret_cast<M95PxxModule*>(pCfg->context);
    uint32_t address = ((uint32_t)Block * LFS_BLOCK_SIZE) + (uint32_t)Off;

    if(address + Size > STORAGE_SIZE_BYTES)
    {
        return LFS_ERR_IO;
    }

    return pThis->WriteData(address, reinterpret_cast<const uint8_t*>(pBuffer), Size) ? 0 : LFS_ERR_IO;
}

int M95PxxModule::LfsBlockErase(const lfs_config* pCfg, lfs_block_t Block)
{
    if(pCfg == nullptr || pCfg->context == nullptr)
    {
        return LFS_ERR_IO;
    }

    M95PxxModule* pThis = reinterpret_cast<M95PxxModule*>(pCfg->context);

    const uint32_t blockSize   = pCfg->block_size;      // 512
    const uint32_t storageSize = STORAGE_SIZE_BYTES;    // 4 MiB

    uint32_t baseAddress = (uint32_t)Block * blockSize;
    if(baseAddress + blockSize > storageSize)
    {
        return LFS_ERR_IO;
    }

    if(!pThis->ErasePage(baseAddress))
    {
        return LFS_ERR_IO;
    }

    return 0;
}

int M95PxxModule::LfsBlockSync(const lfs_config* pCfg)
{
    if(pCfg == nullptr || pCfg->context == nullptr)
    {
        return LFS_ERR_IO;
    }

    return 0;
}

bool M95PxxModule::MountLittleFs(bool FormatOnFail)
{
    if(!m_IsReady)
    {
        return false;
    }

    if(m_LittleFsMounted)
    {
        return true;
    }

    int result = lfs_mount(&m_Lfs, &m_LfsCfg);
    if(result < 0 && FormatOnFail)
    {
        if(!FormatLittleFs())
        {
            return false;
        }

        result = lfs_mount(&m_Lfs, &m_LfsCfg);
    }

    if(result < 0)
    {
        Serial.printf("[M95Pxx] lfs_mount failed: %d\n", result);
        return false;
    }

    m_LittleFsMounted = true;
    return true;
}

bool M95PxxModule::FormatLittleFs()
{
    if(!m_IsReady)
    {
        return false;
    }

    int result = lfs_format(&m_Lfs, &m_LfsCfg);
    if(result < 0)
    {
        Serial.printf("[M95Pxx] lfs_format failed: %d\n", result);
        return false;
    }

  #ifdef USE_DEBUG_M95P		
    Serial.println("[M95Pxx] LittleFS formatted.");
  #endif	
    return true;
}

void M95PxxModule::UnmountLittleFs()
{
    if(!m_LittleFsMounted)
    {
        return;
    }

    lfs_unmount(&m_Lfs);
    m_LittleFsMounted = false;
}

bool M95PxxModule::LittleFsAppend(const String& Path, const uint8_t* pData, size_t Length)
{
    if(!MountLittleFs(true) || !LfsPathIsValid(Path) || pData == nullptr || Length == 0)
    {
        return false;
    }

    String normalized = Path;
    if(normalized.startsWith("/"))
    {
        normalized = normalized.substring(1);
    }

    lfs_file_t file;
    int openResult = lfs_file_open(&m_Lfs, &file, normalized.c_str(), LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND);
    if(openResult < 0)
    {
        return false;
    }

    lfs_ssize_t written = lfs_file_write(&m_Lfs, &file, pData, Length);
    int closeResult = lfs_file_close(&m_Lfs, &file);
    return (written == (lfs_ssize_t)Length) && (closeResult == 0);
}

bool M95PxxModule::LittleFsWriteAt(const String& Path, uint32_t Offset, const uint8_t* pData, size_t Length)
{
    if(!MountLittleFs(true) || !LfsPathIsValid(Path) || pData == nullptr || Length == 0)
    {
        return false;
    }

    String normalized = Path;
    if(normalized.startsWith("/"))
    {
        normalized = normalized.substring(1);
    }

    lfs_file_t file;
    if(lfs_file_open(&m_Lfs, &file, normalized.c_str(), LFS_O_WRONLY | LFS_O_CREAT) < 0)
    {
        return false;
    }

    if(lfs_file_seek(&m_Lfs, &file, (lfs_soff_t)Offset, LFS_SEEK_SET) < 0)
    {
        lfs_file_close(&m_Lfs, &file);
        return false;
    }

    lfs_ssize_t written = lfs_file_write(&m_Lfs, &file, pData, Length);
    int closeResult = lfs_file_close(&m_Lfs, &file);
    return (written == (lfs_ssize_t)Length) && (closeResult == 0);
}

bool M95PxxModule::LittleFsReadRange(const String& Path, uint32_t Offset, uint8_t* pBuffer, size_t Length, size_t& OutRead)
{
    OutRead = 0;
    if(!MountLittleFs(true) || !LfsPathIsValid(Path) || pBuffer == nullptr || Length == 0)
    {
        return false;
    }

    String normalized = Path;
    if(normalized.startsWith("/"))
    {
        normalized = normalized.substring(1);
    }

    lfs_file_t file;
    if(lfs_file_open(&m_Lfs, &file, normalized.c_str(), LFS_O_RDONLY) < 0)
    {
        return false;
    }

    if(lfs_file_seek(&m_Lfs, &file, (lfs_soff_t)Offset, LFS_SEEK_SET) < 0)
    {
        lfs_file_close(&m_Lfs, &file);
        return false;
    }

    lfs_ssize_t read = lfs_file_read(&m_Lfs, &file, pBuffer, Length);
    lfs_file_close(&m_Lfs, &file);

    if(read < 0)
    {
        return false;
    }

    OutRead = (size_t)read;
    return true;
}

bool M95PxxModule::LittleFsGetFileSize(const String& Path, size_t& OutSize) const
{
    OutSize = 0;
    if(!m_LittleFsMounted || !LfsPathIsValid(Path))
    {
        return false;
    }

    String normalized = Path;
    if(normalized.startsWith("/"))
    {
        normalized = normalized.substring(1);
    }

    struct lfs_info info;
    if(lfs_stat(const_cast<lfs_t*>(&m_Lfs), normalized.c_str(), &info) < 0 || info.type != LFS_TYPE_REG)
    {
        return false;
    }

    OutSize = (size_t)info.size;
    return true;
}

bool M95PxxModule::LittleFsExists(const String& Path) const
{
    size_t sizeBytes = 0;
    return LittleFsGetFileSize(Path, sizeBytes);
}

bool M95PxxModule::LittleFsRemove(const String& Path)
{
    if(!m_LittleFsMounted || !LfsPathIsValid(Path))
    {
        return false;
    }

    String normalized = Path;
    if(normalized.startsWith("/"))
    {
        normalized = normalized.substring(1);
    }

    return lfs_remove(&m_Lfs, normalized.c_str()) == 0;
}

String M95PxxModule::LittleFsListFilesJson() const
{
    String payload = "[]";
    if(!m_LittleFsMounted)
    {
        return payload;
    }

    lfs_dir_t dir;
    if(lfs_dir_open(const_cast<lfs_t*>(&m_Lfs), &dir, "/") < 0)
    {
        return payload;
    }

    payload = "[";
    bool first = true;
    struct lfs_info info;

    while(lfs_dir_read(const_cast<lfs_t*>(&m_Lfs), &dir, &info) > 0)
    {
        if(strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0)
        {
            continue;
        }

        if(info.type != LFS_TYPE_REG)
        {
            continue;
        }

        if(!first)
        {
            payload += ",";
        }
        first = false;

        payload += "{\"name\":\"";
        payload += info.name;
        payload += "\",\"size\":";
        payload += String((uint32_t)info.size);
        payload += "}";
    }

    lfs_dir_close(const_cast<lfs_t*>(&m_Lfs), &dir);
    payload += "]";
    return payload;
}

size_t M95PxxModule::LittleFsTotalBytes() const
{
    return STORAGE_SIZE_BYTES;
}

size_t M95PxxModule::LittleFsUsedBytes() const
{
    if(!m_LittleFsMounted)
    {
        return 0;
    }

    size_t used = 0;
    lfs_dir_t dir;
    if(lfs_dir_open(const_cast<lfs_t*>(&m_Lfs), &dir, "/") < 0)
    {
        return 0;
    }

    struct lfs_info info;
    while(lfs_dir_read(const_cast<lfs_t*>(&m_Lfs), &dir, &info) > 0)
    {
        if(info.type == LFS_TYPE_REG)
        {
            used += (size_t)info.size;
        }
    }

    lfs_dir_close(const_cast<lfs_t*>(&m_Lfs), &dir);
    return used;
}

void M95PxxModule::StartTransaction(uint8_t Command)
{
    SPISettings settings(M95PXX_SPI_CLOCK_SPEED, MSBFIRST, SPI_MODE0);
    m_Spi.beginTransaction(settings);
    digitalWrite(M95PXX_SPI_CS_PIN, LOW);
	m_Spi.transfer(Command);
}

void M95PxxModule::EndTransaction()
{
    digitalWrite(M95PXX_SPI_CS_PIN, HIGH);
    m_Spi.endTransaction();
}
