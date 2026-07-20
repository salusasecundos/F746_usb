#include "debug_log.h"

#include "main.h"

#define DEBUG_BAUD_RATE                 115200U
#define DEBUG_TX_TIMEOUT                1000000U

static uint8_t debug_ready;
static const char *debug_stage = "before debug init";

static void debug_put_char(char character)
{
  uint32_t timeout = DEBUG_TX_TIMEOUT;

  if (debug_ready == 0U)
  {
    return;
  }

  while (((USART6->ISR & USART_ISR_TXE) == 0U) && (timeout != 0U))
  {
    timeout--;
  }

  if (timeout != 0U)
  {
    USART6->TDR = (uint8_t)character;
  }
}

void Debug_Log_Init(void)
{
  GPIO_InitTypeDef gpio = {0};
  uint32_t peripheral_clock;

  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_USART6_CLK_ENABLE();

  gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLUP;
  gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  gpio.Alternate = GPIO_AF8_USART6;
  HAL_GPIO_Init(GPIOC, &gpio);

  USART6->CR1 = 0U;
  USART6->CR2 = 0U;
  USART6->CR3 = 0U;
  peripheral_clock = HAL_RCC_GetPCLK2Freq();
  USART6->BRR = (peripheral_clock + (DEBUG_BAUD_RATE / 2U)) /
                DEBUG_BAUD_RATE;
  USART6->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

  debug_ready = 1U;
  debug_stage = "debug ready";
  Debug_Log_Line("");
  Debug_Log_Line("========================================");
  Debug_Log_Line("F746_USB boot diagnostics");
  Debug_Log_U32("USART6 PCLK2 Hz: ", peripheral_clock);
  Debug_Log_Line("USART6: 115200 8N1, TX=Arduino D1/PC6");
  Debug_Log_Line("========================================");
}

void Debug_Log_Write(const char *text)
{
  if (text == NULL)
  {
    return;
  }

  while (*text != '\0')
  {
    if (*text == '\n')
    {
      debug_put_char('\r');
    }
    debug_put_char(*text++);
  }
}

void Debug_Log_Line(const char *text)
{
  Debug_Log_Write(text);
  Debug_Log_Write("\n");
}

void Debug_Log_U32(const char *label, uint32_t value)
{
  char digits[10];
  uint32_t count = 0U;

  Debug_Log_Write(label);
  do
  {
    digits[count++] = (char)('0' + (value % 10U));
    value /= 10U;
  } while ((value != 0U) && (count < sizeof(digits)));

  while (count != 0U)
  {
    debug_put_char(digits[--count]);
  }
  Debug_Log_Write("\n");
}

void Debug_Log_I32(const char *label, int32_t value)
{
  char digits[10];
  uint32_t magnitude;
  uint32_t count = 0U;

  Debug_Log_Write(label);
  if (value < 0)
  {
    debug_put_char('-');
    /* This form is also defined for INT32_MIN. */
    magnitude = (uint32_t)(-(value + 1)) + 1U;
  }
  else
  {
    magnitude = (uint32_t)value;
  }

  do
  {
    digits[count++] = (char)('0' + (magnitude % 10U));
    magnitude /= 10U;
  } while ((magnitude != 0U) && (count < sizeof(digits)));

  while (count != 0U)
  {
    debug_put_char(digits[--count]);
  }
  Debug_Log_Write("\n");
}

void Debug_Log_Hex(const char *label, uint32_t value)
{
  static const char hexadecimal[] = "0123456789ABCDEF";
  int32_t shift;

  Debug_Log_Write(label);
  Debug_Log_Write("0x");
  for (shift = 28; shift >= 0; shift -= 4)
  {
    debug_put_char(hexadecimal[(value >> (uint32_t)shift) & 0x0FU]);
  }
  Debug_Log_Write("\n");
}

void Debug_Log_IPv4(const char *label, uint32_t address)
{
  uint32_t octets[4];
  uint32_t index;

  octets[0] = (address >> 24) & 0xFFU;
  octets[1] = (address >> 16) & 0xFFU;
  octets[2] = (address >> 8) & 0xFFU;
  octets[3] = address & 0xFFU;

  Debug_Log_Write(label);
  for (index = 0U; index < 4U; index++)
  {
    char digits[3];
    uint32_t count = 0U;
    uint32_t value = octets[index];

    do
    {
      digits[count++] = (char)('0' + (value % 10U));
      value /= 10U;
    } while ((value != 0U) && (count < sizeof(digits)));

    while (count != 0U)
    {
      debug_put_char(digits[--count]);
    }
    if (index != 3U)
    {
      debug_put_char('.');
    }
  }
  Debug_Log_Write("\n");
}

void Debug_Log_SetStage(const char *stage)
{
  if (stage != NULL)
  {
    debug_stage = stage;
    Debug_Log_Write("[BOOT] ");
    Debug_Log_Line(stage);
  }
}

const char *Debug_Log_GetStage(void)
{
  return debug_stage;
}

void Debug_Log_Fault(const char *fault_name)
{
  Debug_Log_Write("*** CPU FAULT: ");
  Debug_Log_Line(fault_name);
  Debug_Log_Write("last stage: ");
  Debug_Log_Line(debug_stage);
  Debug_Log_Hex("CFSR  = ", SCB->CFSR);
  Debug_Log_Hex("HFSR  = ", SCB->HFSR);
  Debug_Log_Hex("DFSR  = ", SCB->DFSR);
  Debug_Log_Hex("MMFAR = ", SCB->MMFAR);
  Debug_Log_Hex("BFAR  = ", SCB->BFAR);
  Debug_Log_Hex("AFSR  = ", SCB->AFSR);
}
