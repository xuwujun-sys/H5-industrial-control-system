// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (c) 2008-2023 100askTeam : Dongshan WEI <weidongshan@qq.com> 
 * Discourse:  https://forums.100ask.net
 */
 
/*  Copyright (C) 2008-2023 深圳百问网科技有限公司
 *  All rights reserved
 *
 * 免责声明: 百问网编写的文档, 仅供学员学习使用, 可以转发或引用(请保留作者信息),禁止用于商业用途！
 * 免责声明: 百问网编写的程序, 可以用于商业用途, 但百问网不承担任何后果！
 * 
 * 本程序遵循GPL V3协议, 请遵循协议
 * 百问网学习平台   : https://www.100ask.net
 * 百问网交流社区   : https://forums.100ask.net
 * 百问网官方B站    : https://space.bilibili.com/275908810
 * 本程序所用开发板 : STM32H5
 * 百问网官方淘宝   : https://100ask.taobao.com
 * 联系我们(E-mail): weidongshan@qq.com
 *
 *          版权所有，盗版必究。
 *  
 * 修改历史     版本号           作者        修改内容
 *-----------------------------------------------------
 * 2024.06.23      v01         百问科技      创建文件
 *-----------------------------------------------------
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os2.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"
#include "stdio.h"
#include "modbus.h"
#include "errno.h"

#include "control.h"
#include "bootloader.h"


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
int process_file_record(uint8_t *msg, uint16_t msg_len)
{
    uint16_t file_no;
    //uint16_t channel;
    //uint16_t dev_addr;
    //uint16_t record_no;
    
    if (msg[1] == MODBUS_FC_WRITE_FILE_RECORD)
    {
        file_no = ((uint16_t)msg[4]<<8) | msg[5];        
        if (file_no == FILE_NUMBER_FIRMWARE)
        {
            /* 烧写固件 */
            burn_firmware(msg, msg_len);

            return 0;
        }
    }

    return 0;
}

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
int process_emergency_cmd(modbus_t *ctx, uint8_t *msg, uint16_t msg_len, modbus_mapping_t *mb_mapping)
{    
    int reg_addr_master;
    int val;

    reg_addr_master = ((uint16_t)msg[2]<<8) | msg[3];
    val             = ((uint16_t)msg[4]<<8) | msg[5];
    
    /* 只处理写操作 */
    if (msg[1] != MODBUS_FC_WRITE_SINGLE_REGISTER)
    {
        return 0;
    }

    /* 如果不是CMD_STATUS寄存器直接返回 */
    if (reg_addr_master != MODBUS_UPDATE_REG_ADDR)
        return 0;
    
    if (val == MODBUS_PRIVATE_CMD_ENTER_BOOT)
    {
        if (!isBootloader())
        {
            /* 复位之后就无法回复了,所以我们先回复 */
            modbus_reply(ctx, msg, msg_len, mb_mapping);
            ResetToBootloader();
            return -1;
        }
        return 0;
    }
    else if (val == MODBUS_PRIVATE_CMD_ENTER_APP)
    {
        /* 复位之后就无法回复了,所以我们先回复 */
        modbus_reply(ctx, msg, msg_len, mb_mapping);
        ResetToApplication();
        return -1;
    }
    return 0;
}

