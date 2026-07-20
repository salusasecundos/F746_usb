/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_threadx.c
  * @author  MCD Application Team
  * @brief   ThreadX applicative file
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
#include "app_threadx.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "app_guix.h"
#include "app_bme280.h"
#include "app_state.h"
#include "app_usbx_device.h"
#include "debug_log.h"
#include "main.h"

#if (APP_DIAGNOSTICS_ENABLE != 0U)
#include "tx_byte_pool.h"
#include "tx_thread.h"
#include <stdint.h>
#endif

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#if (APP_DIAGNOSTICS_UART_ENABLE != 0U)
#define DEBUG_HEARTBEAT_STACK_SIZE 2048U
#else
#define DEBUG_HEARTBEAT_STACK_SIZE 1024U
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

static TX_THREAD debug_heartbeat_thread;
static UCHAR *debug_heartbeat_stack;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

static VOID Debug_Heartbeat_Thread(ULONG argument);
#if (APP_DIAGNOSTICS_UART_ENABLE != 0U)
static VOID App_Diagnostics_Log(VOID);
#endif

/* USER CODE END PFP */

/**
  * @brief  Application ThreadX Initialization.
  * @param memory_ptr: memory pointer
  * @retval int
  */
UINT App_ThreadX_Init(VOID *memory_ptr)
{
  UINT ret = TX_SUCCESS;
  TX_BYTE_POOL *byte_pool = (TX_BYTE_POOL*)memory_ptr;

  /* USER CODE BEGIN App_ThreadX_MEM_POOL */

  if (byte_pool == TX_NULL)
  {
    return TX_PTR_ERROR;
  }

  /* USER CODE END App_ThreadX_MEM_POOL */

  /* USER CODE BEGIN App_ThreadX_Init */

  Debug_Log_Line("[RTOS] App_ThreadX_Init started");
  App_State_Init();

  if (App_BME280_Init(byte_pool) != TX_SUCCESS)
  {
    /* Sensor support is optional: GUI, USB and LAN must still start. */
    Debug_Log_Line("[RTOS] BME280 thread initialization failed");
  }

  if ((tx_byte_allocate(byte_pool, (VOID **)&debug_heartbeat_stack,
                        DEBUG_HEARTBEAT_STACK_SIZE, TX_NO_WAIT) == TX_SUCCESS) &&
      (tx_thread_create(&debug_heartbeat_thread, "Debug heartbeat",
                        Debug_Heartbeat_Thread, 0U,
                        debug_heartbeat_stack, DEBUG_HEARTBEAT_STACK_SIZE,
                        30U, 30U, TX_NO_TIME_SLICE,
                        TX_AUTO_START) == TX_SUCCESS))
  {
    Debug_Log_Line("[RTOS] heartbeat thread created");
  }
  else
  {
    Debug_Log_Line("[RTOS] heartbeat thread creation failed");
  }

  ret = App_GUIX_Init(byte_pool);
  Debug_Log_U32("[RTOS] App_GUIX_Init returned: ", ret);

  /* USER CODE END App_ThreadX_Init */

  return ret;
}

/**
  * @brief  MX_ThreadX_Init
  * @param  None
  * @retval None
  */
void MX_ThreadX_Init(void)
{
  /* USER CODE BEGIN  Before_Kernel_Start */

  /* USER CODE END  Before_Kernel_Start */

  tx_kernel_enter();

  /* USER CODE BEGIN  Kernel_Start_Error */

  /* USER CODE END  Kernel_Start_Error */
}

/* USER CODE BEGIN 1 */

static VOID Debug_Heartbeat_Thread(ULONG argument)
{
  ULONG last_heartbeat;
#if (APP_DIAGNOSTICS_UART_ENABLE != 0U)
  ULONG last_diagnostics;
#endif
  ULONG now;
  ULONG service_period;

  (void)argument;

  Debug_Log_Line("[RTOS] scheduler running");
  last_heartbeat = tx_time_get();
#if (APP_DIAGNOSTICS_UART_ENABLE != 0U)
  last_diagnostics = last_heartbeat;
#endif
  service_period = TX_TIMER_TICKS_PER_SECOND / 20U;
  if (service_period == 0U)
  {
    service_period = 1U;
  }

  for (;;)
  {
    tx_thread_sleep(service_period);
    App_USBX_Device_Service();

    now = tx_time_get();
    if ((now - last_heartbeat) >= (5U * TX_TIMER_TICKS_PER_SECOND))
    {
      last_heartbeat = now;
      Debug_Log_U32("[RTOS] alive, HAL tick ms: ", HAL_GetTick());
    }

#if (APP_DIAGNOSTICS_UART_ENABLE != 0U)
    if ((now - last_diagnostics) >=
        (APP_DIAGNOSTICS_UART_PERIOD_SECONDS * TX_TIMER_TICKS_PER_SECOND))
    {
      last_diagnostics = now;
      App_Diagnostics_Log();
    }
#endif
  }
}

#if (APP_DIAGNOSTICS_ENABLE != 0U)

