
/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_azure_rtos.c
  * @author  MCD Application Team
  * @brief   azure_rtos application implementation file
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

/* Includes ------------------------------------------------------------------*/

#include "app_azure_rtos.h"
#include "stm32f7xx.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "debug_log.h"

#if defined(__GNUC__)
#define APP_SDRAM_POOL __attribute__((section(".sdram_data"), aligned(32)))
#else
#define APP_SDRAM_POOL
#endif

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define SERVICE_INIT_STACK_SIZE          4096U
#define SERVICE_INIT_PRIORITY            20U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN TX_Pool_Buffer */
/* USER CODE END TX_Pool_Buffer */
#if defined ( __ICCARM__ )
#pragma data_alignment=4
#endif
__ALIGN_BEGIN static UCHAR tx_byte_pool_buffer[TX_APP_MEM_POOL_SIZE] APP_SDRAM_POOL __ALIGN_END;
static TX_BYTE_POOL tx_app_byte_pool;

/* USER CODE BEGIN NX_Pool_Buffer */
/* USER CODE END NX_Pool_Buffer */
#if defined ( __ICCARM__ )
#pragma data_alignment=4
#endif
__ALIGN_BEGIN static UCHAR nx_byte_pool_buffer[NX_APP_MEM_POOL_SIZE] APP_SDRAM_POOL __ALIGN_END;
static TX_BYTE_POOL nx_app_byte_pool;

/* USER CODE BEGIN UX_Device_Pool_Buffer */
/* USER CODE END UX_Device_Pool_Buffer */
#if defined ( __ICCARM__ )
#pragma data_alignment=4
#endif
__ALIGN_BEGIN static UCHAR ux_device_byte_pool_buffer[UX_DEVICE_APP_MEM_POOL_SIZE] APP_SDRAM_POOL __ALIGN_END;
static TX_BYTE_POOL ux_device_app_byte_pool;

/* USER CODE BEGIN PV */

static TX_THREAD netx_init_thread;
static TX_THREAD usbx_init_thread;
static UCHAR *netx_init_stack;
static UCHAR *usbx_init_stack;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

static VOID NetX_Init_Thread(ULONG argument);
static VOID USBX_Init_Thread(ULONG argument);

/* USER CODE END PFP */

/**
  * @brief  Define the initial system.
  * @param  first_unused_memory : Pointer to the first unused memory
  * @retval None
  */
