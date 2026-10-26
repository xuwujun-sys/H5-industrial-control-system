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
#include "draw.h"
#include "stdio.h"
#include "draw.h"
#include "ux_api.h"
#include "modbus.h"
#include "errno.h"
#include "uart_device.h"
#include "semphr.h"

#include "control.h"
#include "bootloader.h"

static PointMap g_tPointMaps[MAX_POINT_COUNT]; /* 映射表 */
static uint16_t g_iPointVals[MAX_POINT_COUNT]; /* 点的上次数值 */
static uint8_t *g_pucPointMapCurPos;           /* 映射表的当前位置:我们使用memcpy来更新映射表 */
static int g_iPointMapCnt;                     /* 映射表里点的个数 */
static int g_bUpdating = 0;

static ChannelInfo g_tChannelInfos[MAX_CHANNEL_NUMBER] = 
{
    {0, "usb", },  /* 未使用 */
	{1, "uart2", },
	{2, "uart4", },
};

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
int isUpdating(void)
{
    return g_bUpdating;
}

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
void SetUpdateStatus(int on)
{
    g_bUpdating = on;
}


/**********************************************************************
 * 函数名称： get_channelinfo
 * 功能描述： 获得通道的信息
 * 输入参数： channel - 通道
 * 输出参数： 无
 * 返 回 值： ChannelInfo指针
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/26        V1.0     韦东山       创建
 ***********************************************************************/
static PChannelInfo get_channelinfo(int channel)
{
    return &g_tChannelInfos[channel];
}

/**********************************************************************
 * 函数名称： get_channelinfo_for_ctx
 * 功能描述： 获得通道的信息
 * 输入参数： ctx - Modbus上下文
 * 输出参数： 无
 * 返 回 值： ChannelInfo指针
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/26        V1.0     韦东山       创建
 ***********************************************************************/
static PChannelInfo get_channelinfo_for_ctx(modbus_t *ctx)
{
    for (int i = 0; i < sizeof(g_tChannelInfos)/sizeof(g_tChannelInfos[0]); i++)
    {
        if (g_tChannelInfos[i].ctx == ctx)
            return &g_tChannelInfos[i];
    }

    return NULL;
}


/**********************************************************************
 * 函数名称： clear_point_map
 * 功能描述： 清除"点"的映射表
 * 输入参数： 无
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static void clear_point_map(void)
{
	g_pucPointMapCurPos = (uint8_t *)g_tPointMaps;
    g_iPointMapCnt = 0;
	memset(g_iPointVals, 0xff, sizeof(g_iPointVals));
}

/**********************************************************************
 * 函数名称： get_point_map_count
 * 功能描述： 获得映射表里点的个数
 * 输入参数： 无
 * 输出参数： 无
 * 返 回 值： 点的个数
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static int get_point_map_count(void)
{
    return g_iPointMapCnt;
}

/**********************************************************************
 * 函数名称： add_point_map
 * 功能描述： 往映射表里添加一个点的映射关系
 * 输入参数： ptPointMap - 点的映射结构体指针
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static void add_point_map(PPointMap ptPointMap)
{
    if (g_iPointMapCnt < MAX_POINT_COUNT)
    {
        g_tPointMaps[g_iPointMapCnt] = *ptPointMap;
        g_iPointMapCnt++;
    }
}
static void get_modbus_mapping_reg(PPointMap ptPointMap, modbus_mapping_t *mb_mapping, int *pVal);


/**********************************************************************
 * 函数名称： loop_once
 * 功能描述： CH0_Task/CH1_Task/CH2_Task任务的一次循环函数
 * 输入参数： ctx - modbus上下文
 *            channel - 通道
 *            mb_mapping - 里面含有DI/DO/AI/AO寄存器
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static int get_cur_status(int channel)
{
	int count = get_point_map_count();
	int i;
	int val = -1;

    modbus_mapping_t *mb_mapping = g_tChannelInfos[channel].mb_mapping;

	/* 如果modbus_mapping_t里这个channel的点发生了变化,发起写操作 */
	for (i = 0; i < count; i++)
	{
		if (g_tPointMaps[i].channel == channel && g_tPointMaps[i].reg_addr_salve == 0 && !strcmp(g_tPointMaps[i].reg_type, "4x"))
		{
			get_modbus_mapping_reg(&g_tPointMaps[i], mb_mapping, &val);
			return val;
			
		}
	}
    return val;   	
}