extern uint8_t _sidata;
extern uint8_t _sdata;
extern uint8_t _edata;
extern uint8_t _sbss;
extern uint8_t _ebss;
extern uint8_t __framebuffer_start__;
extern uint8_t __framebuffer_end__;
extern uint8_t __sdram_data_start__;
extern uint8_t __sdram_data_end__;

UINT App_Diagnostics_Snapshot_Get(APP_DIAGNOSTICS_SNAPSHOT *snapshot)
{
  TX_THREAD *thread_ptr;
  TX_THREAD *next_thread;
  TX_THREAD *next_suspended;
  TX_THREAD *first_suspended;
  TX_BYTE_POOL *pool_ptr;
  TX_BYTE_POOL *next_pool;
  CHAR *name;
  ULONG created_count;
  ULONG available;
  ULONG fragments;
  ULONG suspended_count;
  ULONG run_count;
  ULONG time_slice;
  UINT state;
  UINT priority;
  UINT preemption_threshold;

  if (snapshot == TX_NULL)
  {
    return TX_PTR_ERROR;
  }

  *snapshot = (APP_DIAGNOSTICS_SNAPSHOT){0};
  snapshot->uptime_seconds = tx_time_get() / TX_TIMER_TICKS_PER_SECOND;
  snapshot->flash_total = 1024U * 1024U;
  snapshot->flash_used =
      (ULONG)((uintptr_t)&_sidata - 0x08000000UL) +
      (ULONG)((uintptr_t)&_edata - (uintptr_t)&_sdata);
  if (snapshot->flash_used <= snapshot->flash_total)
  {
    snapshot->flash_free = snapshot->flash_total - snapshot->flash_used;
  }

  snapshot->axi_ram_total = 192U * 1024U;
  snapshot->axi_ram_used =
      (ULONG)((uintptr_t)&_edata - (uintptr_t)&_sdata) +
      (ULONG)((uintptr_t)&_ebss - (uintptr_t)&_sbss);
  if (snapshot->axi_ram_used <= snapshot->axi_ram_total)
  {
    snapshot->axi_ram_free = snapshot->axi_ram_total - snapshot->axi_ram_used;
  }

  snapshot->sdram_total = 8U * 1024U * 1024U;
  snapshot->sdram_reserved =
      (ULONG)((uintptr_t)&__framebuffer_end__ -
              (uintptr_t)&__framebuffer_start__) +
      (ULONG)((uintptr_t)&__sdram_data_end__ -
              (uintptr_t)&__sdram_data_start__);
  if (snapshot->sdram_reserved <= snapshot->sdram_total)
  {
    snapshot->sdram_unassigned =
        snapshot->sdram_total - snapshot->sdram_reserved;
  }

  pool_ptr = _tx_byte_pool_created_ptr;
  created_count = _tx_byte_pool_created_count;
  while ((pool_ptr != TX_NULL) &&
         (snapshot->pool_count < APP_DIAGNOSTICS_MAX_POOLS) &&
         (created_count != 0U))
  {
    if (tx_byte_pool_info_get(pool_ptr, &name, &available, &fragments,
                              &first_suspended, &suspended_count,
                              &next_pool) != TX_SUCCESS)
    {
      break;
    }

    APP_DIAGNOSTICS_POOL *pool = &snapshot->pools[snapshot->pool_count++];
    pool->name = name;
    pool->total_bytes = pool_ptr->tx_byte_pool_size;
    pool->free_bytes = available;
    pool->fragments = fragments;
    snapshot->heap_total += pool->total_bytes;
    snapshot->heap_free += pool->free_bytes;
    pool_ptr = next_pool;
    created_count--;
  }

  thread_ptr = _tx_thread_created_ptr;
  created_count = _tx_thread_created_count;
  while ((thread_ptr != TX_NULL) &&
         (snapshot->thread_count < APP_DIAGNOSTICS_MAX_THREADS) &&
         (created_count != 0U))
  {
    if (tx_thread_info_get(thread_ptr, &name, &state, &run_count, &priority,
                           &preemption_threshold, &time_slice, &next_thread,
                           &next_suspended) != TX_SUCCESS)
    {
      break;
    }

    _tx_thread_stack_analyze(thread_ptr);
    APP_DIAGNOSTICS_THREAD *thread =
        &snapshot->threads[snapshot->thread_count++];
    thread->name = name;
    thread->state = state;
    thread->priority = priority;
    thread->run_count = run_count;
    thread->stack_total = thread_ptr->tx_thread_stack_size;

    uintptr_t stack_start = (uintptr_t)thread_ptr->tx_thread_stack_start;
    uintptr_t stack_high = (uintptr_t)thread_ptr->tx_thread_stack_highest_ptr;
    uintptr_t stack_end = stack_start + thread->stack_total;
    if ((stack_high >= stack_start) && (stack_high <= stack_end))
    {
      thread->stack_free = (ULONG)(stack_high - stack_start);
      thread->stack_used = thread->stack_total - thread->stack_free;
    }

    snapshot->stack_total += thread->stack_total;
    snapshot->stack_free += thread->stack_free;
    thread_ptr = next_thread;
    created_count--;
  }

  return TX_SUCCESS;
}

