/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_usbx_device.c
  * @author  MCD Application Team
  * @brief   USBX Device applicative file
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
#include "app_usbx_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "usb_otg.h"
#include "app_state.h"
#include "debug_log.h"
#include "ux_dcd_stm32.h"
#include "ux_device_bulk.h"
#include "ux_device_stack.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define USBX_MEMORY_SIZE                 (24U * 1024U)
#define USBX_VBUS_DEBOUNCE_MS            75U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

static UCHAR device_framework_full_speed[] =
{
  /* Device descriptor: VID 0483, PID 5752. */
  0x12U, 0x01U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x40U,
  0x83U, 0x04U, 0x52U, 0x57U, 0x00U, 0x02U, 0x01U, 0x02U,
  0x03U, 0x01U,

  /* One vendor-specific interface with two Bulk endpoints. */
  0x09U, 0x02U, 0x20U, 0x00U, 0x01U, 0x01U, 0x00U, 0xC0U,
  0x32U,
  0x09U, 0x04U, 0x00U, 0x00U, 0x02U, 0xFFU, 0x00U, 0x00U,
  0x04U,
  0x07U, 0x05U, USB_BULK_OUT_ENDPOINT, 0x02U, 0x40U, 0x00U, 0x00U,
  0x07U, 0x05U, USB_BULK_IN_ENDPOINT,  0x02U, 0x40U, 0x00U, 0x00U
};

/* The selected peripheral is Full-Speed; USBX still expects both frameworks. */
static UCHAR device_framework_high_speed[] =
{
  0x12U, 0x01U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x40U,
  0x83U, 0x04U, 0x52U, 0x57U, 0x00U, 0x02U, 0x01U, 0x02U,
  0x03U, 0x01U,
  0x09U, 0x02U, 0x20U, 0x00U, 0x01U, 0x01U, 0x00U, 0xC0U,
  0x32U,
  0x09U, 0x04U, 0x00U, 0x00U, 0x02U, 0xFFU, 0x00U, 0x00U,
  0x04U,
  0x07U, 0x05U, USB_BULK_OUT_ENDPOINT, 0x02U, 0x40U, 0x00U, 0x00U,
  0x07U, 0x05U, USB_BULK_IN_ENDPOINT,  0x02U, 0x40U, 0x00U, 0x00U
};

/* USBX string entries: LANGID, index, ASCII length, ASCII bytes. */
static UCHAR string_framework[] =
{
  0x09U, 0x04U, 0x01U, 18U,
  'S','T','M','i','c','r','o','e','l','e','c','t','r','o','n','i','c','s',
  0x09U, 0x04U, 0x02U, 17U,
  'S','T','M','3','2',' ','B','u','l','k',' ','D','e','v','i','c','e',
  0x09U, 0x04U, 0x03U, 8U,
  'F','7','4','6','0','0','0','1',
  0x09U, 0x04U, 0x04U, 21U,
  'V','e','n','d','o','r',' ','B','u','l','k',' ','I','n','t','e','r','f','a','c','e',
  /* Microsoft OS 1.0 string: "MSFT100", vendor request 0x20. */
  0x09U, 0x04U, 0xEEU, 8U,
  'M','S','F','T','1','0','0', USB_BULK_MS_VENDOR_CODE
};

static UCHAR language_id_framework[] = {0x09U, 0x04U};
static UCHAR bulk_class_name[] = "ux_device_class_vendor_bulk";
static volatile UINT usbx_device_started;
static UINT usbx_diag_state_known;
static UCHAR usbx_diag_last_state;
static uint8_t usbx_diag_last_vbus;
static uint8_t usbx_vbus_candidate;
static uint8_t usbx_vbus_stable;
static uint8_t usbx_soft_connected;
static uint32_t usbx_vbus_candidate_since;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static UINT App_USBX_Device_Change(ULONG event);
static VOID App_USBX_Set_Serial(VOID);
static uint32_t App_USBX_Get_Clk48_Frequency(VOID);

/* USER CODE END PFP */
/**
  * @brief  Application USBX Device Initialization.
  * @param memory_ptr: memory pointer
  * @retval int
  */