VOID tx_application_define(VOID *first_unused_memory)
{
  /* USER CODE BEGIN  tx_application_define */

  /* USER CODE END  tx_application_define */

  VOID *memory_ptr;
  UINT status;
  UINT tx_pool_ready = 0U;
  UINT nx_pool_ready = 0U;
  UINT ux_pool_ready = 0U;

  (void)first_unused_memory;

  /* Must happen before any other thread can start logging: this is the
     earliest point ThreadX object creation is valid (tx_kernel_enter() has
     just run its low-level init and is calling us back), and it arms the
     debug-log mutex used by every Debug_Log_* call from here on. */
  Debug_Log_ThreadingInit();

  Debug_Log_SetStage("ThreadX application definition");
  Debug_Log_Line("[RTOS] tx_application_define entered");

  status = tx_byte_pool_create(&tx_app_byte_pool, "Tx App memory pool",
                               tx_byte_pool_buffer, TX_APP_MEM_POOL_SIZE);
  if (status != TX_SUCCESS)
  {
    /* USER CODE BEGIN TX_Byte_Pool_Error */

    Debug_Log_U32("[RTOS] TX pool create failed: ", status);
    /* USER CODE END TX_Byte_Pool_Error */
  }
  else
  {
    tx_pool_ready = 1U;
    /* USER CODE BEGIN TX_Byte_Pool_Success */

    /* USER CODE END TX_Byte_Pool_Success */

    memory_ptr = (VOID *)&tx_app_byte_pool;

    Debug_Log_SetStage("GUIX/application init");
    status = App_ThreadX_Init(memory_ptr);
    if (status != TX_SUCCESS)
    {
      /* USER CODE BEGIN  App_ThreadX_Init_Error */

      Debug_Log_U32("[RTOS] application init failed: ", status);
      /* USER CODE END  App_ThreadX_Init_Error */
    }
    else
    {
      Debug_Log_Line("[RTOS] application/GUI init passed");
    }

    /* USER CODE BEGIN  App_ThreadX_Init_Success */

    /* USER CODE END  App_ThreadX_Init_Success */

  }

  status = tx_byte_pool_create(&nx_app_byte_pool, "Nx App memory pool",
                               nx_byte_pool_buffer, NX_APP_MEM_POOL_SIZE);
  if (status != TX_SUCCESS)
  {
    /* USER CODE BEGIN NX_Byte_Pool_Error */

    Debug_Log_U32("[NETX] pool create failed: ", status);
    /* USER CODE END NX_Byte_Pool_Error */
  }
  else
  {
    nx_pool_ready = 1U;
    /* USER CODE BEGIN TX_Byte_Pool_Success */

    /* USER CODE END TX_Byte_Pool_Success */

    /* USER CODE BEGIN MX_NetXDuo_Init_Success */

    /* USER CODE END MX_NetXDuo_Init_Success */

  }

  status = tx_byte_pool_create(&ux_device_app_byte_pool, "Ux App memory pool",
                               ux_device_byte_pool_buffer,
                               UX_DEVICE_APP_MEM_POOL_SIZE);
  if (status != TX_SUCCESS)
  {
    /* USER CODE BEGIN UX_Device_Byte_Pool_Error */

    Debug_Log_U32("[USBX] pool create failed: ", status);
    /* USER CODE END UX_Device_Byte_Pool_Error */
  }
  else
  {
    ux_pool_ready = 1U;
    /* USER CODE BEGIN UX_Device_Byte_Pool_Success */

    /* USER CODE END UX_Device_Byte_Pool_Success */

    /* USER CODE BEGIN MX_USBX_Device_Init_Success */

    /* USER CODE END MX_USBX_Device_Init_Success */
  }

  /* Hardware-facing middleware is initialized only after the scheduler starts.
     Both threads use the same low priority and a time slice, so a stalled
     optional service cannot prevent GUIX or the other service from running. */
  if ((tx_pool_ready != 0U) && (nx_pool_ready != 0U) &&
      (tx_byte_allocate(&tx_app_byte_pool, (VOID **)&netx_init_stack,
                        SERVICE_INIT_STACK_SIZE, TX_NO_WAIT) == TX_SUCCESS) &&
      (tx_thread_create(&netx_init_thread, "NetX init", NetX_Init_Thread, 0U,
                        netx_init_stack, SERVICE_INIT_STACK_SIZE,
                        SERVICE_INIT_PRIORITY, SERVICE_INIT_PRIORITY,
                        5U, TX_AUTO_START) == TX_SUCCESS))
  {
    Debug_Log_Line("[NETX] post-scheduler init thread created");
  }
  else
  {
    Debug_Log_Line("[NETX] init thread creation failed");
  }

  if ((tx_pool_ready != 0U) && (ux_pool_ready != 0U) &&
      (tx_byte_allocate(&tx_app_byte_pool, (VOID **)&usbx_init_stack,
                        SERVICE_INIT_STACK_SIZE, TX_NO_WAIT) == TX_SUCCESS) &&
      (tx_thread_create(&usbx_init_thread, "USBX init", USBX_Init_Thread, 0U,
                        usbx_init_stack, SERVICE_INIT_STACK_SIZE,
                        SERVICE_INIT_PRIORITY, SERVICE_INIT_PRIORITY,
                        5U, TX_AUTO_START) == TX_SUCCESS))
  {
    Debug_Log_Line("[USBX] post-scheduler init thread created");
  }
  else
  {
    Debug_Log_Line("[USBX] init thread creation failed");
  }

  Debug_Log_SetStage("ThreadX scheduler handoff");
  Debug_Log_Line("[RTOS] tx_application_define complete");
}

/* USER CODE BEGIN  0 */

static VOID NetX_Init_Thread(ULONG argument)
{
  UINT status;

  (void)argument;
  Debug_Log_SetStage("NetX Duo init after scheduler");
  Debug_Log_Line("[NETX] initialization started");
  status = MX_NetXDuo_Init((VOID *)&nx_app_byte_pool);
  if (status == NX_SUCCESS)
  {
    Debug_Log_Line("[NETX] initialized; cable is optional");
  }
  else
  {
    Debug_Log_U32("[NETX] initialization failed: ", status);
  }
}

static VOID USBX_Init_Thread(ULONG argument)
{
  UINT status;

  (void)argument;
  Debug_Log_SetStage("USBX device init after scheduler");
  Debug_Log_Line("[USBX] initialization started");
  status = MX_USBX_Device_Init((VOID *)&ux_device_app_byte_pool);
  if (status == UX_SUCCESS)
  {
    Debug_Log_Line("[USBX] device initialized");
  }
  else
  {
    Debug_Log_U32("[USBX] initialization failed: ", status);
  }
}

/* USER CODE END  0 */
