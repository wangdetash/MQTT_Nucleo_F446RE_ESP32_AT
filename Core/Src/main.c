/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "mqtt_helper.h"
#include "application_config.h"

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
UART_HandleTypeDef huart4;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_uart4_rx;

/* USER CODE BEGIN PV */
/* xTaskCreate stack depth is in StackType_t words: 2048 = 8192 bytes. */
#define MQTT_TASK_STACK_DEPTH 2048U
static SemaphoreHandle_t mqtt_mutex;
static volatile uint32_t pending_interrupts;
static uint32_t count;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_UART4_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
static void mqtt_publish_task(void *argument);
static void mqtt_sucscribe_task(void *argument);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
int __io_putchar(int ch)
{
  uint8_t byte = (uint8_t)ch;
  if (HAL_UART_Transmit(&huart2, &byte, 1, HAL_MAX_DELAY) != HAL_OK)
  {
    return EOF;
  }
  return ch;
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_UART4_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("Application running ...\r\n");
  esp32_status_t esp_status = esp32_init();
  /* Initialize UART DMA before requesting the module's version. */
  static char firmware_version[1024];
  esp32_status_t version_status = esp32_get_firmware_version(
      firmware_version, sizeof(firmware_version));
  if (version_status == ESP32_OK)
  {
    printf("ESP-AT firmware version:\r\n%s", firmware_version);
  }
  else
  {
    printf("ESP-AT firmware version query failed (status=%d)\r\n",
           (int)version_status);
  }
  printf("esp32_init: %s (status=%d)\r\n",
         esp_status == ESP32_OK ? "succeeded" : "failed", (int)esp_status);

  esp_status = esp32_join_ap((uint8_t *)WIFI_SSID, (uint8_t *)WIFI_PASSWORD);
  printf("esp32_join_ap: %s (status=%d)\r\n",
         esp_status == ESP32_OK ? "succeeded" : "failed", (int)esp_status);

  esp_status = esp32_config_sntp(UTC_OFFSET);
  printf("esp32_config_sntp: %s (status=%d)\r\n",
         esp_status == ESP32_OK ? "succeeded" : "failed", (int)esp_status);

  mqtt_status_t mqtt_status = mqtt_connect(CLIENT_ID, MQTT_BROKER, MQTT_PORT);
  printf("mqtt_connect: %s (status=%d)\r\n",
         mqtt_status == MQTT_SUCCESS ? "succeeded" : "failed", (int)mqtt_status);

  mqtt_mutex = xSemaphoreCreateMutex();
  if (mqtt_mutex == NULL ||
      xTaskCreate(mqtt_publish_task, "mqtt_publish_task", MQTT_TASK_STACK_DEPTH,
                  NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS /*||
      xTaskCreate(mqtt_sucscribe_task, "mqtt_sucscribe_task", MQTT_TASK_STACK_DEPTH,
                  NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS*/)
  {
    printf("MQTT task creation failed\r\n");
    Error_Handler();
  }
  vTaskStartScheduler();
  Error_Handler(); /* Scheduler returns only if it cannot start. */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 180;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief UART4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_UART4_Init(void)
{

  /* USER CODE BEGIN UART4_Init 0 */

  /* USER CODE END UART4_Init 0 */

  /* USER CODE BEGIN UART4_Init 1 */

  /* USER CODE END UART4_Init 1 */
  huart4.Instance = UART4;
  huart4.Init.BaudRate = 115200;
  huart4.Init.WordLength = UART_WORDLENGTH_8B;
  huart4.Init.StopBits = UART_STOPBITS_1;
  huart4.Init.Parity = UART_PARITY_NONE;
  huart4.Init.Mode = UART_MODE_TX_RX;
  huart4.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart4) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN UART4_Init 2 */

  /* USER CODE END UART4_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream2_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream2_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream2_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PA5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
  if (GPIO_Pin == GPIO_PIN_13 && pending_interrupts != UINT32_MAX)
  {
    ++pending_interrupts;
  }
}

static unsigned int mqtt_month_number(const char *month)
{
  static const char * const months[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
  };
  for (unsigned int i = 0; i < 12; ++i)
  {
    if (strcmp(month, months[i]) == 0) return i + 1;
  }
  return 0;
}

static void mqtt_publish_task(void *argument)
{
  (void)argument;
  for (;;)
  {
    uint32_t interrupt_occurred = 0;
    /* EXTI priority 5 is masked by this critical section. Keep queued events. */
    taskENTER_CRITICAL();
    if (pending_interrupts > 0)
    {
      pending_interrupts = 0;
      interrupt_occurred = 1;
    }
    taskEXIT_CRITICAL();

    if (interrupt_occurred)
    {
      char payload[160];
      sntp_time_t timestamp = {0};
      ++count;
      xSemaphoreTake(mqtt_mutex, portMAX_DELAY);
      printf("count: %lu\r\n", (unsigned long)count);
      esp32_status_t time_status = esp32_get_sntp_time(&timestamp);
      unsigned int month = mqtt_month_number(timestamp.month);
      if (time_status == ESP32_OK && month != 0 &&
          timestamp.year >= 2020 && timestamp.year <= 9999 &&
          timestamp.date >= 1 && timestamp.date <= 31 &&
          timestamp.hour >= 0 && timestamp.hour <= 23 &&
          timestamp.min >= 0 && timestamp.min <= 59 &&
          timestamp.sec >= 0 && timestamp.sec <= 59)
      {
        int length = snprintf(payload, sizeof(payload),
            "{\"count\":%lu,\"time\":\"%02d:%02d:%02d\","
            "\"date\":\"%04d-%02u-%02d\"}",
            (unsigned long)count, timestamp.hour, timestamp.min, timestamp.sec,
            timestamp.year, month, timestamp.date);
        if (length > 0 && (size_t)length < sizeof(payload))
        {
          mqtt_status_t status = mqtt_publish(SENSOR_DATA_TOPIC,
              strlen(SENSOR_DATA_TOPIC), (uint8_t *)payload, (size_t)length);
          printf("MQTT publish %s (status=%d)\r\n",
                 status == MQTT_SUCCESS ? "succeeded" : "failed", (int)status);
        }
        else
        {
          printf("MQTT publish failed: JSON buffer too small\r\n");
        }
      }
      else
      {
        printf("MQTT publish failed: valid SNTP date/time unavailable\r\n");
      }
      xSemaphoreGive(mqtt_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

static void mqtt_sucscribe_task(void *argument)
{
  (void)argument;
  mqtt_status_t status = MQTT_ERROR;
  for (;;)
  {
    if (status != MQTT_SUCCESS)
    {
      xSemaphoreTake(mqtt_mutex, portMAX_DELAY);
//      status = mqtt_subscribe(RAZORPAY_TOPIC, strlen(RAZORPAY_TOPIC));
//      printf("MQTT subscribe %s (status=%d)\r\n",
//             status == MQTT_SUCCESS ? "succeeded" : "failed", (int)status);
//      xSemaphoreGive(mqtt_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