UINT MX_USBX_Device_Init(VOID *memory_ptr)
{
  UINT ret = UX_SUCCESS;
  TX_BYTE_POOL *byte_pool = (TX_BYTE_POOL*)memory_ptr;

  /* USER CODE BEGIN MX_USBX_Device_MEM_POOL */

  VOID *usbx_memory = UX_NULL;
  Debug_Log_SetStage("USBX memory allocation");
  if (byte_pool == TX_NULL)
  {
    Debug_Log_Line("[USBX] null byte pool");
    return UX_ERROR;
  }
  if (tx_byte_allocate(byte_pool, &usbx_memory, USBX_MEMORY_SIZE,
                       TX_NO_WAIT) != TX_SUCCESS)
  {
    Debug_Log_Line("[USBX] memory allocation failed");
    return UX_MEMORY_INSUFFICIENT;
  }
  Debug_Log_Line("[USBX] memory allocated");

  /* USER CODE END MX_USBX_Device_MEM_POOL */

  /* USER CODE BEGIN MX_USBX_Device_Init */

  App_USBX_Set_Serial();

  Debug_Log_SetStage("ux_system_initialize");
  ret = ux_system_initialize(usbx_memory, USBX_MEMORY_SIZE, UX_NULL, 0U);
  if (ret != UX_SUCCESS)
  {
    Debug_Log_U32("[USBX] ux_system_initialize failed: ", ret);
    return ret;
  }
  Debug_Log_Line("[USBX] system initialized");

  Debug_Log_SetStage("ux_device_stack_initialize");
  ret = ux_device_stack_initialize(device_framework_high_speed,
                                   sizeof(device_framework_high_speed),
                                   device_framework_full_speed,
                                   sizeof(device_framework_full_speed),
                                   string_framework, sizeof(string_framework),
                                   language_id_framework,
                                   sizeof(language_id_framework),
                                   App_USBX_Device_Change);
  if (ret != UX_SUCCESS)
  {
    Debug_Log_U32("[USBX] device stack init failed: ", ret);
    return ret;
  }
  Debug_Log_Line("[USBX] device stack initialized");

  Debug_Log_SetStage("USBX Microsoft descriptor registration");
  ret = _ux_device_stack_microsoft_extension_register(
      USB_BULK_MS_VENDOR_CODE, App_USBX_Bulk_Microsoft_Request);
  if (ret != UX_SUCCESS)
  {
    Debug_Log_U32("[USBX] Microsoft descriptor register failed: ", ret);
    return ret;
  }
  Debug_Log_Line("[USBX] Microsoft descriptors registered");

  Debug_Log_SetStage("USBX Bulk class registration");
  ret = ux_device_stack_class_register(bulk_class_name,
                                       App_USBX_Bulk_Class_Entry,
                                       1U, 0U, UX_NULL);
  if (ret != UX_SUCCESS)
  {
    Debug_Log_U32("[USBX] Bulk class register failed: ", ret);
    return ret;
  }
  Debug_Log_Line("[USBX] Bulk class registered");

  Debug_Log_SetStage("USBX Bulk thread creation");
  ret = App_USBX_Bulk_Thread_Create(byte_pool);
  if (ret != UX_SUCCESS)
  {
    Debug_Log_U32("[USBX] Bulk thread create failed: ", ret);
    return ret;
  }
  Debug_Log_Line("[USBX] Bulk thread created");

  /* USBX DCD software must exist before HAL starts issuing PCD callbacks. */
  Debug_Log_SetStage("USBX STM32 DCD initialization");
  ret = ux_dcd_stm32_initialize((ULONG)USB_OTG_FS,
                                (ULONG)&hpcd_USB_OTG_FS);
  if (ret != UX_SUCCESS)
  {
    Debug_Log_U32("[USBX] STM32 DCD init failed: ", ret);
    return ret;
  }
  Debug_Log_Line("[USBX] STM32 DCD initialized");

  Debug_Log_SetStage("USB OTG FS HAL PCD initialization");
  MX_USB_OTG_FS_PCD_Init();
  Debug_Log_Line("[USBX] HAL PCD initialized");

  Debug_Log_SetStage("USB OTG FS FIFO configuration");
  if ((HAL_PCDEx_SetRxFiFo(&hpcd_USB_OTG_FS, 0x80U) != HAL_OK) ||
      (HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 0U, 0x40U) != HAL_OK) ||
      (HAL_PCDEx_SetTxFiFo(&hpcd_USB_OTG_FS, 1U, 0x80U) != HAL_OK))
  {
    Debug_Log_Line("[USBX] FIFO configuration failed");
    return UX_ERROR;
  }
  Debug_Log_Line("[USBX] FIFOs configured");

  Debug_Log_SetStage("USB OTG FS PCD start");
  if (HAL_PCD_Start(&hpcd_USB_OTG_FS) != HAL_OK)
  {
    Debug_Log_Line("[USBX] HAL_PCD_Start failed");
    return UX_ERROR;
  }
  Debug_Log_Line("[USBX] HAL PCD started");
  Debug_Log_Line("[USBX] device-only VBUS override enabled");

  Debug_Log_U32("[USBX] CLK48 Hz: ", App_USBX_Get_Clk48_Frequency());
  Debug_Log_Hex("[USBX] PJ IDR:   ", GPIOJ->IDR);
  Debug_Log_Hex("[USBX] GOTGCTL:  ", USB_OTG_FS->GOTGCTL);
  Debug_Log_Hex("[USBX] GCCFG:    ", USB_OTG_FS->GCCFG);
  Debug_Log_Hex("[USBX] GUSBCFG:  ", USB_OTG_FS->GUSBCFG);
  Debug_Log_Hex("[USBX] GINTMSK:  ", USB_OTG_FS->GINTMSK);
  Debug_Log_Hex("[USBX] GINTSTS:  ", USB_OTG_FS->GINTSTS);
  {
    uint32_t USBx_BASE = (uint32_t)USB_OTG_FS;
    Debug_Log_Hex("[USBX] DCFG:     ", USBx_DEVICE->DCFG);
    Debug_Log_Hex("[USBX] DCTL:     ", USBx_DEVICE->DCTL);
    Debug_Log_Hex("[USBX] DSTS:     ", USBx_DEVICE->DSTS);
  }

  usbx_device_started = 1U;
  usbx_vbus_candidate =
      (HAL_GPIO_ReadPin(USB_FS_VBUS_MONITOR_GPIO_Port,
                        USB_FS_VBUS_MONITOR_Pin) == GPIO_PIN_SET) ? 1U : 0U;
  usbx_vbus_stable = usbx_vbus_candidate;
  usbx_vbus_candidate_since = HAL_GetTick();
  usbx_soft_connected = 1U;
  App_State_SetUsbCable(usbx_vbus_stable);

  if (usbx_vbus_stable == 0U)
  {
    (void)HAL_PCD_DevDisconnect(&hpcd_USB_OTG_FS);
    usbx_soft_connected = 0U;
  }
  App_USBX_Device_Diagnostics();

  /* USER CODE END MX_USBX_Device_Init */

  return ret;
}