#endif /* APP_DIAGNOSTICS_ENABLE */

#if (APP_DIAGNOSTICS_UART_ENABLE != 0U)

static UINT App_Diagnostics_Append_Text(char *buffer, UINT position,
                                        UINT capacity, const char *text)
{
  while ((*text != '\0') && ((position + 1U) < capacity))
  {
    buffer[position++] = *text++;
  }
  buffer[position] = '\0';
  return position;
}

static UINT App_Diagnostics_Append_U32(char *buffer, UINT position,
                                       UINT capacity, ULONG value)
{
  char reverse[10];
  UINT count = 0U;

  do
  {
    reverse[count++] = (char)('0' + (value % 10U));
    value /= 10U;
  } while ((value != 0U) && (count < sizeof(reverse)));

  while ((count != 0U) && ((position + 1U) < capacity))
  {
    buffer[position++] = reverse[--count];
  }
  buffer[position] = '\0';
  return position;
}

static VOID App_Diagnostics_Log_Pair(const char *label, ULONG first,
                                     ULONG second)
{
  char line[96];
  UINT position = 0U;

  line[0] = '\0';
  position = App_Diagnostics_Append_Text(line, position, sizeof(line), label);
  position = App_Diagnostics_Append_U32(line, position, sizeof(line), first);
  position = App_Diagnostics_Append_Text(line, position, sizeof(line), "/");
  (void)App_Diagnostics_Append_U32(line, position, sizeof(line), second);
  Debug_Log_Line(line);
}

static VOID App_Diagnostics_Log(VOID)
{
  APP_DIAGNOSTICS_SNAPSHOT snapshot;
  char line[96];

  if (App_Diagnostics_Snapshot_Get(&snapshot) != TX_SUCCESS)
  {
    return;
  }

  Debug_Log_Line("[MEM] ===== diagnostics =====");
  App_Diagnostics_Log_Pair("[MEM] flash used/total: ",
                           snapshot.flash_used, snapshot.flash_total);
  App_Diagnostics_Log_Pair("[MEM] AXI RAM used/total: ",
                           snapshot.axi_ram_used, snapshot.axi_ram_total);
  App_Diagnostics_Log_Pair("[MEM] heap free/total: ",
                           snapshot.heap_free, snapshot.heap_total);
  App_Diagnostics_Log_Pair("[MEM] stacks free/total: ",
                           snapshot.stack_free, snapshot.stack_total);
  App_Diagnostics_Log_Pair("[MEM] SDRAM unassigned/total: ",
                           snapshot.sdram_unassigned, snapshot.sdram_total);

  for (UINT index = 0U; index < snapshot.pool_count; index++)
  {
    UINT position = 0U;
    APP_DIAGNOSTICS_POOL *pool = &snapshot.pools[index];
    line[0] = '\0';
    position = App_Diagnostics_Append_Text(line, position, sizeof(line),
                                           "[MEM] pool ");
    position = App_Diagnostics_Append_Text(line, position, sizeof(line),
                                           pool->name);
    position = App_Diagnostics_Append_Text(line, position, sizeof(line),
                                           " free/total/fragments ");
    position = App_Diagnostics_Append_U32(line, position, sizeof(line),
                                          pool->free_bytes);
    position = App_Diagnostics_Append_Text(line, position, sizeof(line), "/");
    position = App_Diagnostics_Append_U32(line, position, sizeof(line),
                                          pool->total_bytes);
    position = App_Diagnostics_Append_Text(line, position, sizeof(line), "/");
    (void)App_Diagnostics_Append_U32(line, position, sizeof(line),
                                     pool->fragments);
    Debug_Log_Line(line);
  }

  for (UINT index = 0U; index < snapshot.thread_count; index++)
  {
    UINT position = 0U;
    APP_DIAGNOSTICS_THREAD *thread = &snapshot.threads[index];
    line[0] = '\0';
    position = App_Diagnostics_Append_Text(line, position, sizeof(line),
                                           "[MEM] task ");
    position = App_Diagnostics_Append_Text(line, position, sizeof(line),
                                           thread->name);
    position = App_Diagnostics_Append_Text(line, position, sizeof(line),
                                           " state/prio/run/stack-free ");
    position = App_Diagnostics_Append_U32(line, position, sizeof(line),
                                          thread->state);
    position = App_Diagnostics_Append_Text(line, position, sizeof(line), "/");
    position = App_Diagnostics_Append_U32(line, position, sizeof(line),
                                          thread->priority);
    position = App_Diagnostics_Append_Text(line, position, sizeof(line), "/");
    position = App_Diagnostics_Append_U32(line, position, sizeof(line),
                                          thread->run_count);
    position = App_Diagnostics_Append_Text(line, position, sizeof(line), "/");
    (void)App_Diagnostics_Append_U32(line, position, sizeof(line),
                                     thread->stack_free);
    Debug_Log_Line(line);
  }
}

#endif /* APP_DIAGNOSTICS_UART_ENABLE */

/* USER CODE END 1 */