/**********************************************************************
 * 函数名称： BE32toLE32
 * 功能描述： 把大字节序转换为小字节序
 * 输入参数： buf - 里面存有4个字节的大字节序数据
 * 输出参数： 无
 * 返 回 值： 小字节序的uint32_t数值
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static uint32_t BE32toLE32(uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | ((uint32_t)buf[3] << 0);
}

/**********************************************************************
 * 函数名称： parse_map_info
 * 功能描述： 解析映射信息, 存入映射表
 * 输入参数： msg - 里面存有上位机发来的消息, 消息为"write file record"请求包
 *            msg_len - 消息长度
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static void parse_map_info(uint8_t *msg, uint16_t msg_len)
{
    static FileInfo tFileInfo;
    static int recv_len = 0;
    int cur_len;
    PPointMap ptPointMap;
    
    uint16_t file_no;
    uint16_t record_no;
    char buf[100];

    file_no = ((uint16_t)msg[4]<<8) | msg[5];
    record_no = ((uint16_t)msg[6]<<8) | msg[7];

    if (record_no == 0)
    {
		/* 收到文件头,则算出文件大小,并清除旧的映射表 */
		/* 使用memcpy比较安全,因为"&msg[10]"不一定是对齐的地址 */
        memcpy(&tFileInfo, &msg[10], sizeof(tFileInfo));
        tFileInfo.file_len = BE32toLE32((uint8_t *)&tFileInfo.file_len);
        recv_len = 0;
        clear_point_map();
        sprintf(buf, "Get map len %d", tFileInfo.file_len);
        Draw_String(0, 0, buf, 0xff0000, 0);
    }
    else
    {
        cur_len = msg[2] - 7;

		memcpy(g_pucPointMapCurPos, &msg[10], cur_len);
		g_pucPointMapCurPos += cur_len;
		g_iPointMapCnt = (uint32_t)(g_pucPointMapCurPos - (uint8_t *)g_tPointMaps) / sizeof(PointMap);
#if 0	
        /* 使用memcpy比较安全,因为"&msg[10]"不一定是对齐的地址, ptPointMap指向不对齐的地址时,会导致add_point_map出错 */
        ptPointMap = (PPointMap)&msg[10];
        cur_len = msg[2] - 7;
        for (int i = 0; i < cur_len / sizeof(PointMap); i++)
        {
            add_point_map(ptPointMap);
            ptPointMap++;
        }
#endif        
        recv_len += cur_len;
        if (recv_len >= tFileInfo.file_len)
        {
            sprintf(buf, "Get %d map info  ", get_point_map_count());
            Draw_String(0, 16, buf, 0xff0000, 0);
        }
    }
}


