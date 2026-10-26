/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : app_freertos.c
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

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern UART_HandleTypeDef huart4;
extern UART_HandleTypeDef huart2;

static SemaphoreHandle_t g_xBinarySemaphoreSwitch;
static SemaphoreHandle_t g_xBinarySemaphoreENV;
static SemaphoreHandle_t g_xBinarySemaphoreTempHumi;
static SemaphoreHandle_t g_ch1_lock;

modbus_mapping_t *g_mb_mapping;

static modbus_t *g_ch1_ctx;


int Wait_UART2_Tx_Complete(int timeout);
int Wait_UART4_Rx_Complete(int timeout);
int UART4_GetData(uint8_t *pData);
void UART4_Rx_Start(void);

/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .priority = (osPriority_t) osPriorityNormal,
  .stack_size = 128 * 4
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

static void LibmodbusClientTask( void *pvParameters )	
{
	modbus_t *ctx;
	int rc;
	uint16_t val;
	int nb = 1;
	
	ctx = modbus_new_st_rtu("usb", 115200, 'N', 8, 1);
	modbus_set_slave(ctx, 1);
	
	rc = modbus_connect(ctx);
	if (rc == -1) {
		//fprintf(stderr, "Unable to connect %s\n", modbus_strerror(errno));
		modbus_free(ctx);
		vTaskDelete(NULL);;
	}

	for (;;) {
		/* read hoding register 1 */
		rc = modbus_read_registers(ctx, 1, nb, &val);
		if (rc != nb)
			continue;

		/* display on lcd */
		Draw_Number(0, 0, val, 0xff0000);

		/* val ++ */
		val++;

		/* write val to hoding register 2 */
		rc = modbus_write_registers(ctx, 2, nb, &val);
	}

	/* For RTU */
	modbus_close(ctx);
	modbus_free(ctx);

	vTaskDelete(NULL);
}



static void LibmodbusCH1SwitchClientTask( void *pvParameters )	
{
	modbus_t *ctx;
	int rc;
	uint16_t vals[10];
	int nb = 1;
    uint8_t bits[10];
    uint8_t buf[100];
    int led_status = 1;

    ctx = g_ch1_ctx;

    xSemaphoreTake(g_ch1_lock, portMAX_DELAY);
    
    modbus_set_slave(ctx, 1);
    rc = modbus_read_bits(ctx, 0, 5, bits);
    if (rc == 5)
    {
        memcpy(&g_mb_mapping->tab_bits[1], bits, 5);
    }
    
    xSemaphoreGive(g_ch1_lock);
    

	for (;;) {

        /* read switch(ID=1), get status of key1,key2,key3 
         * display on lcd
         * blink led
         */
        xSemaphoreTake(g_ch1_lock, portMAX_DELAY);
        vTaskDelay(20);
        modbus_set_slave(ctx, 1);
        rc = modbus_read_input_bits(ctx, 0, 3, bits);
        if (rc == 3)
        {
            sprintf((char *)buf, "SWITCH keys: %d %d %d", bits[0], bits[1], bits[2]);
            Draw_String(0, 0, (char *)buf, 0xff0000, 0);
        
            /* update DI register */
            memcpy(g_mb_mapping->tab_input_bits, bits, 3);
        }
        xSemaphoreGive(g_ch1_lock);
        
        /* wait for PC update DO */
        if (xSemaphoreTake(g_xBinarySemaphoreSwitch, 500) == pdTRUE)
        {
            xSemaphoreTake(g_ch1_lock, portMAX_DELAY);
            vTaskDelay(20);
            modbus_set_slave(ctx, 1);
            rc = modbus_write_bits(ctx, 0, 5, &g_mb_mapping->tab_bits[1]);            
            xSemaphoreGive(g_ch1_lock);
        }
	}

	/* For RTU */
	modbus_close(ctx);
	modbus_free(ctx);

	vTaskDelete(NULL);
}




