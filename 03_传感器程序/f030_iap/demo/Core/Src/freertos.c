/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "uart_device.h"
#include "modbus.h"
#include "errno.h"
#include "control.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

//#define USE_SWITCH_SENSOR 1
//#define USE_ENV_MONITOR_SENSOR 1
#define USE_TMP_HUMI_SENSOR 1



#ifdef USE_SWITCH_SENSOR

#define SLAVE_ADDR 1
#define NB_BITS             5
#define NB_INPUT_BITS       3 
#define NB_REGISTERS        1
#define NB_INPUT_REGISTERS  0  

#endif

#ifdef USE_ENV_MONITOR_SENSOR
extern ADC_HandleTypeDef hadc;

#define SLAVE_ADDR 2
#define NB_BITS             5
#define NB_INPUT_BITS       0 
#define NB_REGISTERS        1
#define NB_INPUT_REGISTERS  2  

#endif

#ifdef USE_TMP_HUMI_SENSOR

void AHT20Task(void *argument);
static void aht20_get_datas(uint16_t *temp, uint16_t *humi);

#define SLAVE_ADDR 3
#define NB_BITS             5
#define NB_INPUT_BITS       0 
#define NB_REGISTERS        1
#define NB_INPUT_REGISTERS  2  

#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
#ifdef USE_TMP_HUMI_SENSOR
  osThreadNew(AHT20Task, NULL, &defaultTask_attributes);
#endif
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */

#ifdef USE_TMP_HUMI_SENSOR

//**********************************************************//
//CRC校验类型�? CRC8
//多项式： X8+X5+X4+1
//Poly:0011 0001 0x31
unsigned char Calc_CRC8(unsigned char *message,unsigned char Num)
{
    unsigned char i;
    unsigned char byte;
    unsigned char crc =0xFF;
    for (byte = 0;byte<Num;byte++)
    {
        crc^=(message[byte]);
        for(i=8;i>0;--i)
        {
            if(crc&0x80)
                crc=(crc<<1)^0x31;
            else
                crc=(crc<<1);
        }
    }
    return crc;
}//

static uint32_t g_temp, g_humi;

static void aht20_get_datas(uint16_t *temp, uint16_t *humi)
{
	*temp = g_temp;
	*humi = g_humi;
}

void AHT20Task(void *argument)
{
	uint8_t cmd[] = { 0xAC, 0x33, 0x00};	
	uint8_t datas[7];	
	uint8_t crc;	
	
	extern I2C_HandleTypeDef hi2c1;
	
	vTaskDelay(10); /* wait for ready */

	while (1)
	{
		if (HAL_OK == HAL_I2C_Master_Transmit(&hi2c1, 0x70, cmd, 3, 100))
		{
			vTaskDelay(100); /* wait for ready */
			if (HAL_OK == HAL_I2C_Master_Receive(&hi2c1, 0x70, datas, 7, 100))
			{
				/* cal crc */
				crc = Calc_CRC8(datas, 6);
				if (crc == datas[6])
				{
					/* ok */
					g_humi = ((uint32_t)datas[1] << 12) | ((uint32_t)datas[2] << 4) | ((uint32_t)datas[3] >> 4);
					g_temp = (((uint32_t)datas[3] & 0x0f) << 16) | ((uint32_t)datas[4] << 8) | ((uint32_t)datas[5]);

					g_humi = g_humi * 100 * 10/ 0x100000; /* 0.1% */
					g_temp = g_temp * 200 * 10/ 0x100000 - 500; /* 0.1C */
				}
			}
		}

		vTaskDelay(20);		
	}
}

#endif

/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
    GPIO_PinState val;
	uint8_t *query;
	modbus_t *ctx;
	int rc;
	modbus_mapping_t *mb_mapping;
    int err;

#ifdef USE_ENV_MONITOR_SENSOR
    HAL_ADCEx_Calibration_Start(&hadc);