/**********************************************************************
 * 函数名称： local_read_point
 * 功能描述： 读取中控自己的传感器
 * 输入参数： ptPointMap - 点的映射结构体指针
 * 输出参数： pVal - 用来保存读到的数值
 * 返 回 值： 0-成功, (-1)-失败
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static int local_read_point(PPointMap ptPointMap, int *pVal)
{
    char buf[100];
    if (!ptPointMap->channel)
    {
		if (ptPointMap->reg_addr_salve == 0 && !strcmp(ptPointMap->reg_type, "0x"))
		{
			if (GPIO_PIN_RESET == HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_12))
				*pVal = 1;
			else
				*pVal = 0;
			return 0;
		}
    }
	return -1;
}

/**********************************************************************
 * 函数名称： local_write_point
 * 功能描述： 写中控自己的传感器
 * 输入参数： ptPointMap - 点的映射结构体指针
 *            val - 要写入的值
 * 输出参数： 无
 * 返 回 值： 0-成功, (-1)-失败
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static int local_write_point(PPointMap ptPointMap, int val)
{
    char buf[100];
    
    if (!ptPointMap->channel)
    {
		if (ptPointMap->reg_addr_salve == 0 && !strcmp(ptPointMap->reg_type, "0x"))
		{
			if (val)
				HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, GPIO_PIN_RESET);
			else
				HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, GPIO_PIN_SET);
			return 0;
		}

		if (ptPointMap->reg_addr_salve == MODBUS_UPDATE_REG_ADDR && !strcmp(ptPointMap->reg_type, "4x"))
		{
            sprintf(buf, "get update cmd 0x%x", val);
            Draw_String(0, 80, buf, 0xff0000, 0);

            if (val == MODBUS_PRIVATE_CMD_ENTER_BOOT)
            {
                sprintf(buf, "reboot to enter bootloader");
                Draw_String(0, 96, buf, 0xff0000, 0);
                if (!isBootloader())
                {
                    ResetToBootloader();
                }
            }
            else if (val == MODBUS_PRIVATE_CMD_ENTER_APP)
            {
                sprintf(buf, "reboot to enter application");
                Draw_String(0, 96, buf, 0xff0000, 0);
                ResetToApplication();
            }
            else
            {
                sprintf(buf, "invalid command       ");
                Draw_String(0, 96, buf, 0xff0000, 0);
            }
                
			return 0;
		}

    }
	return -1;
}

/**********************************************************************
 * 函数名称： update_modbus_mapping_reg
 * 功能描述： 根据点的映射信息更新它在mb_mapping里的寄存器数值
 * 输入参数： ptPointMap - 点的映射结构体指针
 *            mb_mapping - 里面含有DI/DO/AI/AO寄存器
 *            val - 要更新的数值
 * 输出参数： 无
 * 返 回 值： 0-成功, (-1)-失败
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static void update_modbus_mapping_reg(PPointMap ptPointMap, modbus_mapping_t *mb_mapping, int val)
{
	if (!strcmp(ptPointMap->reg_type, "0x"))
		mb_mapping->tab_bits[ptPointMap->reg_addr_master] = val;
	if (!strcmp(ptPointMap->reg_type, "1x"))
		mb_mapping->tab_input_bits[ptPointMap->reg_addr_master] = val;
	if (!strcmp(ptPointMap->reg_type, "4x"))
		mb_mapping->tab_registers[ptPointMap->reg_addr_master] = val;
	if (!strcmp(ptPointMap->reg_type, "3x"))
		mb_mapping->tab_input_registers[ptPointMap->reg_addr_master] = val;
}

/**********************************************************************
 * 函数名称： get_modbus_mapping_reg
 * 功能描述： 根据点的映射信息获得它在mb_mapping里的寄存器数值
 * 输入参数： ptPointMap - 点的映射结构体指针
 *            mb_mapping - 里面含有DI/DO/AI/AO寄存器
 * 输出参数： pVal - 用来保存读到的数值
 * 返 回 值： 0-成功, (-1)-失败
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static void get_modbus_mapping_reg(PPointMap ptPointMap, modbus_mapping_t *mb_mapping, int *pVal)
{
	if (!strcmp(ptPointMap->reg_type, "0x"))
		*pVal = mb_mapping->tab_bits[ptPointMap->reg_addr_master];
	if (!strcmp(ptPointMap->reg_type, "1x"))
		*pVal = mb_mapping->tab_input_bits[ptPointMap->reg_addr_master];
	if (!strcmp(ptPointMap->reg_type, "4x"))
		*pVal = mb_mapping->tab_registers[ptPointMap->reg_addr_master];
	if (!strcmp(ptPointMap->reg_type, "3x"))
		*pVal = mb_mapping->tab_input_registers[ptPointMap->reg_addr_master];
}


/**********************************************************************
 * 函数名称： modbus_read_point
 * 功能描述： 根据点的映射信息读取外接的传感器数值
 * 输入参数： ctx - modbus上下文
 *            ptPointMap - 点的映射结构体指针
 * 输出参数： pVal - 用来保存读到的数值
 * 返 回 值： 0-成功, (-1)-失败
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static int modbus_read_point(modbus_t *ctx, PPointMap ptPointMap, int *pVal)
{
    int rc;
	static int cnt = 0;
    char buf[100];
    PChannelInfo ptChannelInfo;
    int err = 0;
    
	if (!ctx)
		return -1;

    ptChannelInfo = get_channelinfo_for_ctx(ctx);
    xSemaphoreTake(ptChannelInfo->xMutex, portMAX_DELAY);
	
    if (ptPointMap->channel)
    {

        *pVal = 0;        
        
        modbus_set_slave(ctx, ptPointMap->dev_addr);

        if (!strcmp(ptPointMap->reg_type, "0x"))
        {
            rc = modbus_read_bits(ctx, ptPointMap->reg_addr_salve, 1, (uint8_t *)pVal);
            if (rc == 1)
                err = 0;
            else
                err = -1;
        }
        else if (!strcmp(ptPointMap->reg_type, "1x"))
        {
            rc = modbus_read_input_bits(ctx, ptPointMap->reg_addr_salve, 1, (uint8_t *)pVal);
            if (rc == 1)
                err = 0;
            else
                err = -1;
        }
        else if (!strcmp(ptPointMap->reg_type, "4x"))
        {
            rc = modbus_read_registers(ctx, ptPointMap->reg_addr_salve, 1, ( uint16_t *)pVal);
            if (rc == 1)
                err = 0;
            else
                err = -1;
        }
        else if (!strcmp(ptPointMap->reg_type, "3x"))
        {
            rc = modbus_read_input_registers(ctx, ptPointMap->reg_addr_salve, 1, (uint16_t *)pVal);
            if (rc == 1)
                err = 0;
            else
                err = -1;
        }
        else
        {
            err = -1;
        }
    }

    xSemaphoreGive(ptChannelInfo->xMutex);
    return err;
}

/**********************************************************************
 * 函数名称： modbus_write_point
 * 功能描述： 根据点的映射信息写外接的传感器数值
 * 输入参数： ctx - modbus上下文
 *            ptPointMap - 点的映射结构体指针
 *            val - 要写入的值
 * 输出参数： 无
 * 返 回 值： 0-成功, (-1)-失败
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static int modbus_write_point(modbus_t *ctx, PPointMap ptPointMap, int val)
{
    int rc;
    static int cnt = 0;
    char buf[100];
    PChannelInfo ptChannelInfo;
    int err = 0;

	if (!ctx)
	{
		return -1;
	}

    ptChannelInfo = get_channelinfo_for_ctx(ctx);
    xSemaphoreTake(ptChannelInfo->xMutex, portMAX_DELAY);

    if (ptPointMap->channel)
    {

        modbus_set_slave(ctx, ptPointMap->dev_addr);
        
        if (!strcmp(ptPointMap->reg_type, "0x"))
        {
            rc = modbus_write_bit(ctx, ptPointMap->reg_addr_salve, val);
            if (rc == 1)
                err = 0;
            else 
            {
                err = -1;
            }
        }
        else if (!strcmp(ptPointMap->reg_type, "4x"))
        {
            rc = modbus_write_register(ctx, ptPointMap->reg_addr_salve, val);
            if (rc == 1)
                err = 0;
            else
            {
                if (ptPointMap->channel == 2 && ptPointMap->reg_addr_salve == 0 && !strcmp(ptPointMap->reg_type, "4x"))
                {
                    sprintf(buf, "write reg err: val = 0x%x, cnt = %d", val, ++cnt);
                    Draw_String(0, 212, buf, 0xff0000, 0);
                }
                err = -1;
            }
        }
        else
        {
            //printf("can not write for %s\n", ptInfo->reg_type);
            err = -1;
        }
    }
    else
    {
        err = -1;
    }

    xSemaphoreGive(ptChannelInfo->xMutex);
    return err;
}

/**********************************************************************
 * 函数名称： process_emergency_cmd
 * 功能描述： 处理紧急的命令(比如复位), 这类命令要立即执行或立即发给传感器并得到结果
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
static int process_emergency_cmd(modbus_t *ctx, uint8_t *msg, uint16_t msg_len, modbus_mapping_t *mb_mapping)
{    
	int count = get_point_map_count();
	int i;
    PChannelInfo ptChannelInfo;
    int reg_addr_master;
    int val;
    int write = 1;
    PPointMap ptPointMap = NULL;
    int err = 0;
    static int cnt = 0;
    char buf[100];

    reg_addr_master = ((uint16_t)msg[2]<<8) | msg[3];
    val             = ((uint16_t)msg[4]<<8) | msg[5];
    
    /* 只处理写操作 */
    if (msg[1] != MODBUS_FC_WRITE_SINGLE_REGISTER)
    {
        return 0;
    }

    /* 找到映射的点 */
	for (i = 0; i < count; i++)
	{
		if (g_tPointMaps[i].reg_addr_master == reg_addr_master && !strcmp(g_tPointMaps[i].reg_type, "4x"))
		{
            ptPointMap = &g_tPointMaps[i];
            break;
		}
	}

    if (!ptPointMap)
        return 0;

    /* 如果不是CMD_STATUS寄存器直接返回 */
    if ((ptPointMap->reg_addr_salve != MODBUS_UPDATE_REG_ADDR))
        return 0;
    
    /* 对于中控: 直接操作 */
    if (ptPointMap->channel == 0)
    {
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
    }
    else
    {
        if (val == MODBUS_PRIVATE_CMD_ENTER_BOOT)
            SetUpdateStatus(1);
        
        /* 对于外接的传感器,直接下发命令 */
        ptChannelInfo = get_channelinfo(ptPointMap->channel);
        if (!ptChannelInfo->ctx)
            return 0;

        err = modbus_write_point(ptChannelInfo->ctx, ptPointMap, val);
        sprintf(buf, "modbus_write_point reg %d, val = 0x%x, err = %d, cnt = %d", ptPointMap->reg_addr_salve, val, err, cnt++);
        Draw_String(0, 228, buf, 0xff0000, 0);

        /* 出错或升级完毕 */
        if (err || (val == MODBUS_PRIVATE_CMD_ENTER_APP))
        {
            SetUpdateStatus(0);
        }
        
        return err;
    }

    return 0;
}


