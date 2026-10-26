/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os2.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"
#include "draw.h"
#include "stdio.h"
#include "draw.h"
#include "ux_api.h"
#include "modbus.h"
#include "errno.h"
#include "uart_device.h"
#include "semphr.h"
#include "bootloader.h"


#define UPDATE_TIMEOUT 1000

static int g_bFlashProgramming = 0;

int isSoftReset(void)
{
    return HAL_RCC_GetResetSource() & RCC_RESET_FLAG_SW;
}

uint32_t get_app_vector(void)
{
    PFirmwareInfo ptFlashInfo = (PFirmwareInfo)CFG_OFFSET;
    return ptFlashInfo->load_addr;
}

static void SoftReset(void)
{
    __set_FAULTMASK(1);//关闭所有中断
    HAL_NVIC_SystemReset();
}

static void start_app_c(void)
{
    /* 触发软件复位 */
    SoftReset();
}

/**********************************************************************
 * 函数名称： isFlashProgramming
 * 功能描述： 当前是否正在烧写Flash
 * 输入参数： 无
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/25        V1.0     韦东山       创建
 ***********************************************************************/
int isFlashProgramming(void)
{
    return g_bFlashProgramming;
}

static uint32_t BE32toLE32(uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | ((uint32_t)buf[3] << 0);
}

static int GetLocalFirmwareInfo(PFirmwareInfo ptFirmwareInfo)
{
    volatile PFirmwareInfo ptFlashInfo = (PFirmwareInfo)CFG_OFFSET;
    
    if (ptFlashInfo->file_len == 0xFFFFFFFF)
        return -1;
    
    *ptFirmwareInfo = *ptFlashInfo;
    return 0;
}

/* https://lxp32.github.io/docs/a-simple-example-crc32-calculation/ */
static int GetCRC32(const char *s,size_t n)
{
    uint32_t crc=0xFFFFFFFF;

    for(size_t i=0;i<n;i++) {
            char ch=s[i];
            for(size_t j=0;j<8;j++) {
                    uint32_t b=(ch^crc)&1;
                    crc>>=1;
                    if(b) crc=crc^0xEDB88320;
                    ch>>=1;
            }
    }

    return ~crc;
}


/**********************************************************************
 * 函数名称： EraseFlash
 * 功能描述： 擦除Flash
 * 输入参数： flash_addr - Flash起始地址
 *            len        - 要擦除的长度
 * 输出参数： 无
 * 返 回 值： 0-成功, (-1)-失败
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/25        V1.0     韦东山       创建
 ***********************************************************************/
int EraseFlash(uint32_t flash_addr, uint32_t len)
{
    FLASH_EraseInitTypeDef tEraseInit;
    uint32_t SectorError;
    uint32_t sectors = (len + (SECTOR_SIZE - 1)) / SECTOR_SIZE;
    uint32_t flash_offset = flash_addr - 0x08000000;
    uint32_t bank_sectors;
    uint32_t erased_sectors = 0;
    
    HAL_FLASH_Unlock();
    g_bFlashProgramming = 1;

    /* erase bank1 */
    if (flash_offset < 0x100000)
    {
        tEraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
        tEraseInit.Banks     = FLASH_BANK_1;
        tEraseInit.Sector    = flash_offset / SECTOR_SIZE;
        bank_sectors = (0x100000 - flash_offset) / SECTOR_SIZE;
        if (sectors <= bank_sectors)
            erased_sectors = sectors;
        else
            erased_sectors = bank_sectors;
        tEraseInit.NbSectors = erased_sectors;
        
        if (HAL_OK != HAL_FLASHEx_Erase(&tEraseInit, &SectorError))
        {
            g_bFlashProgramming = 0;
            HAL_FLASH_Lock();
            return -1;
        }

        flash_offset += erased_sectors*SECTOR_SIZE;
    }

    sectors -= erased_sectors;
    flash_offset -= 0x100000;
    
    /* erase bank2 */
    if (sectors)
    {
        tEraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
        tEraseInit.Banks     = FLASH_BANK_2;
        tEraseInit.Sector    = flash_offset / SECTOR_SIZE;
        bank_sectors = (0x100000 - flash_offset) / SECTOR_SIZE;
        if (sectors <= bank_sectors)
            erased_sectors = sectors;
        else
            erased_sectors = bank_sectors;
        tEraseInit.NbSectors = erased_sectors;
        
        if (HAL_OK != HAL_FLASHEx_Erase(&tEraseInit, &SectorError))
        {
            g_bFlashProgramming = 0;
            HAL_FLASH_Lock();
            return -1;
        }
    }

    g_bFlashProgramming = 0;
    HAL_FLASH_Lock();
    return 0;

}

