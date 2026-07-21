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
#include "app_threadx.h"
#include "main.h"
#include "dma2d.h"
#include "eth.h"
#include "i2c.h"
#include "ltdc.h"
#include "usb_otg.h"
#include "gpio.h"
#include "fmc.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "debug_log.h"

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

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MPU_Config(void);
/* USER CODE BEGIN PFP */

static uint32_t SDRAM_SelfTest(void);
static void LCD_ShowStartupPattern(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

#define LCD_FRAMEBUFFER_ADDRESS          0xC0000000UL
#define SDRAM_TEST_ADDRESS               0xC007F000UL
#define LCD_WIDTH                        480U
#define LCD_HEIGHT                       272U

static uint32_t SDRAM_SelfTest(void)
{
  static const uint32_t patterns[] =
  {
    0x00000000UL, 0xFFFFFFFFUL, 0xAAAAAAAAUL, 0x55555555UL,
    0x12345678UL, 0x87654321UL, 0xA5A55A5AUL, 0x5A5AA5A5UL
  };
  volatile uint32_t *memory = (volatile uint32_t *)SDRAM_TEST_ADDRESS;
  uint32_t index;
  uint32_t expected;

  for (index = 0U; index < 64U; index++)
  {
    memory[index] = patterns[index % (sizeof(patterns) / sizeof(patterns[0]))] ^
                    (index * 0x01010101UL);
  }
  __DSB();

  for (index = 0U; index < 64U; index++)
  {
    expected = patterns[index % (sizeof(patterns) / sizeof(patterns[0]))] ^
               (index * 0x01010101UL);
    if (memory[index] != expected)
    {
      Debug_Log_Hex("[SDRAM] failed address: ",
                    SDRAM_TEST_ADDRESS + (index * sizeof(uint32_t)));
      Debug_Log_Hex("[SDRAM] expected: ", expected);
      Debug_Log_Hex("[SDRAM] read:     ", memory[index]);
      return 1U;
    }
  }

  for (index = 0U; index < 64U; index++)
  {
    memory[index] = 0U;
  }
  __DSB();
  Debug_Log_Line("[SDRAM] test passed");
  return 0U;
}

static void LCD_ShowStartupPattern(void)
{
  volatile uint16_t *framebuffer =
      (volatile uint16_t *)LCD_FRAMEBUFFER_ADDRESS;
  HAL_StatusTypeDef address_status;
  HAL_StatusTypeDef reload_status;
  uint32_t x;
  uint32_t y;
  uint16_t color;

  for (y = 0U; y < LCD_HEIGHT; y++)
  {
    for (x = 0U; x < LCD_WIDTH; x++)
    {
      if (x < (LCD_WIDTH / 4U))
      {
        color = 0xF800U; /* Red. */
      }
      else if (x < (LCD_WIDTH / 2U))
      {
        color = 0x07E0U; /* Green. */
      }
      else if (x < ((LCD_WIDTH * 3U) / 4U))
      {
        color = 0x001FU; /* Blue. */
      }
      else
      {
        color = 0xFFFFU; /* White. */
      }
      framebuffer[(y * LCD_WIDTH) + x] = color;
    }
  }
  __DSB();

  address_status = HAL_LTDC_SetAddress(&hltdc, LCD_FRAMEBUFFER_ADDRESS, 0U);
  reload_status = HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_IMMEDIATE);
  HAL_GPIO_WritePin(LCD_DISP_GPIO_Port, LCD_DISP_Pin, GPIO_PIN_SET);
  HAL_Delay(10U);
  HAL_GPIO_WritePin(LCD_BL_CTRL_GPIO_Port, LCD_BL_CTRL_Pin, GPIO_PIN_SET);

  Debug_Log_U32("[LCD] HAL_LTDC_SetAddress: ", (uint32_t)address_status);
  Debug_Log_U32("[LCD] HAL_LTDC_Reload:     ", (uint32_t)reload_status);
  Debug_Log_Hex("[LCD] LTDC GCR:  ", LTDC->GCR);
  Debug_Log_Hex("[LCD] Layer CFBAR: ", LTDC_Layer1->CFBAR);
  Debug_Log_Hex("[LCD] GPIOI ODR: ", GPIOI->ODR);
  Debug_Log_Hex("[LCD] GPIOK ODR: ", GPIOK->ODR);
  Debug_Log_Line("[LCD] startup color bars enabled");
  Debug_Log_Line("[LCD] holding hardware test for 2 seconds");
  HAL_Delay(2000U);
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

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  Debug_Log_Init();
  Debug_Log_SetStage("system clock configured");

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  Debug_Log_SetStage("GPIO init");
  MX_GPIO_Init();
  Debug_Log_SetStage("DMA2D init");
  MX_DMA2D_Init();
  Debug_Log_SetStage("FMC/SDRAM init");
  MX_FMC_Init();
  if (SDRAM_SelfTest() != 0U)
  {
    Debug_Log_SetStage("SDRAM self-test failed");
    Error_Handler();
  }
  Debug_Log_SetStage("I2C1/BME280 bus init");
  MX_I2C1_Init();
  Debug_Log_SetStage("I2C3/touch init");
  MX_I2C3_Init();
  Debug_Log_SetStage("LTDC init");
  MX_LTDC_Init();
  /* USER CODE BEGIN 2 */

  LCD_ShowStartupPattern();

  /* Ethernet initialization does not wait for a cable or an IP address. */
  Debug_Log_SetStage("Ethernet MAC init (cable optional)");
  MX_ETH_Init();

  /* USBX initializes and starts the PCD only after its descriptors and
     vendor class are registered. */

  Debug_Log_SetStage("enter ThreadX kernel");

  /* USER CODE END 2 */

  MX_ThreadX_Init();

  /* We should never get here as control is now taken by the scheduler */

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
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 432;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 9;
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

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_7) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0xC0000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_8MB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.Size = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER2;
  MPU_InitStruct.BaseAddress = 0x20040000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_64KB;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

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
  /* HAL error callbacks can fire from interrupt context, and this path is
     about to halt with interrupts disabled either way, so it deliberately
     uses the raw, unlocked Debug_Log_Write() instead of Debug_Log_Line():
     taking the debug log mutex here could be illegal (ISR) or deadlock on
     a lock a just-interrupted thread still owns. */
  Debug_Log_Write("*** Error_Handler ***\n");
  Debug_Log_Write("last stage: ");
  Debug_Log_Write(Debug_Log_GetStage());
  Debug_Log_Write("\n");
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