/**********************************************************************
 * 函数名称： send_firmware_to_device
 * 功能描述： 发送固件给下级传感器
 * 输入参数： msg - 里面存有上位机发来的消息
 *            msg_len - 消息长度
 * 输出参数： 无
 * 返 回 值： 0-成功, (-1)-失败
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/25        V1.0     韦东山       创建
 ***********************************************************************/
static int send_firmware_to_device(uint8_t *msg, uint16_t msg_len)
{
    uint16_t file_no;
    uint16_t dev_addr;
    uint16_t record_no;
    uint16_t len;
    uint16_t channel;
    PChannelInfo ptChannelInfo;

    int rc;

    char buf[100];
    static int cnt = 0;
    int time;
    
    /* file_no是16位的数值
     * 约定:
     * bit[15:12]: channel
     * bit[11:8] : 真正的file_no, 0-map信息,1-固件
     * bit[7:0]  : dev_addr
     */
    file_no = ((uint16_t)msg[4]<<8) | msg[5];    /* 比如0x2103 */
    record_no = ((uint16_t)msg[6]<<8) | msg[7];
    
    channel  = (file_no >> 12) & 0xf;  /* 对于0x2103, channel    = 2 */
    dev_addr = (file_no) & 0xff;       /* 对于0x2103, dev_addr = 3 */
    file_no  = (file_no >> 8) & 0xf;   /* 对于0x2103, file_no    = 1 */

    len = ((uint16_t)msg[8]<<8) | msg[9];
    len *= 2;
    
    ptChannelInfo = get_channelinfo(channel);
    if (!ptChannelInfo)
        return -1;

    if (!ptChannelInfo->ctx)
        return -1;

    xSemaphoreTake(ptChannelInfo->xMutex, portMAX_DELAY);

    time = HAL_GetTick();
    modbus_set_slave(ptChannelInfo->ctx, dev_addr);
    rc = modbus_write_file_record(ptChannelInfo->ctx, file_no, record_no, &msg[10], len);
    time = HAL_GetTick() - time;

    xSemaphoreGive(ptChannelInfo->xMutex);

    sprintf(buf, "Send file to sensor: record_no = %d, cur len = %d", record_no, len);
    Draw_String(0, 244, buf, 0xff0000, 0);
    sprintf(buf, "use time = %d ms, rc = %d, cnt = %d",  time, rc, ++cnt);
    Draw_String(0, 260, buf, 0xff0000, 0);
    
    if (rc < 0)
        return -1;
    else
        return 0;
}