static void LibmodbusCH1ENVClientTask( void *pvParameters )	
{
	modbus_t *ctx;
	int rc;
	uint16_t vals[10];
	int nb = 1;
    uint8_t bits[10];
    uint8_t buf[100];
    int led_status = 1;

    ctx = g_ch1_ctx;

    xSemaphoreTake(g_ch1_lock, portMAX_DELAY);
    
    modbus_set_slave(ctx, 2);
    rc = modbus_read_bits(ctx, 0, 5, bits);
    if (rc == 5)
    {
        memcpy(&g_mb_mapping->tab_bits[6], bits, 5);
    }
    
    xSemaphoreGive(g_ch1_lock);

	for (;;) {

        /* read switch(ID=2), get status of key1,key2,key3 
         * display on lcd
         * blink led
         */
        xSemaphoreTake(g_ch1_lock, portMAX_DELAY);
        vTaskDelay(20);
        modbus_set_slave(ctx, 2);
		rc = modbus_read_input_registers(ctx, 0, 2, vals);
		if (rc == 2)
		{
            sprintf((char *)buf, "ENV Sensor : opti 0x%x, res 0x%x          ", vals[0], vals[1]);
            Draw_String(0, 16, (char *)buf, 0xff0000, 0);

            /* update AI register */
            memcpy(g_mb_mapping->tab_input_registers, vals, 4);
		}
        xSemaphoreGive(g_ch1_lock);

        /* wait for PC update DO */
        if (xSemaphoreTake(g_xBinarySemaphoreENV, 500) == pdTRUE)
        {
            xSemaphoreTake(g_ch1_lock, portMAX_DELAY);
            vTaskDelay(20);
            modbus_set_slave(ctx, 2);
            rc = modbus_write_bits(ctx, 0, 5, &g_mb_mapping->tab_bits[6]);            
            xSemaphoreGive(g_ch1_lock);
        }

	}

	/* For RTU */
	modbus_close(ctx);
	modbus_free(ctx);

	vTaskDelete(NULL);
}

static void LibmodbusCH2TempHumiClientTask( void *pvParameters )	
{
	modbus_t *ctx;
	int rc;
	uint16_t vals[10];
	int nb = 1;
    uint8_t bits[10];
    uint8_t buf[100];
    int led_status = 1;
	
	ctx = modbus_new_st_rtu("uart4", 115200, 'N', 8, 1);
	modbus_set_slave(ctx, 3);
	
	rc = modbus_connect(ctx);
	if (rc == -1) {
		//fprintf(stderr, "Unable to connect %s\n", modbus_strerror(errno));
		modbus_free(ctx);
		vTaskDelete(NULL);;
	}

    rc = modbus_read_bits(ctx, 0, 5, bits);
    if (rc == 5)
    {
        memcpy(&g_mb_mapping->tab_bits[11], bits, 5);
    }

	for (;;) {

        /* read tem_hum(ID=3), get temp, humi
         * display on lcd
         * blink led
         */
		rc = modbus_read_input_registers(ctx, 0, 2, vals);
		if (rc == 2)
		{
            sprintf((char *)buf, "TEM/HUM Sensor : temp %d.%d, humi %d.%d          ", vals[0]/10, vals[0]%10, vals[1]/10, vals[1]%10);
            Draw_String(0, 32, (char *)buf, 0xff0000, 0);

            /* update AI register */
            memcpy(&g_mb_mapping->tab_input_registers[2], vals, 4);
		}

        /* wait for PC update DO */
        if (xSemaphoreTake(g_xBinarySemaphoreTempHumi, 500) == pdTRUE)
        {
            rc = modbus_write_bits(ctx, 0, 5, &g_mb_mapping->tab_bits[11]);            
        }
	}

	/* For RTU */
	modbus_close(ctx);
	modbus_free(ctx);

	vTaskDelete(NULL);
}

static void RS485toUSB( void *pvParameters )	
{
    struct UART_Device *pUSBUART = GetUARTDevice("usb");
    struct UART_Device *pRS485UART = GetUARTDevice("uart4");
    uint8_t data;

    while (1)
    {
        if (0 == pRS485UART->RecvByte(pRS485UART, &data, portMAX_DELAY))
        {
            pUSBUART->Send(pUSBUART, &data, 1, 100);
        }
    }
}


static void USBtoRS485( void *pvParameters )	
{
    struct UART_Device *pUSBUART = GetUARTDevice("usb");
    struct UART_Device *pRS485UART = GetUARTDevice("uart4");
    uint8_t data;

    while (1)
    {
        if (0 == pUSBUART->RecvByte(pUSBUART, &data, portMAX_DELAY))
        {
            pRS485UART->Send(pRS485UART, &data, 1, 100);
        }
    }
}


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
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
#if 1  
	xTaskCreate(
		LibmodbusServerTask, // 函数指针, 任务函数
		"LibmodbusServerTask", // 任务的名�?
		400, // 栈大�?,单位为word,10表示40字节
		NULL, // 调用任务函数时传入的参数
		osPriorityNormal, // 优先�?
		NULL); // 任务句柄, 以后使用它来操作这个任务
    