#endif

	ctx = modbus_new_st_rtu("uart1", 115200, 'N', 8, 1);
	modbus_set_slave(ctx, SLAVE_ADDR);
	query = pvPortMalloc(MODBUS_RTU_MAX_ADU_LENGTH);

	mb_mapping = modbus_mapping_new_start_address(0,
												  NB_BITS,
												  0,
												  NB_INPUT_BITS,
												  0,
												  NB_REGISTERS,
												  0,
												  NB_INPUT_REGISTERS);
	
	rc = modbus_connect(ctx);
	if (rc == -1) {
		//fprintf(stderr, "Unable to connect %s\n", modbus_strerror(errno));
		modbus_free(ctx);
		vTaskDelete(NULL);;
	}

    mb_mapping->tab_registers[0] = MODBUS_PRIVATE_STATUS_IDLE;

	for (;;) {
		do {
			rc = modbus_receive(ctx, query);
			/* Filtered queries return 0 */
		} while (rc == 0);
 
		/* The connection is not closed on errors which require on reply such as
		   bad CRC in RTU. */
		if (rc == -1 && errno != EMBBADCRC) {
			/* Quit */
			continue;
		}

        /* update values of registers
         * a. read gpio
         * b. update reg
         */
#ifdef USE_SWITCH_SENSOR
        /* key1 */
        val = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_3);
        if (val == GPIO_PIN_RESET)
        {
            mb_mapping->tab_input_bits[0] = 1;
        }
        else
        {
            mb_mapping->tab_input_bits[0] = 0;
        }

        /* key2 */
        val = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_4);
        if (val == GPIO_PIN_RESET)
        {
            mb_mapping->tab_input_bits[1] = 1;
        }
        else
        {
            mb_mapping->tab_input_bits[1] = 0;
        }

        /* key3 */
        val = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_5);
        if (val == GPIO_PIN_RESET)
        {
            mb_mapping->tab_input_bits[2] = 1;
        }
        else
        {
            mb_mapping->tab_input_bits[2] = 0;
        }

#endif

#ifdef USE_ENV_MONITOR_SENSOR
        /* READ ADC to update tab_input_registers */
        for (int i = 0; i < 2; i++)
        {
            HAL_ADC_Start(&hadc);
             if (HAL_OK == HAL_ADC_PollForConversion(&hadc, 100))
             {
                mb_mapping->tab_input_registers[i] = HAL_ADC_GetValue(&hadc);
             }
        }
#endif

#ifdef USE_TMP_HUMI_SENSOR
		/* can not start aht20 to capture , it takes 80ms, too long */
		uint16_t temp, humi;
		aht20_get_datas(&temp, &humi);
		mb_mapping->tab_input_registers[0] = temp;
		mb_mapping->tab_input_registers[1] = humi;
#endif
#if 1        
        err = process_emergency_cmd(ctx, query, rc, mb_mapping);
        if (err)
        {
            modbus_reply_exception(ctx, query, MODBUS_EXCEPTION_SLAVE_OR_SERVER_FAILURE);
            continue;
        }
        err = process_file_record(query, rc);
        if (err)
        {
            modbus_reply_exception(ctx, query, MODBUS_EXCEPTION_SLAVE_OR_SERVER_FAILURE);
            continue;
        }
#endif
		rc = modbus_reply(ctx, query, rc, mb_mapping);
		if (rc == -1) {
			//break;
		}

#ifdef USE_SWITCH_SENSOR
        /* switch1 */
		if (mb_mapping->tab_bits[0])
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET); //switch1
		else
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET); //switch1

        /* switch2 */
		if (mb_mapping->tab_bits[1])
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET); //switch2
		else
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET); //switch2

		if (mb_mapping->tab_bits[2])
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET); //LED1
		else
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET); //LED1

		if (mb_mapping->tab_bits[3])
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET); //LED2
		else
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET); //LED2

		if (mb_mapping->tab_bits[4])
            HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET); //LED3
		else
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET); //LED3
#endif


#ifdef USE_ENV_MONITOR_SENSOR
            /* beep1 */
            if (mb_mapping->tab_bits[0])
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_SET); //beep1
            else
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET); //beep1
    
            /* beep2 */
            if (mb_mapping->tab_bits[1])
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET); //beep2
            else
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET); //beep2
    
            if (mb_mapping->tab_bits[2])
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET); //LED1
            else
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET); //LED1
    
            if (mb_mapping->tab_bits[3])
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET); //LED2
            else
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET); //LED2
    
            if (mb_mapping->tab_bits[4])
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET); //LED3
            else
                HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET); //LED3
#endif

#ifdef USE_TMP_HUMI_SENSOR
		/* beep1 */
		if (mb_mapping->tab_bits[0])
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_SET); //beep1
		else
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET); //beep1

		/* beep2 */
		if (mb_mapping->tab_bits[1])
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_SET); //beep2
		else
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET); //beep2

		if (mb_mapping->tab_bits[2])
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET); //LED1
		else
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET); //LED1

		if (mb_mapping->tab_bits[3])
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET); //LED2
		else
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET); //LED2

		if (mb_mapping->tab_bits[4])
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_RESET); //LED3
		else
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET); //LED3

#endif

	}

	modbus_mapping_free(mb_mapping);
	vPortFree(query);
	/* For RTU */
	modbus_close(ctx);
	modbus_free(ctx);

	vTaskDelete(NULL);
    
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

