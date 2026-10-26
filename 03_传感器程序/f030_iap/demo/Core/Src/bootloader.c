/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os2.h"
#include "string.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"
#include "modbus.h"
#include "errno.h"
#include "bootloader.h"


#define UPDATE_TIMEOUT 1000

static int g_bFlashProgramming = 0;

uint32_t get_app_vector(void)
{
    //PFirmwareInfo ptFlashInfo = (PFirmwareInfo)CFG_OFFSET;
    //return ptFlashInfo->load_addr;
    return APP_LOAD_ADDR;
}

static void SoftReset(void)
{
    //__set_FAULTMASK(1);//关闭所有中断
    __disable_irq();
    HAL_NVIC_SystemReset();
}

/**********************************************************************
 * 函数名称： RelocateVector
 * 功能描述： 把中断向量表复制到SRAM,并让0地址映射到SRAM
 * 输入参数： 无
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/25        V1.0     韦东山       创建
 ***********************************************************************/
void RelocateVector(void)
{
    /* http://www.51hei.com/bbs/dpj-40235-1.html */
    memcpy((void *)VECTOR_ADDR_AT_RAM, (void *)APP_LOAD_ADDR, VECTOR_SIZE_AT_RAM);
    __HAL_SYSCFG_REMAPMEMORY_SRAM();
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
    uint32_t PageError;
    uint32_t pages = (len + (FLASH_PAGE_SIZE - 1)) / FLASH_PAGE_SIZE;
    
    HAL_FLASH_Unlock();
    g_bFlashProgramming = 1;

    /* erase sector */
    {
        tEraseInit.TypeErase   = FLASH_TYPEERASE_PAGES;
        tEraseInit.PageAddress = flash_addr;
        tEraseInit.NbPages     = pages;
        
        if (HAL_OK != HAL_FLASHEx_Erase(&tEraseInit, &PageError))
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
    uint32_t val;
    uint8_t *src_buf = (uint8_t *)firmware_buf;
    
    HAL_FLASH_Unlock();
    g_bFlashProgramming = 1;

    /* program */
    len = (len + 3) & ~3;

    for (int i = 0; i < len; i+=4)
    {
        val = ((uint32_t)src_buf[0] << 0) | ((uint32_t)src_buf[1] << 8) | ((uint32_t)src_buf[2] << 16) | ((uint32_t)src_buf[3] << 24);
        if (HAL_OK != HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flash_addr, val))
        {
            g_bFlashProgramming = 0;
            HAL_FLASH_Lock();
            return -1;
        }

        flash_addr += 4;
        src_buf += 4;
    }


    g_bFlashProgramming = 0;
    HAL_FLASH_Lock();
    return 0;

}

static int WriteFirmwareInfo(PFirmwareInfo ptFirmwareInfo)
{
    FLASH_EraseInitTypeDef tEraseInit;
    uint32_t PageError;
    uint32_t val;
    
    uint32_t flash_addr = CFG_OFFSET;
    uint8_t *src_buf = (uint8_t *)ptFirmwareInfo;
    
    HAL_FLASH_Unlock();

    /* erase bank2 */
    tEraseInit.TypeErase   = FLASH_TYPEERASE_PAGES;
    tEraseInit.PageAddress = flash_addr;
    tEraseInit.NbPages     = 1;
    
    if (HAL_OK != HAL_FLASHEx_Erase(&tEraseInit, &PageError))
    {
        HAL_FLASH_Lock();
        return -1;
    }

    /* program */
    for (int i = 0; i < sizeof(FirmwareInfo); i+=4)
    {
        val = ((uint32_t)src_buf[0] << 0) | ((uint32_t)src_buf[1] << 8) | ((uint32_t)src_buf[2] << 16) | ((uint32_t)src_buf[3] << 24);
        if (HAL_OK != HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flash_addr, val))
        {
            HAL_FLASH_Lock();
            return -1;
        }

        flash_addr += 4;
        src_buf += 4;
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
    static int cnt = 0;

    FirmwareInfo tFirmwareInfo;
    
    uint16_t record_no;

    cnt++;

    record_no = ((uint16_t)msg[6]<<8) | msg[7];

    if (record_no == 0)
    {
		/* 收到文件头,则算出文件大小 */
		/* 使用memcpy比较安全,因为"&msg[10]"不一定是对齐的地址 */
        memcpy(&tFileInfo, &msg[10], sizeof(tFileInfo));
        tFileInfo.file_len = BE32toLE32((uint8_t *)&tFileInfo.file_len);
        recv_len = 0;
        flash_addr = APP_LOAD_ADDR;

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

