/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_threadx.h
  * @author  MCD Application Team
  * @brief   ThreadX applicative header file
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2021 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __APP_THREADX_H__
#define __APP_THREADX_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "tx_api.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "app_diagnostics_config.h"

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

#if (APP_DIAGNOSTICS_ENABLE != 0U)
#define APP_DIAGNOSTICS_MAX_THREADS 16U
#define APP_DIAGNOSTICS_MAX_POOLS   4U

typedef struct
{
  const CHAR *name;
  UINT state;
  UINT priority;
  ULONG run_count;
  ULONG stack_total;
  ULONG stack_used;
  ULONG stack_free;
} APP_DIAGNOSTICS_THREAD;

typedef struct
{
  const CHAR *name;
  ULONG total_bytes;
  ULONG free_bytes;
  ULONG fragments;
} APP_DIAGNOSTICS_POOL;

typedef struct
{
  ULONG uptime_seconds;
  ULONG flash_total;
  ULONG flash_used;
  ULONG flash_free;
  ULONG axi_ram_total;
  ULONG axi_ram_used;
  ULONG axi_ram_free;
  ULONG sdram_total;
  ULONG sdram_reserved;
  ULONG sdram_unassigned;
  ULONG heap_total;
  ULONG heap_free;
  ULONG stack_total;
  ULONG stack_free;
  UINT thread_count;
  UINT pool_count;
  APP_DIAGNOSTICS_THREAD threads[APP_DIAGNOSTICS_MAX_THREADS];
  APP_DIAGNOSTICS_POOL pools[APP_DIAGNOSTICS_MAX_POOLS];
} APP_DIAGNOSTICS_SNAPSHOT;
#endif

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
UINT App_ThreadX_Init(VOID *memory_ptr);
void MX_ThreadX_Init(void);
/* USER CODE BEGIN EFP */

#if (APP_DIAGNOSTICS_ENABLE != 0U)
UINT App_Diagnostics_Snapshot_Get(APP_DIAGNOSTICS_SNAPSHOT *snapshot);
#endif

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

#ifdef __cplusplus
}
#endif
#endif /* __APP_THREADX_H__ */