/* USER CODE BEGIN 1 */

VOID App_USBX_Device_Service(VOID)
{
  uint8_t vbus;
  uint32_t now;

  if ((usbx_device_started == 0U) || (_ux_system_slave == UX_NULL))
  {
    return;
  }

  now = HAL_GetTick();
  vbus = (HAL_GPIO_ReadPin(USB_FS_VBUS_MONITOR_GPIO_Port,
                           USB_FS_VBUS_MONITOR_Pin) == GPIO_PIN_SET) ? 1U : 0U;

  if (vbus != usbx_vbus_candidate)
  {
    usbx_vbus_candidate = vbus;
    usbx_vbus_candidate_since = now;
  }
  else if ((vbus != usbx_vbus_stable) &&
           ((now - usbx_vbus_candidate_since) >= USBX_VBUS_DEBOUNCE_MS))
  {
    usbx_vbus_stable = vbus;

    if (vbus == 0U)
    {
      Debug_Log_Line("[USBX] cable removed; resetting device session");

      App_USBX_Bulk_Force_Disconnect();
      if (_ux_system_slave->ux_system_slave_device.ux_slave_device_state !=
          UX_DEVICE_RESET)
      {
        (void)ux_device_stack_disconnect();
      }
      if (usbx_soft_connected != 0U)
      {
        (void)HAL_PCD_DevDisconnect(&hpcd_USB_OTG_FS);
        usbx_soft_connected = 0U;
      }
      App_State_SetUsbConfigured(0U);
      App_State_SetUsbCable(0U);
    }
    else
    {
      Debug_Log_Line("[USBX] cable inserted; reconnecting device");

      App_State_SetUsbCable(1U);
      if (usbx_soft_connected == 0U)
      {
        (void)HAL_PCD_DevConnect(&hpcd_USB_OTG_FS);
        usbx_soft_connected = 1U;
      }
    }
  }

  App_USBX_Device_Diagnostics();
}