/**********************************************************************
 * 函数名称： process_file_record
 * 功能描述： 解析文件信息
 * 输入参数： msg - 里面存有上位机发来的消息
 *            msg_len - 消息长度
 * 输出参数： 无
 * 返 回 值： 0-成功, (-1)-失败
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static int process_file_record(uint8_t *msg, uint16_t msg_len)
{
    uint16_t file_no;
    uint16_t channel;
    uint16_t dev_addr;
    uint16_t record_no;
    int err = 0;
    
    if (msg[1] == MODBUS_FC_WRITE_FILE_RECORD)
    {
        file_no = ((uint16_t)msg[4]<<8) | msg[5];
        record_no = ((uint16_t)msg[6]<<8) | msg[7];

        /* file_no是16位的数值
         * 约定:
         * bit[15:12]: channel
         * bit[11:8] : 真正的file_no, 0-map信息,1-固件
         * bit[7:0]  : dev_addr
         */

        channel  = (file_no >> 12) & 0xf;
        dev_addr = (file_no) & 0xff;
        file_no  = (file_no >> 8) & 0xf;
        
        if (channel == 0 && file_no  == FILE_NUMBER_POINT_MAP)
        {
            /* 处理"点"的映射信息 */
            parse_map_info(msg, msg_len);
        }

        if (channel == 0 && file_no == FILE_NUMBER_FIRMWARE)
        {
            /* 给主控烧写固件 */
            burn_firmware(msg, msg_len);
        }

        if (channel != 0 && file_no == FILE_NUMBER_FIRMWARE)
        {
            /* 转发固件给传感器 */
            SetUpdateStatus(1);
            err =send_firmware_to_device(msg, msg_len);
            if (err)
            {
                SetUpdateStatus(0);
            }
        }
		
    }

    return err;
}

