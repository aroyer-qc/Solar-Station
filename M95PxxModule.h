#ifndef MODULE_M95PXX_H
#define MODULE_M95PXX_H

#include <Arduino.h>
#include <SPI.h>

extern "C" {
#include "lfs.h"
}

class M95PxxModule
{
    public:
                        M95PxxModule();

        void            Begin();
        bool            IsReady();

        bool            ReadData(uint32_t Address, uint8_t* pBuffer, size_t Length);
        bool            WriteData(uint32_t Address, const uint8_t* pData, size_t Length);
		bool 			ErasePage(uint32_t Address);
        uint8_t         ReadStatusRegister();
        bool            MountLittleFs(bool FormatOnFail = true);
        bool            FormatLittleFs();
        void            UnmountLittleFs();
        bool            IsLittleFsMounted() const { return m_LittleFsMounted; }

        bool            LittleFsAppend(const String& Path, const uint8_t* pData, size_t Length);
        bool            LittleFsWriteAt(const String& Path, uint32_t Offset, const uint8_t* pData, size_t Length);
        bool            LittleFsReadRange(const String& Path, uint32_t Offset, uint8_t* pBuffer, size_t Length, size_t& OutRead);
        bool            LittleFsGetFileSize(const String& Path, size_t& OutSize) const;
        bool            LittleFsExists(const String& Path) const;
        bool            LittleFsRemove(const String& Path);
        String          LittleFsListFilesJson() const;
        size_t          LittleFsTotalBytes() const;
        size_t          LittleFsUsedBytes() const;

    private:

        void            WriteEnable();
        void            WaitWriteComplete(uint32_t TimeoutMs = 100);
		void			StartTransaction(uint8_t Command);
		void			EndTransaction();

        static constexpr uint8_t CMD_WREN = 0x06;
        static constexpr uint8_t CMD_RDSR = 0x05;
        static constexpr uint8_t CMD_READ = 0x03;
        static constexpr uint8_t CMD_WRITE = 0x02;
        static constexpr uint8_t CMD_RDID = 0x9F;
		static constexpr uint8_t CMD_PAGE_ERASE = 0xD8;

        static constexpr uint32_t LFS_READ_SIZE = 16u;
        static constexpr uint32_t LFS_PROG_SIZE = 512u;
        static constexpr uint32_t LFS_BLOCK_SIZE = 512u;
        static constexpr uint32_t STORAGE_SIZE_BYTES = 4u * 1024u * 1024u;
        static constexpr uint32_t LFS_BLOCK_COUNT = STORAGE_SIZE_BYTES / LFS_BLOCK_SIZE;
        static constexpr uint32_t LFS_CACHE_SIZE = 512u;
        static constexpr uint32_t LFS_LOOKAHEAD_SIZE = 32u;
        static constexpr uint32_t LFS_BLOCK_CYCLES = 100u;

        static int      LfsBlockRead(const lfs_config* pCfg, lfs_block_t Block, lfs_off_t Off, void* pBuffer, lfs_size_t Size);
        static int      LfsBlockProg(const lfs_config* pCfg, lfs_block_t Block, lfs_off_t Off, const void* pBuffer, lfs_size_t Size);
        static int      LfsBlockErase(const lfs_config* pCfg, lfs_block_t Block);
        static int      LfsBlockSync(const lfs_config* pCfg);
        bool            ReadJedecId(uint8_t& Manufacturer, uint8_t& Type, uint8_t& Capacity);
        bool            LfsPathIsValid(const String& Path) const;

        SPIClass        m_Spi;
        bool            m_IsReady;
        bool            m_LittleFsMounted;
        lfs_t           m_Lfs;
        lfs_config      m_LfsCfg;
        uint8_t         m_ReadCache[LFS_CACHE_SIZE];
        uint8_t         m_ProgCache[LFS_CACHE_SIZE];
        uint8_t         m_Lookahead[LFS_LOOKAHEAD_SIZE];
};

#endif // MODULE_M95PXX_H