static int WriteFirmware(uint8_t *firmware_buf, uint32_t len, uint32_t flash_addr)
{
    HAL_FLASH_Unlock();
    g_bFlashProgramming = 1;

    /* program */
    len = (len + 15) & ~15;

    for (int i = 0; i < len; i+=16)
    {
        if (HAL_OK != HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, flash_addr, (uint32_t)firmware_buf))
        {
            g_bFlashProgramming = 0;
            HAL_FLASH_Lock();
            return -1;
        }

        flash_addr += 16;
        firmware_buf += 16;
    }


    g_bFlashProgramming = 0;
    HAL_FLASH_Lock();
    return 0;

}

static int WriteFirmwareInfo(PFirmwareInfo ptFirmwareInfo)
{
    FLASH_EraseInitTypeDef tEraseInit;
    uint32_t SectorError;
    uint32_t flash_addr = CFG_OFFSET;
    uint8_t *src_buf = (uint8_t *)ptFirmwareInfo;
    
    HAL_FLASH_Unlock();

    /* erase bank2 */
    tEraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
    tEraseInit.Banks     = FLASH_BANK_2;
    tEraseInit.Sector    = (flash_addr - 0x08000000 - 0x100000) / SECTOR_SIZE;
    tEraseInit.NbSectors = 1;
    
    if (HAL_OK != HAL_FLASHEx_Erase(&tEraseInit, &SectorError))
    {
        HAL_FLASH_Lock();
        return -1;
    }

    /* program */
    for (int i = 0; i < sizeof(FirmwareInfo); i+=16)
    {
        if (HAL_OK != HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, flash_addr, (uint32_t)src_buf))
        {
            HAL_FLASH_Lock();
            return -1;
        }

        flash_addr += 16;
        src_buf += 16;
    }

    HAL_FLASH_Lock();
    return 0;
}

/**********************************************************************
 * 函数名称： ResetToBootloader
 * 功能描述： 设置固件信息为"需要进入BootLoader",并软复位以便进入bootloader
 * 输入参数： 无
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/25        V1.0     韦东山       创建
 ***********************************************************************/
void ResetToBootloader(void)
{
    FirmwareInfo tFirmwareInfo;

    memset(&tFirmwareInfo, 0xff, sizeof(FirmwareInfo));
    GetLocalFirmwareInfo(&tFirmwareInfo);

    tFirmwareInfo.bEnterBootloader = 1;
    WriteFirmwareInfo(&tFirmwareInfo);

    SoftReset();
}

/**********************************************************************
 * 函数名称： ResetToApplication
 * 功能描述： 设置固件信息为"无需进入BootLoader",并软复位以便进入app
 * 输入参数： 无
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/25        V1.0     韦东山       创建
 ***********************************************************************/
void ResetToApplication(void)
{
    FirmwareInfo tFirmwareInfo;

    memset(&tFirmwareInfo, 0xff, sizeof(FirmwareInfo));
    GetLocalFirmwareInfo(&tFirmwareInfo);

    tFirmwareInfo.bEnterBootloader = 0;
    WriteFirmwareInfo(&tFirmwareInfo);

    SoftReset();
}