/**********************************************************************
 * 函数名称： loop_once
 * 功能描述： CH0_Task/CH1_Task/CH2_Task任务的一次循环函数
 * 输入参数： ctx - modbus上下文
 *            channel - 通道
 *            mb_mapping - 里面含有DI/DO/AI/AO寄存器
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static void loop_once(modbus_t *ctx, int channel, modbus_mapping_t *mb_mapping)
{
	int count = get_point_map_count();
	int i;
	int val;
	int pre_val;
    char buf[100];
    static int cnt = 0;

	/* 如果modbus_mapping_t里这个channel的点发生了变化,发起写操作 */
	for (i = 0; i < count; i++)
	{
        /* 不要操作CMD_STATUS寄存器 */
        if (g_tPointMaps[i].reg_addr_salve == MODBUS_UPDATE_REG_ADDR && !strcmp(g_tPointMaps[i].reg_type, "4x"))
            continue;
        
		/* channel不等于0,表示要使用modbus函数访问外接的传感器 */
		if (g_tPointMaps[i].channel == channel && channel)
		{
			get_modbus_mapping_reg(&g_tPointMaps[i], mb_mapping, &val);
			if (val != g_iPointVals[i])
			{                
				if (0 == modbus_write_point(ctx, &g_tPointMaps[i], val))
				{
					update_modbus_mapping_reg(&g_tPointMaps[i], mb_mapping, val);
					g_iPointVals[i] = val;
				}
			}
		}

		/* channel等于0,表示要访问中控自己的传感器 */
		if (g_tPointMaps[i].channel == channel && !channel)
		{
			get_modbus_mapping_reg(&g_tPointMaps[i], mb_mapping, &val);
			if (val != g_iPointVals[i])
			{
				if (0 == local_write_point(&g_tPointMaps[i], val))
				{
					update_modbus_mapping_reg(&g_tPointMaps[i], mb_mapping, val);
					g_iPointVals[i] = val;
				}
			}
		}
	}

	/* 然后更新映射表里channel通道的所有点 */
	for (i = 0; i < count; i++)
	{
        /* 不要操作CMD_STATUS寄存器 */
        if (g_tPointMaps[i].reg_addr_salve == MODBUS_UPDATE_REG_ADDR && !strcmp(g_tPointMaps[i].reg_type, "4x"))
            continue;
        
		/* channel不等于0,表示要使用modbus函数访问外接的传感器 */
		if (g_tPointMaps[i].channel == channel && channel)
		{
			if (0 == modbus_read_point(ctx, &g_tPointMaps[i], &val))
			{
                sprintf(buf, "update mapping val = 0x%x, preVal = 0x%x, ok cnt = %d", val, g_iPointVals[i], cnt++);
                Draw_String(0, 100+32, buf, 0xff0000, 0);
				update_modbus_mapping_reg(&g_tPointMaps[i], mb_mapping, val);
			}
            else
            {
                sprintf(buf, "update mapping val = 0x%x, preVal = 0x%x, err cnt = %d", val, g_iPointVals[i], cnt++);
                Draw_String(0, 100+32, buf, 0xff0000, 0);
            }
		}

		/* channel等于0,表示要访问中控自己的传感器 */
		if (g_tPointMaps[i].channel == channel && !channel)
		{
			if (0 == local_read_point(&g_tPointMaps[i], &val))
				update_modbus_mapping_reg(&g_tPointMaps[i], mb_mapping, val);
		}
	}
}

