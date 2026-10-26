#ifndef _CONTROL_H
#define _CONTROL_H

#define MAX_POINT_COUNT 500

/* 映射升级时用到的1个寄存器
 * AO[MODBUS_UPDATE_REG_ADDR] : 写入命令, 读出状态
 *  写: 1-升级, 2-启动
 *  读: 0-传感器无法接收文件, 1-传感器可以接收文件, 2-文件接收完毕
 */

#define MODBUS_UPDATE_REG_ADDR 0
#define MODBUS_PRIVATE_CMD_ENTER_BOOT 0x55
#define MODBUS_PRIVATE_CMD_ENTER_APP  0xAA

#define MODBUS_PRIVATE_STATUS_BUSY 0x12
#define MODBUS_PRIVATE_STATUS_IDLE 0x34
#define MODBUS_PRIVATE_STATUS_DONE 0x56

#define FILE_NUMBER_POINT_MAP  0
#define FILE_NUMBER_FIRMWARE   1

#define MAX_CHANNEL_NUMBER 3

typedef struct PointMap {
    char reg_type[4];
    uint16_t reg_addr_master; /* 主控的寄存器地址 */    
    uint16_t channel;         /* 0-主控本身, 1-通过CH1访问, 2-通过CH2访问 */
    uint16_t dev_addr;        /* 传感器的设备地址 */
    uint16_t reg_addr_salve;  /* 传感器的寄存器地址 */
}PointMap, *PPointMap;

typedef struct ChannelInfo {
	int channel;
	char *uart_name;
    SemaphoreHandle_t xMutex;
	modbus_mapping_t *mb_mapping;
    modbus_t *ctx;
}ChannelInfo, *PChannelInfo;

/**********************************************************************
 * 函数名称： get_firmware_queue
 * 功能描述： 获得通道的固件队列
 * 输入参数： channel - 通道
 * 输出参数： 无
 * 返 回 值： 队列句柄
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/26        V1.0     韦东山       创建
 ***********************************************************************/
QueueHandle_t get_firmware_queue(int channel);

/**********************************************************************
 * 函数名称： SetUpdateStatus
 * 功能描述： 设置升级状态
 * 输入参数： 1-正在升级, 0-不在升级
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/26        V1.0     韦东山       创建
 ***********************************************************************/
void SetUpdateStatus(int on);

/**********************************************************************
 * 函数名称： isUpdating
 * 功能描述： 是否正在升级
 * 输入参数： 无
 * 输出参数： 无
 * 返 回 值： 0-不在升级,1-正在升级
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/26        V1.0     韦东山       创建
 ***********************************************************************/
int isUpdating(void);


/**********************************************************************
 * 函数名称： LibmodbusServerTask
 * 功能描述： 主控Modbus任务
 * 输入参数： pvParameters - 未使用
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
void LibmodbusServerTask( void *pvParameters )	;

#endif