/**********************************************************************
 * 函数名称： isNeedToUpdate
 * 功能描述： 判断是否需要升级
 * 输入参数： 无
 * 输出参数： 无
 * 返 回 值： 0-无需升级,1-需要升级
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/25        V1.0     韦东山       创建
 ***********************************************************************/
int isNeedToUpdate(void)
{
    FirmwareInfo tFirmwareInfo;

    /* 没有固件信息则需要升级 */
    if (GetLocalFirmwareInfo(&tFirmwareInfo))
        return 1;

    /* 固件信息里明确需要升级 */
    if (tFirmwareInfo.bEnterBootloader == 1)
        return 1;

    /* 固件信息里明确表示无需升级 */
    if (tFirmwareInfo.bEnterBootloader == 0)
        return 0;

    return 1;
}


/**********************************************************************
 * 函数名称： isBootloader
 * 功能描述： 判断当期程序是bootloader还是app
 * 输入参数： 无
 * 输出参数： 无
 * 返 回 值： 0-是APP,1-是bootloader
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/25        V1.0     韦东山       创建
 ***********************************************************************/
int isBootloader(void)
{
    uint32_t link_addr = (uint32_t)isBootloader;
    if (link_addr < APP_LOAD_ADDR)
        return 1;
    else
        return 0;
}

/**********************************************************************
 * 函数名称： burn_firmware
 * 功能描述： 烧写固件
 * 输入参数： msg - 里面存有上位机发来的消息, 消息为"write file record"请求包
 *            msg_len - 消息长度
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
void burn_firmware(uint8_t *msg, uint16_t msg_len)
{
    static FileInfo tFileInfo;
    static int recv_len = 0;
    static uint32_t flash_addr = APP_LOAD_ADDR;
    int cur_len;

    FirmwareInfo tFirmwareInfo;
    
    uint16_t file_no;
    uint16_t channel;
    uint16_t dev_addr;
    uint16_t record_no;
    char buf[100];

    file_no = ((uint16_t)msg[4]<<8) | msg[5];
    record_no = ((uint16_t)msg[6]<<8) | msg[7];

    channel  = (file_no >> 12) & 0xf;
    dev_addr = (file_no) & 0xff;
    file_no  = (file_no >> 8) & 0xf;

    if (record_no == 0)
    {
		/* 收到文件头,则算出文件大小 */
		/* 使用memcpy比较安全,因为"&msg[10]"不一定是对齐的地址 */
        memcpy(&tFileInfo, &msg[10], sizeof(tFileInfo));
        tFileInfo.file_len = BE32toLE32((uint8_t *)&tFileInfo.file_len);
        recv_len = 0;
        flash_addr = APP_LOAD_ADDR;

        sprintf(buf, "Get firmware %s, len %d, Erasing ...", tFileInfo.file_name, tFileInfo.file_len);
        Draw_String(0, 32, buf, 0xff0000, 0);

        EraseFlash(APP_LOAD_ADDR, tFileInfo.file_len); /* 擦除APP */
        EraseFlash(CFG_OFFSET, SECTOR_SIZE); /* 擦除配置信息 */
    }
    else
    {
        cur_len = msg[2] - 7;
        recv_len += cur_len;
        
        WriteFirmware(&msg[10], cur_len, flash_addr);
        flash_addr += cur_len;
        
        if (recv_len >= tFileInfo.file_len)
        {
            sprintf(buf, "Get firmware %s, recv len %d done", tFileInfo.file_name, recv_len);
            Draw_String(0, 48, buf, 0xff0000, 0);

            tFirmwareInfo.bEnterBootloader = 0;
            tFirmwareInfo.version  = 0;
            tFirmwareInfo.file_len = tFileInfo.file_len;
            tFirmwareInfo.load_addr = APP_LOAD_ADDR;
            tFirmwareInfo.crc32     = 0; /* 未使用 */
            strcpy((char *)tFirmwareInfo.file_name, (char *)tFileInfo.file_name);

            WriteFirmwareInfo(&tFirmwareInfo);
        }

    }
}