/**********************************************************************
 * 函数名称： CH0_Task
 * 功能描述： 中控任务, 用来读写中控自己的传感器
 * 输入参数： pvParameters - 就是mb_mapping(里面含有DI/DO/AI/AO寄存器)
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static void CH0_Task( void *pvParameters )	
{
	modbus_mapping_t * mb_mapping = pvParameters;
	
	while (1)
	{
		loop_once(NULL, 0, mb_mapping);

		vTaskDelay(100);
	}
	vTaskDelete(NULL);
}

/**********************************************************************
 * 函数名称： CHn_Task
 * 功能描述： CHn任务, 用来读写接在CHn上的传感器(n>=1)
 * 输入参数： pvParameters - 就是mb_mapping(里面含有DI/DO/AI/AO寄存器)
 * 输出参数： 无
 * 返 回 值： 无
 * 修改日期：      版本号     修改人       修改内容
 * -----------------------------------------------
 * 2024/06/23        V1.0     韦东山       创建
 ***********************************************************************/
static void CHn_Task( void *pvParameters )	
{
	PChannelInfo ptChannelInfo = (PChannelInfo)pvParameters;
	modbus_mapping_t * mb_mapping = ptChannelInfo->mb_mapping;

	modbus_t *ctx;
	int rc;
    int wait_cnt;

#if 0
    int time;
    char buf[100];
    int cnt = 0;
    int file_no = 1;
    int record_no = 0;
    uint8_t *msg = pvPortMalloc(260);
    int len = 240;
#endif

	ctx = modbus_new_st_rtu(ptChannelInfo->uart_name, 115200, 'N', 8, 1);
    ptChannelInfo->ctx = ctx;

	rc = modbus_connect(ctx);
	if (rc == -1) {
		//fprintf(stderr, "Unable to connect %s\n", modbus_strerror(errno));
		modbus_free(ctx);
		vTaskDelete(NULL);;
	}
	
	while (1)
	{
        if (!isUpdating())
        {
            wait_cnt = 0;
    		loop_once(ctx, ptChannelInfo->channel, mb_mapping);
        }
        else
        {
            wait_cnt++;
            if (wait_cnt >= 1000) /* 下面每次VTasDelay 100毫秒, 1000次=100秒 */
            {
                SetUpdateStatus(0); /* 100秒足够升级了 */
            }
        }
            
        vTaskDelay(100);
	}
	vTaskDelete(NULL);
}

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
void LibmodbusServerTask( void *pvParameters )	
{
    uint8_t *query;
    modbus_t *ctx;
    int rc;
    modbus_mapping_t *mb_mapping;
	PChannelInfo ptChannelInfo;
	int channel;
    int err;

    if (isBootloader())
    {
        Draw_String(150, 0, "Bootloader", 0xff0000, 0);
    }
    else
    {
        Draw_String(150, 0, "Application", 0xff0000, 0);
    }
    
    ctx = modbus_new_st_rtu("usb", 115200, 'N', 8, 1);
    modbus_set_slave(ctx, 1);
    query = pvPortMalloc(MODBUS_RTU_MAX_ADU_LENGTH);

    mb_mapping = modbus_mapping_new_start_address(0,
                                                  500,
                                                  0,
                                                  500,
                                                  0,
                                                  500,
                                                  0,
                                                  500);
    
    memset(mb_mapping->tab_bits, 0, mb_mapping->nb_bits);
    memset(mb_mapping->tab_registers, 0, mb_mapping->nb_registers*2);
    
    rc = modbus_connect(ctx);
    if (rc == -1) {
        //fprintf(stderr, "Unable to connect %s\n", modbus_strerror(errno));
        modbus_free(ctx);
        vTaskDelete(NULL);;
    }

    for (channel = 1; channel < MAX_CHANNEL_NUMBER; channel++)
    {
        ptChannelInfo = &g_tChannelInfos[channel];
        ptChannelInfo->mb_mapping = mb_mapping;
        ptChannelInfo->xMutex     = xSemaphoreCreateMutex();
    }
        
    /* bootloader无需启动CH0, CH1,CH2任务 */
    if (!isBootloader())
    {
        xTaskCreate(
            CH0_Task, // 函数指针, 任务函数
            "CH0_Task", // 任务的名称
            400, // 栈大小
            mb_mapping, // 调用任务函数时传入的参数
            osPriorityNormal, // 优先级
            NULL); // 任务句柄, 以后使用它来操作这个任务
		
		for (channel = 1; channel < MAX_CHANNEL_NUMBER; channel++)
		{			
            ptChannelInfo = &g_tChannelInfos[channel];
	    	xTaskCreate(
	    		CHn_Task, // 函数指针, 任务函数
	    		ptChannelInfo->uart_name, // 任务的名称
	    		400, // 栈大小
	    		ptChannelInfo, // 调用任务函数时传入的参数
	    		osPriorityNormal, // 优先级
	    		NULL); // 任务句柄, 以后使用它来操作这个任务
		}
    }

    for (;;) {
        do {
            rc = modbus_receive(ctx, query);
            /* Filtered queries return 0 */
        } while (rc == 0);
 
        /* The connection is not closed on errors which require on reply such as
           bad CRC in RTU. */
        if (rc < 0 ) {
            /* Quit */
            continue;
        }

        err = process_emergency_cmd(ctx, query, rc, mb_mapping);
        if (err)
        {
            modbus_reply_exception(ctx, query, MODBUS_EXCEPTION_SLAVE_OR_SERVER_BUSY);
            continue;
        }
        
        err = process_file_record(query, rc);
        if (err)
        {
            modbus_reply_exception(ctx, query, MODBUS_EXCEPTION_SLAVE_OR_SERVER_BUSY);
            continue;
        }
        
        rc = modbus_reply(ctx, query, rc, mb_mapping);
        if (rc == -1) {
            //break;
        }
    }

    modbus_mapping_free(mb_mapping);
    vPortFree(query);
    /* For RTU */
    modbus_close(ctx);
    modbus_free(ctx);

    vTaskDelete(NULL);
}

