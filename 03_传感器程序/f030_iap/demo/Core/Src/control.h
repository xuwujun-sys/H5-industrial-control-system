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

/**********************************************************************
 * 函数名称： process_emergency_cmd
 * 功能描述： 处理紧急的命令(比如复位)
 * 输入参数： ctx - Modbus上下文
 *            msg - 里面存有上位机发来的消息, 消息为"write file record"请求包
 *            msg_len - 消息长度
 *            mb_mapping - 里面含有DI/DO/AI/AO寄存器
 * 输出参数： 无
 * 返 回 值： 0-成功, (-1)-失败
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
int process_emergency_cmd(modbus_t *ctx, uint8_t *msg, uint16_t msg_len, modbus_mapping_t *mb_mapping);

/**********************************************************************
 * 函数名称： process_file_record
 * 功能描述： 解析文件信息,烧写固件
 * 输入参数： msg - 里面存有上位机发来的消息
 *            msg_len - 消息长度
 * 输出参数： 无
 * 返 回 值： 0-成功, (-1)-失败
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
int process_file_record(uint8_t *msg, uint16_t msg_len);


#endif