VOID App_USBX_Device_Diagnostics(VOID)
{
  UCHAR device_state;
  uint8_t vbus;

  if ((usbx_device_started == 0U) || (_ux_system_slave == UX_NULL))
  {
    return;
  }

  vbus = (HAL_GPIO_ReadPin(USB_FS_VBUS_MONITOR_GPIO_Port,
                           USB_FS_VBUS_MONITOR_Pin) == GPIO_PIN_SET) ? 1U : 0U;
  device_state = _ux_system_slave->ux_system_slave_device.ux_slave_device_state;

  if ((usbx_diag_state_known == 0U) || (vbus != usbx_diag_last_vbus))
  {
    Debug_Log_U32("[USBX] VBUS present: ", vbus);
    usbx_diag_last_vbus = vbus;
  }

  if ((usbx_diag_state_known == 0U) || (device_state != usbx_diag_last_state))
  {
    Debug_Log_U32("[USBX] device state: ", device_state);
    usbx_diag_last_state = device_state;
  }

  usbx_diag_state_known = 1U;
}

static UINT App_USBX_Device_Change(ULONG event)
{
  if (event == UX_DEVICE_ATTACHED)
  {
    App_State_SetUsbCable(1U);
  }
  else if (event == UX_DEVICE_REMOVED)
  {
    App_State_SetUsbConfigured(0U);
    App_State_SetUsbCable(0U);
  }
  return UX_SUCCESS;
}

static uint32_t App_USBX_Get_Clk48_Frequency(VOID)
{
  uint32_t pll_m;
  uint32_t pll_n;
  uint32_t pll_q;
  uint32_t pll_source;

  /* The F746 HAL getter implements only SAI1/SAI2 and returns zero for
     RCC_PERIPHCLK_CLK48. This project selects main PLL Q as CLK48. */
  if (__HAL_RCC_GET_CLK48_SOURCE() != RCC_CLK48SOURCE_PLL)
  {
    return 0U;
  }

  pll_m = RCC->PLLCFGR & RCC_PLLCFGR_PLLM;
  pll_n = (RCC->PLLCFGR & RCC_PLLCFGR_PLLN) >> RCC_PLLCFGR_PLLN_Pos;
  pll_q = (RCC->PLLCFGR & RCC_PLLCFGR_PLLQ) >> RCC_PLLCFGR_PLLQ_Pos;
  pll_source = ((RCC->PLLCFGR & RCC_PLLCFGR_PLLSRC) != 0U) ?
               HSE_VALUE : HSI_VALUE;

  if ((pll_m == 0U) || (pll_q == 0U))
  {
    return 0U;
  }

  return (uint32_t)(((uint64_t)pll_source * pll_n) /
                    ((uint64_t)pll_m * pll_q));
}

static VOID App_USBX_Set_Serial(VOID)
{
  static const UCHAR hex[] = "0123456789ABCDEF";
  ULONG uid = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
  const ULONG serial_offset = 47U;

  for (ULONG index = 0U; index < 8U; index++)
  {
    ULONG shift = (7U - index) * 4U;
    string_framework[serial_offset + index] = hex[(uid >> shift) & 0x0FU];
  }
}

/* USER CODE END 1 */