#else
	xTaskCreate(
		LibmodbusClientTask, // 函数指针, 任务函数
		"LibmodbusClientTask", // 任务的名�?
		200, // 栈大�?,单位为word,10表示40字节
		NULL, // 调用任务函数时传入的参数
		osPriorityNormal, // 优先�?
		NULL); // 任务句柄, 以后使用它来操作这个任务
#endif

#if 0
  xTaskCreate(
	  USBtoRS485, // 函数指针, 任务函数
	  "USBtoRS485", // 任务的名
	  200, // �?
	  NULL, // 调用任务函数时传入的参数
	  osPriorityNormal, // 优先�?
	  NULL); // 任务句柄, 以后使用它来操作这个任务

  xTaskCreate(
	  RS485toUSB, // 函数指针, 任务函数
	  "RS485toUSB", // 任务的名
	  200, // �?
	  NULL, // 调用任务函数时传入的参数
	  osPriorityNormal, // 优先�?
	  NULL); // 任务句柄, 以后使用它来操作这个任务
#endif

#if 0
  g_xBinarySemaphoreSwitch = xSemaphoreCreateBinary( );
  g_xBinarySemaphoreENV    = xSemaphoreCreateBinary( );
  g_xBinarySemaphoreTempHumi = xSemaphoreCreateBinary( );
  g_ch1_lock = xSemaphoreCreateMutex();

  /* Task1 */
  xTaskCreate(
      LibmodbusServerTask, // 函数指针, 任务函数
      "LibmodbusServerTask", // 任务的名�?
      300, // 栈大�?,单位为word,10表示40字节
      NULL, // 调用任务函数时传入的参数
      osPriorityNormal, // 优先�?
      NULL); // 任务句柄, 以后使用它来操作这个任务

      g_ch1_ctx = modbus_new_st_rtu("uart2", 115200, 'N', 8, 1);
      modbus_set_slave(g_ch1_ctx, 1);
      
      if (modbus_connect(g_ch1_ctx) == -1) {
          //fprintf(stderr, "Unable to connect %s\n", modbus_strerror(errno));
          modbus_free(modbus_connect(g_ch1_ctx));
          return;
      }


  /* Task2 */
  xTaskCreate(
      LibmodbusCH1SwitchClientTask, // 函数指针, 任务函数
      "LibmodbusCH1SwitchClientTask", // 任务的名�?
      300, // 栈大�?,单位为word,10表示40字节
      NULL, // 调用任务函数时传入的参数
      osPriorityNormal, // 优先�?
      NULL); // 任务句柄, 以后使用它来操作这个任务

  /* Task3 */
  xTaskCreate(
      LibmodbusCH1ENVClientTask, // 函数指针, 任务函数
      "LibmodbusCH1ENVClientTask", // 任务的名�?
      300, // 栈大�?,单位为word,10表示40字节
      NULL, // 调用任务函数时传入的参数
      osPriorityNormal, // 优先�?
      NULL); // 任务句柄, 以后使用它来操作这个任务

  /* Task4 */
  xTaskCreate(
      LibmodbusCH2TempHumiClientTask, // 函数指针, 任务函数
      "LibmodbusCH2TempHumiClientTask", // 任务的名�?
      300, // 栈大�?,单位为word,10表示40字节
      NULL, // 调用任务函数时传入的参数
      osPriorityNormal, // 优先�?
      NULL); // 任务句柄, 以后使用它来操作这个任务
#endif

#if 0
  xTaskCreate(
	  CH1_UART2_ClientTask, // 函数指针, 任务函数
	  "CH1_UART2_ClientTask", // 任务的名
	  400, // 栿
	  NULL, // 调用任务函数时传入的参数
	  osPriorityNormal, // 优先线
	  NULL); // 任务句柄, 以后使用它来操作这个任务

  xTaskCreate(
	  CH2_UART4_ServerTask, // 函数指针, 任务函数
	  "CH2_UART4_ServerTask", // 任务的名
	  400, // 栿
	  NULL, // 调用任务函数时传入的参数
	  osPriorityNormal, // 优先线
	  NULL); // 任务句柄, 以后使用它来操作这个任务
#endif
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}
/* USER CODE BEGIN Header_StartDefaultTask */
/**
* @brief Function implementing the defaultTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN defaultTask */
  /* Infinite loop */
  for(;;)
  {
// 		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, GPIO_PIN_RESET);
//		vTaskDelay(500);
//		
//		HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, GPIO_PIN_SET);
//		vTaskDelay(500);
	  ux_system_tasks_run();
  }
  /* USER CODE END defaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

