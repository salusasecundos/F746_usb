/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    app_netxduo.c
  * @author  MCD Application Team
  * @brief   NetXDuo applicative file
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
#include "app_netxduo.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "app_state.h"
#include "app_threadx.h"
#include "app_vnc_server.h"
#include "debug_log.h"
#include "nxd_dhcp_client.h"
#include "nx_stm32_eth_config.h"
#include "nx_stm32_phy_driver.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define APP_NETX_PACKET_PAYLOAD_SIZE 1536U
#define APP_NETX_PACKET_POOL_SIZE    (64U * 1024U)
#define APP_NETX_IP_STACK_SIZE       4096U
#define APP_NETX_THREAD_STACK_SIZE   4096U
#define APP_NETX_ARP_CACHE_SIZE      2048U
#define APP_NETX_THREAD_PRIORITY     8U
#define APP_NETX_RECEIVE_WAIT        (TX_TIMER_TICKS_PER_SECOND / 10U)
#define APP_NETX_ACCEPT_WAIT         (TX_TIMER_TICKS_PER_SECOND / 5U)
#define APP_NETX_DIAG_INTERVAL       (5U * TX_TIMER_TICKS_PER_SECOND)
#define APP_NETX_TCP_TX_QUEUE_DEPTH  4U
#define APP_NETX_TCP_RETRY_COUNT     3U
#define APP_NETX_LISTEN_BACKLOG      4U
#define APP_NETX_MUTEX_STALL_LOG     (4U * TX_TIMER_TICKS_PER_SECOND)
#define APP_NETX_MUTEX_STALL_RESET   (12U * TX_TIMER_TICKS_PER_SECOND)
#if (APP_DIAGNOSTICS_WEB_ENABLE != 0U)
#define APP_WEB_THREAD_STACK_SIZE     4096U
#define APP_WEB_THREAD_PRIORITY       16U
#define APP_WEB_ACCEPT_WAIT           TX_TIMER_TICKS_PER_SECOND
#define APP_WEB_REQUEST_WAIT          ((TX_TIMER_TICKS_PER_SECOND >= 2U) ? \
                                       (TX_TIMER_TICKS_PER_SECOND / 2U) : 1U)
#define APP_WEB_SEND_WAIT             (2U * TX_TIMER_TICKS_PER_SECOND)
#define APP_WEB_TX_QUEUE_DEPTH        4U
#define APP_WEB_LISTEN_BACKLOG        4U
#define APP_WEB_OUTPUT_SIZE           1200U
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */
static NX_PACKET_POOL app_packet_pool;
static NX_IP app_ip;
static NX_DHCP app_dhcp;
static NX_TCP_SOCKET app_server_socket;
static UINT app_server_socket_created;
static TX_THREAD app_network_thread;
#if (APP_DIAGNOSTICS_WEB_ENABLE != 0U)
static NX_TCP_SOCKET app_web_socket;
static UINT app_web_socket_created;
static TX_THREAD app_web_thread;
#endif

static UCHAR *app_network_thread_stack;
static UCHAR *app_ip_thread_stack;
static UCHAR *app_packet_pool_memory;
static UCHAR *app_arp_cache_memory;
#if (APP_DIAGNOSTICS_WEB_ENABLE != 0U)
static UCHAR *app_web_thread_stack;
static UCHAR app_web_output[APP_WEB_OUTPUT_SIZE];
static APP_DIAGNOSTICS_SNAPSHOT app_web_snapshot;
#endif

static volatile UINT app_dhcp_renew_requested;
static volatile UINT app_disconnect_requested;
static UINT app_dhcp_started;
static UINT app_driver_link_enabled;
static UINT app_phy_state_known;
static UINT app_last_phy_link;
static UINT app_last_dhcp_start_status = NX_SUCCESS;
static UCHAR app_last_dhcp_state = 0xFFU;
static UINT app_last_address_ready;
static ULONG app_last_ip_address;
static ULONG app_next_diag_tick;
static UINT app_netx_initialized;
static ULONG app_ip_mutex_stall_start;
static UINT app_ip_mutex_stall_logged;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
static VOID App_Network_Thread(ULONG thread_input);
static UINT App_Network_Send_Response(NX_TCP_SOCKET *socket_ptr);
static UINT App_Network_Process_Packet(NX_TCP_SOCKET *socket_ptr,
                                       NX_PACKET *packet_ptr,
                                       UCHAR receive_buffer[APP_PACKET_SIZE],
                                       ULONG *receive_length);
static UINT App_Network_Status_Update(UINT client_active);
static UINT App_Network_Link_Service(VOID);
static VOID App_Network_Dhcp_Renew(VOID);
static VOID App_Dhcp_State_Change(NX_DHCP *dhcp_ptr, UCHAR new_state);
static VOID App_Network_Periodic_Diagnostics(ULONG ip_address);
static UINT App_Network_Socket_Open(VOID);
static UINT App_Network_Socket_Reset(VOID);
#if (APP_DIAGNOSTICS_WEB_ENABLE != 0U)
typedef struct
{
  NX_TCP_SOCKET *socket;
  ULONG length;
  UINT status;
} APP_WEB_WRITER;

static VOID App_Web_Thread(ULONG thread_input);
static UINT App_Web_Send_Page(NX_TCP_SOCKET *socket_ptr);
static UINT App_Web_Writer_Flush(APP_WEB_WRITER *writer);
static VOID App_Web_Append_Text(APP_WEB_WRITER *writer, const char *text);
static VOID App_Web_Append_U32(APP_WEB_WRITER *writer, ULONG value);
static UINT App_Web_Socket_Open(VOID);
static UINT App_Web_Socket_Reset(VOID);
#endif

/* USER CODE END PFP */
/**
  * @brief  Application NetXDuo Initialization.
  * @param memory_ptr: memory pointer
  * @retval int
  */
UINT MX_NetXDuo_Init(VOID *memory_ptr)
{
  UINT ret = NX_SUCCESS;
  TX_BYTE_POOL *byte_pool = (TX_BYTE_POOL*)memory_ptr;

  /* USER CODE BEGIN MX_NetXDuo_MEM_POOL */
  if ((tx_byte_allocate(byte_pool, (VOID **)&app_packet_pool_memory,
                        APP_NETX_PACKET_POOL_SIZE, TX_NO_WAIT) != TX_SUCCESS) ||
      (tx_byte_allocate(byte_pool, (VOID **)&app_ip_thread_stack,
                        APP_NETX_IP_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS) ||
      (tx_byte_allocate(byte_pool, (VOID **)&app_network_thread_stack,
                        APP_NETX_THREAD_STACK_SIZE, TX_NO_WAIT) != TX_SUCCESS) ||
      (tx_byte_allocate(byte_pool, (VOID **)&app_arp_cache_memory,
                        APP_NETX_ARP_CACHE_SIZE, TX_NO_WAIT) != TX_SUCCESS)
#if (APP_DIAGNOSTICS_WEB_ENABLE != 0U)
      || (tx_byte_allocate(byte_pool, (VOID **)&app_web_thread_stack,
                           APP_WEB_THREAD_STACK_SIZE,
                           TX_NO_WAIT) != TX_SUCCESS)
#endif
      )
  {
    return NX_NOT_SUCCESSFUL;
  }
  /* USER CODE END MX_NetXDuo_MEM_POOL */

  /* USER CODE BEGIN MX_NetXDuo_Init */
  nx_system_initialize();

  ret = nx_packet_pool_create(&app_packet_pool, "Ethernet packet pool",
                              APP_NETX_PACKET_PAYLOAD_SIZE,
                              app_packet_pool_memory,
                              APP_NETX_PACKET_POOL_SIZE);
  if (ret != NX_SUCCESS)
  {
    return ret;
  }

  ret = nx_ip_create(&app_ip, "STM32F746 Ethernet", IP_ADDRESS(0, 0, 0, 0),
                     IP_ADDRESS(0, 0, 0, 0), &app_packet_pool,
                     nx_stm32_eth_driver, app_ip_thread_stack,
                     APP_NETX_IP_STACK_SIZE, 5U);
  if (ret != NX_SUCCESS)
  {
    return ret;
  }

  ret = nx_arp_enable(&app_ip, app_arp_cache_memory, APP_NETX_ARP_CACHE_SIZE);
  if (ret != NX_SUCCESS)
  {
    return ret;
  }

  ret = nx_icmp_enable(&app_ip);
  if (ret != NX_SUCCESS)
  {
    return ret;
  }

  ret = nx_tcp_enable(&app_ip);
  if (ret != NX_SUCCESS)
  {
    return ret;
  }

  ret = nx_udp_enable(&app_ip);
  if (ret != NX_SUCCESS)
  {
    return ret;
  }

  ret = nx_dhcp_create(&app_dhcp, &app_ip, "DHCP client");
  if (ret != NX_SUCCESS)
  {
    return ret;
  }

  ret = nx_dhcp_state_change_notify(&app_dhcp, App_Dhcp_State_Change);
  if (ret != NX_SUCCESS)
  {
    return ret;
  }

  ret = tx_thread_create(&app_network_thread, "NetX application",
                         App_Network_Thread, 0U,
                         app_network_thread_stack,
                         APP_NETX_THREAD_STACK_SIZE,
                         APP_NETX_THREAD_PRIORITY,
                         APP_NETX_THREAD_PRIORITY,
                         TX_NO_TIME_SLICE, TX_AUTO_START);
  if (ret != TX_SUCCESS)
  {
    return NX_NOT_SUCCESSFUL;
  }

#if (APP_DIAGNOSTICS_WEB_ENABLE != 0U)
  ret = tx_thread_create(&app_web_thread, "Diagnostics HTTP",
                         App_Web_Thread, 0U,
                         app_web_thread_stack,
                         APP_WEB_THREAD_STACK_SIZE,
                         APP_WEB_THREAD_PRIORITY,
                         APP_WEB_THREAD_PRIORITY,
                         TX_NO_TIME_SLICE, TX_AUTO_START);
  if (ret != TX_SUCCESS)
  {
    return NX_NOT_SUCCESSFUL;
  }
#endif

  ret = App_VNC_Server_Init(byte_pool, &app_ip, &app_packet_pool);
  if (ret != NX_SUCCESS)
  {
    Debug_Log_U32("[VNC] server initialization failed: ", ret);
    return ret;
  }

  app_netx_initialized = 1U;

  /* USER CODE END MX_NetXDuo_Init */

  return ret;
}

/* USER CODE BEGIN 1 */

static VOID App_Dhcp_State_Change(NX_DHCP *dhcp_ptr, UCHAR new_state)
{
  (void)dhcp_ptr;

  if (new_state != app_last_dhcp_state)
  {
    app_last_dhcp_state = new_state;
    Debug_Log_U32("[NETX] DHCP state: ", (uint32_t)new_state);
  }
}

static VOID App_Network_Periodic_Diagnostics(ULONG ip_address)
{
  ULONG now = tx_time_get();
  ULONG packets_sent = 0U;
  ULONG packets_received = 0U;
  ULONG receive_dropped = 0U;
  ULONG send_dropped = 0U;

  if ((app_next_diag_tick != 0U) &&
      ((LONG)(now - app_next_diag_tick) < 0))
  {
    return;
  }

  app_next_diag_tick = now + APP_NETX_DIAG_INTERVAL;
  (void)nx_ip_info_get(&app_ip, &packets_sent, NX_NULL,
                       &packets_received, NX_NULL, NX_NULL,
                       &receive_dropped, NX_NULL, &send_dropped,
                       NX_NULL, NX_NULL);

  Debug_Log_IPv4("[NETX] current IPv4: ", ip_address);
  Debug_Log_U32("[NETX] IP packets TX: ", packets_sent);
  Debug_Log_U32("[NETX] IP packets RX: ", packets_received);
  if ((receive_dropped != 0U) || (send_dropped != 0U))
  {
    Debug_Log_U32("[NETX] IP RX dropped: ", receive_dropped);
    Debug_Log_U32("[NETX] IP TX dropped: ", send_dropped);
  }

  if (ip_address == 0U)
  {
    Debug_Log_U32("[ETH] RX DMA IRQ: ", g_nx_stm32_eth_diagnostics.rx_interrupts);
    Debug_Log_U32("[ETH] RX buffers linked: ", g_nx_stm32_eth_diagnostics.rx_buffers_linked);
    Debug_Log_U32("[ETH] RX delivered: ", g_nx_stm32_eth_diagnostics.rx_packets_delivered);
    Debug_Log_U32("[ETH] RX buffers allocated: ", g_nx_stm32_eth_diagnostics.rx_allocated);
    Debug_Log_U32("[ETH] RX alloc failed: ", g_nx_stm32_eth_diagnostics.rx_alloc_failed);
    Debug_Log_U32("[ETH] TX DMA IRQ: ", g_nx_stm32_eth_diagnostics.tx_interrupts);
    Debug_Log_U32("[ETH] TX released: ", g_nx_stm32_eth_diagnostics.tx_packets_released);
    Debug_Log_U32("[ETH] DMA errors: ", g_nx_stm32_eth_diagnostics.error_interrupts);
    Debug_Log_Hex("[ETH] last DMA error: ", g_nx_stm32_eth_diagnostics.last_dma_error);
    Debug_Log_U32("[ETH] HAL start status: ", g_nx_stm32_eth_diagnostics.start_status);
    Debug_Log_Hex("[ETH] MACCR: ", ETH->MACCR);
    Debug_Log_Hex("[ETH] MACFFR: ", ETH->MACFFR);
    Debug_Log_Hex("[ETH] DMAOMR: ", ETH->DMAOMR);
    Debug_Log_Hex("[ETH] DMAIER: ", ETH->DMAIER);
    Debug_Log_Hex("[ETH] DMASR: ", ETH->DMASR);
    Debug_Log_Hex("[ETH] DMAMFBOCR: ", ETH->DMAMFBOCR);
    Debug_Log_Hex("[ETH] HAL ErrorCode: ", heth.ErrorCode);
    Debug_Log_Hex("[ETH] HAL DMAErrorCode: ", heth.DMAErrorCode);
  }
}

static UINT App_Network_Link_Service(VOID)
{
  ULONG driver_value = 0U;
  int32_t phy_state;
  UINT link_up;
  UINT status;

  /* Reading the PHY does not require app_ip.nx_ip_protection.  The previous
     NX_LINK_GET_STATUS path waited on that mutex forever, so a damaged or
     abandoned NetX lock also stopped cable detection and DHCP controls. */
  phy_state = nx_eth_phy_get_link_state();
  if (phy_state == ETH_PHY_STATUS_ERROR)
  {
    if (app_phy_state_known == 0U)
    {
      Debug_Log_Line("[NETX] PHY status read failed");
      app_phy_state_known = 1U;
    }
    return NX_FALSE;
  }

  link_up = (phy_state > ETH_PHY_STATUS_LINK_DOWN) ? NX_TRUE : NX_FALSE;
  if ((app_phy_state_known == 0U) || (link_up != app_last_phy_link))
  {
    Debug_Log_Line((link_up != NX_FALSE) ?
                   "[NETX] physical link up" :
                   "[NETX] physical link down");
    app_phy_state_known = 1U;
    app_last_phy_link = link_up;
  }

  if (link_up != NX_FALSE)
  {
    if (app_driver_link_enabled == 0U)
    {
      status = nx_ip_driver_direct_command(&app_ip, NX_LINK_ENABLE,
                                           &driver_value);
      if ((status != NX_SUCCESS) && (status != NX_ALREADY_ENABLED))
      {
        Debug_Log_U32("[NETX] driver link enable failed: ", status);
        return NX_FALSE;
      }

      app_driver_link_enabled = 1U;
      Debug_Log_Line("[NETX] Ethernet MAC enabled");
    }

    if (app_dhcp_started == 0U)
    {
      status = nx_dhcp_start(&app_dhcp);
      if ((status == NX_SUCCESS) || (status == NX_DHCP_ALREADY_STARTED))
      {
        app_dhcp_started = 1U;
        app_last_dhcp_start_status = NX_SUCCESS;
        Debug_Log_Line("[NETX] DHCP started");
      }
      else if (status != app_last_dhcp_start_status)
      {
        app_last_dhcp_start_status = status;
        Debug_Log_U32("[NETX] DHCP start failed: ", status);
      }
    }

    return NX_TRUE;
  }

  if (app_dhcp_started != 0U)
  {
    (void)nx_dhcp_stop(&app_dhcp);
    (void)nx_dhcp_reinitialize(&app_dhcp);
    app_dhcp_started = 0U;
    app_last_dhcp_start_status = NX_SUCCESS;
    Debug_Log_Line("[NETX] DHCP stopped");
  }

  if (app_driver_link_enabled != 0U)
  {
    (void)nx_ip_driver_direct_command(&app_ip, NX_LINK_DISABLE,
                                      &driver_value);
    app_driver_link_enabled = 0U;
    Debug_Log_Line("[NETX] Ethernet MAC disabled");
  }

  return NX_FALSE;
}

VOID App_NetXDuo_RequestDhcpRenew(VOID)
{
  app_dhcp_renew_requested = 1U;
  Debug_Log_Line("[NETX] DHCP RENEW requested");
}

VOID App_NetXDuo_RequestDisconnect(VOID)
{
  app_disconnect_requested = 1U;
  Debug_Log_Line("[NETX] CLOSE CLIENT requested (TCP 7001 only)");
}

static VOID App_Network_Dhcp_Renew(VOID)
{
  UINT status;

  if (app_dhcp_started == 0U)
  {
    Debug_Log_Line("[NETX] DHCP renew deferred: link/client not started");
    return;
  }

  status = nx_dhcp_force_renew(&app_dhcp);
  if (status == NX_SUCCESS)
  {
    Debug_Log_Line("[NETX] DHCP renew started");
    return;
  }

  /* A stale DHCP state should be rebuilt instead of silently ignoring the
     button.  Link service will start a clean discovery on its next pass. */
  Debug_Log_U32("[NETX] DHCP renew failed, restarting: ", status);
  (void)nx_dhcp_stop(&app_dhcp);
  (void)nx_dhcp_reinitialize(&app_dhcp);
  app_dhcp_started = 0U;
  app_last_dhcp_start_status = NX_SUCCESS;
}

VOID App_NetXDuo_WatchdogService(VOID)
{
  TX_THREAD *owner;
  ULONG now;
  UINT ip_thread_state;
  UINT owner_state = TX_TERMINATED;

  if (app_netx_initialized == 0U)
  {
    return;
  }

  /* This check intentionally does not call a NetX service: it must remain
     operational when the NetX protection mutex itself is wedged. */
  TX_INTERRUPT_SAVE_AREA
  TX_DISABLE
  ip_thread_state = app_ip.nx_ip_thread.tx_thread_state;
  owner = app_ip.nx_ip_protection.tx_mutex_owner;
  if (owner != TX_NULL)
  {
    owner_state = owner->tx_thread_state;
  }
  TX_RESTORE

  if (ip_thread_state != TX_MUTEX_SUSP)
  {
    app_ip_mutex_stall_start = 0U;
    app_ip_mutex_stall_logged = 0U;
    return;
  }

  now = tx_time_get();
  if (app_ip_mutex_stall_start == 0U)
  {
    app_ip_mutex_stall_start = now;
    return;
  }

  if (((now - app_ip_mutex_stall_start) >= APP_NETX_MUTEX_STALL_LOG) &&
      (app_ip_mutex_stall_logged == 0U))
  {
    app_ip_mutex_stall_logged = 1U;
    Debug_Log_Line("[NETX] ERROR: IP protection mutex is stuck");
    Debug_Log_Line((owner != TX_NULL) ? owner->tx_thread_name :
                   "[NETX] mutex owner: none/corrupt");
    Debug_Log_U32("[NETX] mutex owner state: ", owner_state);
    Debug_Log_U32("[NETX] mutex ownership count: ",
                  app_ip.nx_ip_protection.tx_mutex_ownership_count);
    Debug_Log_U32("[NETX] packet pool free: ",
                  app_packet_pool.nx_packet_pool_available);
  }

  if ((now - app_ip_mutex_stall_start) >= APP_NETX_MUTEX_STALL_RESET)
  {
    /* Releasing an unknown owner's mutex would leave TCP/IP lists in an
       undefined state.  A controlled MCU restart is the only safe recovery
       after the stack invariant has already been broken. */
    Debug_Log_Line("[NETX] fatal stall: controlled watchdog reset");
    NVIC_SystemReset();
  }
}

static UINT App_Network_Status_Update(UINT client_active)
{
  ULONG actual_status = 0U;
  ULONG ip_address = 0U;
  ULONG network_mask = 0U;
  UINT link_up;
  UINT address_ready;

  link_up = App_Network_Link_Service();
  address_ready = (nx_ip_status_check(&app_ip, NX_IP_ADDRESS_RESOLVED,
                                      &actual_status, NX_NO_WAIT) == NX_SUCCESS);

  if (address_ready != 0U)
  {
    (void)nx_ip_address_get(&app_ip, &ip_address, &network_mask);
  }

  if ((address_ready != app_last_address_ready) ||
      ((address_ready != 0U) && (ip_address != app_last_ip_address)))
  {
    if (address_ready != 0U)
    {
      Debug_Log_IPv4("[NETX] IPv4 address: ", ip_address);
    }
    else if (app_last_address_ready != 0U)
    {
      Debug_Log_Line("[NETX] IPv4 address lost");
    }
    app_last_address_ready = address_ready;
    app_last_ip_address = ip_address;
  }

  App_Network_Periodic_Diagnostics(ip_address);

  App_State_SetLan(link_up, address_ready, ip_address);
  App_State_SetLanClient(client_active);
  return ((link_up != 0U) && (address_ready != 0U)) ? NX_SUCCESS : NX_NOT_SUCCESSFUL;
}

static UINT App_Network_Send_Response(NX_TCP_SOCKET *socket_ptr)
{
  NX_PACKET *packet_ptr;
  UCHAR response[APP_PACKET_SIZE];
  UINT status;

  App_State_BuildResponse(response);

  status = nx_packet_allocate(&app_packet_pool, &packet_ptr, NX_TCP_PACKET,
                              APP_NETX_RECEIVE_WAIT);
  if (status != NX_SUCCESS)
  {
    return status;
  }

  status = nx_packet_data_append(packet_ptr, response, sizeof(response),
                                 &app_packet_pool, APP_NETX_RECEIVE_WAIT);
  if (status != NX_SUCCESS)
  {
    (void)nx_packet_release(packet_ptr);
    return status;
  }

  status = nx_tcp_socket_send(socket_ptr, packet_ptr, APP_NETX_RECEIVE_WAIT);
  if (status != NX_SUCCESS)
  {
    (void)nx_packet_release(packet_ptr);
    return status;
  }

  App_State_NoteLanTx();
  return NX_SUCCESS;
}

static UINT App_Network_Process_Packet(NX_TCP_SOCKET *socket_ptr,
                                       NX_PACKET *packet_ptr,
                                       UCHAR receive_buffer[APP_PACKET_SIZE],
                                       ULONG *receive_length)
{
  ULONG packet_length = 0U;
  ULONG packet_offset = 0U;
  ULONG copied;
  ULONG chunk;
  UINT status = NX_SUCCESS;

  (void)nx_packet_length_get(packet_ptr, &packet_length);

  while (packet_offset < packet_length)
  {
    chunk = APP_PACKET_SIZE - *receive_length;
    if (chunk > (packet_length - packet_offset))
    {
      chunk = packet_length - packet_offset;
    }

    copied = 0U;
    status = nx_packet_data_extract_offset(packet_ptr, packet_offset,
                                           &receive_buffer[*receive_length],
                                           chunk, &copied);
    if ((status != NX_SUCCESS) || (copied == 0U))
    {
      break;
    }

    packet_offset += copied;
    *receive_length += copied;

    if (*receive_length == APP_PACKET_SIZE)
    {
      App_State_SetControls(receive_buffer);
      App_State_NoteLanRx();
      *receive_length = 0U;

      status = App_Network_Send_Response(socket_ptr);
      if (status != NX_SUCCESS)
      {
        break;
      }
    }
  }

  return status;
}

static UINT App_Network_Socket_Open(VOID)
{
  UINT status;

  status = nx_tcp_socket_create(&app_ip, &app_server_socket,
                                "F746 data server", NX_IP_NORMAL,
                                NX_FRAGMENT_OKAY, NX_IP_TIME_TO_LIVE,
                                4096U, NX_NULL, NX_NULL);
  if (status != NX_SUCCESS)
  {
    return status;
  }
  app_server_socket_created = 1U;

  status = nx_tcp_socket_transmit_configure(&app_server_socket,
                                             APP_NETX_TCP_TX_QUEUE_DEPTH,
                                             NX_IP_PERIODIC_RATE,
                                             APP_NETX_TCP_RETRY_COUNT, 1U);
  if (status == NX_SUCCESS)
  {
    status = nx_tcp_server_socket_listen(&app_ip, APP_NETX_TCP_PORT,
                                         &app_server_socket,
                                         APP_NETX_LISTEN_BACKLOG, NX_NULL);
  }
  if (status != NX_SUCCESS)
  {
    (void)nx_tcp_socket_delete(&app_server_socket);
    app_server_socket_created = 0U;
  }
  return status;
}

static UINT App_Network_Socket_Reset(VOID)
{
  UINT status;

  if (app_server_socket_created == 0U)
  {
    return App_Network_Socket_Open();
  }

  (void)nx_tcp_socket_disconnect(&app_server_socket, NX_NO_WAIT);
  status = nx_tcp_server_socket_unaccept(&app_server_socket);
  if (status != NX_SUCCESS)
  {
    Debug_Log_U32("[NETX] TCP 7001 unaccept failed: ", status);
    App_State_SetLanClient(0U);
    return status;
  }

  status = nx_tcp_server_socket_relisten(&app_ip, APP_NETX_TCP_PORT,
                                         &app_server_socket);
  if (status == NX_CONNECTION_PENDING)
  {
    /* NetX has already attached a queued SYN to this socket.  This is a
       successful relisten result, not an error requiring reconstruction. */
    status = NX_SUCCESS;
  }
  else if (status == NX_INVALID_RELISTEN)
  {
    /* The listen record is absent, but the unaccepted socket is CLOSED and
       reusable.  Restore only the listener; do not delete a live socket. */
    status = nx_tcp_server_socket_listen(&app_ip, APP_NETX_TCP_PORT,
                                         &app_server_socket,
                                         APP_NETX_LISTEN_BACKLOG, NX_NULL);
  }
  else if (status != NX_SUCCESS)
  {
    Debug_Log_U32("[NETX] TCP 7001 relisten failed: ", status);
  }

  App_State_SetLanClient(0U);
  return status;
}

static VOID App_Network_Thread(ULONG thread_input)
{
  NX_PACKET *packet_ptr;
  UCHAR receive_buffer[APP_PACKET_SIZE];
  ULONG receive_length = 0U;
  UINT status;

  (void)thread_input;

  status = App_Network_Socket_Open();
  if (status != NX_SUCCESS)
  {
    return;
  }

  for (;;)
  {
    if (app_dhcp_renew_requested != 0U)
    {
      app_dhcp_renew_requested = 0U;
      App_Network_Dhcp_Renew();
    }

    if (App_Network_Status_Update(0U) != NX_SUCCESS)
    {
      tx_thread_sleep(APP_NETX_ACCEPT_WAIT);
      continue;
    }

    status = nx_tcp_server_socket_accept(&app_server_socket,
                                         APP_NETX_ACCEPT_WAIT);
    if (status != NX_SUCCESS)
    {
      continue;
    }

    receive_length = 0U;
    app_disconnect_requested = 0U;
    App_State_SetLanClient(1U);

    while ((app_disconnect_requested == 0U) &&
           (App_Network_Status_Update(1U) == NX_SUCCESS))
    {
      if (app_dhcp_renew_requested != 0U)
      {
        app_dhcp_renew_requested = 0U;
        App_Network_Dhcp_Renew();
      }

      packet_ptr = NX_NULL;
      status = nx_tcp_socket_receive(&app_server_socket, &packet_ptr,
                                     APP_NETX_RECEIVE_WAIT);
      if (status == NX_SUCCESS)
      {
        status = App_Network_Process_Packet(&app_server_socket, packet_ptr,
                                            receive_buffer, &receive_length);
        (void)nx_packet_release(packet_ptr);
        if (status != NX_SUCCESS)
        {
          break;
        }
      }
      else if (status != NX_NO_PACKET)
      {
        break;
      }
    }

    do
    {
      status = App_Network_Socket_Reset();
      if (status != NX_SUCCESS)
      {
        tx_thread_sleep(APP_NETX_ACCEPT_WAIT);
      }
    } while (status != NX_SUCCESS);
  }
}

#if (APP_DIAGNOSTICS_WEB_ENABLE != 0U)

static UINT App_Web_Writer_Flush(APP_WEB_WRITER *writer)
{
  NX_PACKET *packet_ptr;

  if ((writer->status != NX_SUCCESS) || (writer->length == 0U))
  {
    return writer->status;
  }

  writer->status = nx_packet_allocate(&app_packet_pool, &packet_ptr,
                                      NX_TCP_PACKET, APP_WEB_SEND_WAIT);
  if (writer->status != NX_SUCCESS)
  {
    return writer->status;
  }

  writer->status = nx_packet_data_append(packet_ptr, app_web_output,
                                         writer->length, &app_packet_pool,
                                         APP_WEB_SEND_WAIT);
  if (writer->status != NX_SUCCESS)
  {
    (void)nx_packet_release(packet_ptr);
    return writer->status;
  }

  writer->status = nx_tcp_socket_send(writer->socket, packet_ptr,
                                      APP_WEB_SEND_WAIT);
  if (writer->status != NX_SUCCESS)
  {
    (void)nx_packet_release(packet_ptr);
    return writer->status;
  }

  writer->length = 0U;
  return NX_SUCCESS;
}

static VOID App_Web_Append_Text(APP_WEB_WRITER *writer, const char *text)
{
  while ((*text != '\0') && (writer->status == NX_SUCCESS))
  {
    if (writer->length >= APP_WEB_OUTPUT_SIZE)
    {
      (void)App_Web_Writer_Flush(writer);
    }
    if (writer->status == NX_SUCCESS)
    {
      app_web_output[writer->length++] = (UCHAR)*text++;
    }
  }
}

static VOID App_Web_Append_U32(APP_WEB_WRITER *writer, ULONG value)
{
  char reverse[10];
  UINT count = 0U;

  do
  {
    reverse[count++] = (char)('0' + (value % 10U));
    value /= 10U;
  } while ((value != 0U) && (count < sizeof(reverse)));

  while ((count != 0U) && (writer->status == NX_SUCCESS))
  {
    char digit[2] = {reverse[--count], '\0'};
    App_Web_Append_Text(writer, digit);
  }
}

static const char *App_Web_Thread_State_Name(UINT state)
{
  switch (state)
  {
    case TX_READY:             return "READY";
    case TX_COMPLETED:         return "DONE";
    case TX_TERMINATED:        return "TERM";
    case TX_SUSPENDED:         return "SUSP";
    case TX_SLEEP:             return "SLEEP";
    case TX_QUEUE_SUSP:        return "QUEUE";
    case TX_SEMAPHORE_SUSP:    return "SEMA";
    case TX_EVENT_FLAG:        return "EVENT";
    case TX_BLOCK_MEMORY:      return "BLOCK";
    case TX_BYTE_MEMORY:       return "BYTE";
    case TX_IO_DRIVER:         return "IO";
    case TX_FILE:              return "FILE";
    case TX_TCP_IP:            return "TCPIP";
    case TX_MUTEX_SUSP:        return "MUTEX";
    case TX_PRIORITY_CHANGE:   return "PRIO";
    default:                   return "OTHER";
  }
}

static VOID App_Web_Append_Memory_Row(APP_WEB_WRITER *writer,
                                      const char *name, ULONG used,
                                      ULONG free, ULONG total)
{
  App_Web_Append_Text(writer, "<tr><td>");
  App_Web_Append_Text(writer, name);
  App_Web_Append_Text(writer, "</td><td>");
  App_Web_Append_U32(writer, used);
  App_Web_Append_Text(writer, "</td><td>");
  App_Web_Append_U32(writer, free);
  App_Web_Append_Text(writer, "</td><td>");
  App_Web_Append_U32(writer, total);
  App_Web_Append_Text(writer, "</td></tr>");
}

static UINT App_Web_Send_Page(NX_TCP_SOCKET *socket_ptr)
{
  APP_WEB_WRITER writer = {socket_ptr, 0U, NX_SUCCESS};
  ULONG packet_total = 0U;
  ULONG packet_free = 0U;
  ULONG empty_requests = 0U;
  ULONG empty_suspensions = 0U;
  ULONG invalid_releases = 0U;
  ULONG ip = app_last_ip_address;

  if (App_Diagnostics_Snapshot_Get(&app_web_snapshot) != TX_SUCCESS)
  {
    return NX_NOT_SUCCESSFUL;
  }

  (void)nx_packet_pool_info_get(&app_packet_pool, &packet_total, &packet_free,
                                &empty_requests, &empty_suspensions,
                                &invalid_releases);

  App_Web_Append_Text(&writer,
      "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
      "Cache-Control: no-store\r\nConnection: close\r\n\r\n"
      "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<meta http-equiv='refresh' content='");
  App_Web_Append_U32(&writer, APP_DIAGNOSTICS_WEB_REFRESH_SECONDS);
  App_Web_Append_Text(&writer,
      "'><title>F746 diagnostics</title><style>"
      "body{font:14px monospace;background:#0c1420;color:#e8eff6;margin:18px}"
      "h1,h2{color:#00be96} .cards{display:flex;gap:12px;flex-wrap:wrap}"
      ".card{background:#121f2d;border:1px solid #294158;padding:10px 14px}"
      "table{border-collapse:collapse;width:100%;margin:8px 0 18px}"
      "th,td{border-bottom:1px solid #294158;padding:5px 7px;text-align:left}"
      "th{color:#7ed9c5} .ok{color:#00d7a5}</style></head><body>"
      "<h1>STM32F746 diagnostics</h1><div class='cards'><div class='card'>IP: ");
  App_Web_Append_U32(&writer, (ip >> 24) & 0xFFU);
  App_Web_Append_Text(&writer, ".");
  App_Web_Append_U32(&writer, (ip >> 16) & 0xFFU);
  App_Web_Append_Text(&writer, ".");
  App_Web_Append_U32(&writer, (ip >> 8) & 0xFFU);
  App_Web_Append_Text(&writer, ".");
  App_Web_Append_U32(&writer, ip & 0xFFU);
  App_Web_Append_Text(&writer, "</div><div class='card'>Uptime: ");
  App_Web_Append_U32(&writer, app_web_snapshot.uptime_seconds);
  App_Web_Append_Text(&writer, " s</div><div class='card'>Threads: ");
  App_Web_Append_U32(&writer, app_web_snapshot.thread_count);
  App_Web_Append_Text(&writer, "</div></div>");

  App_Web_Append_Text(&writer,
      "<h2>Memory, bytes</h2><table><tr><th>Region</th><th>Used</th>"
      "<th>Free</th><th>Total</th></tr>");
  App_Web_Append_Memory_Row(&writer, "Flash", app_web_snapshot.flash_used,
                            app_web_snapshot.flash_free,
                            app_web_snapshot.flash_total);
  App_Web_Append_Memory_Row(&writer, "AXI SRAM", app_web_snapshot.axi_ram_used,
                            app_web_snapshot.axi_ram_free,
                            app_web_snapshot.axi_ram_total);
  App_Web_Append_Memory_Row(&writer, "SDRAM assigned",
                            app_web_snapshot.sdram_reserved,
                            app_web_snapshot.sdram_unassigned,
                            app_web_snapshot.sdram_total);
  App_Web_Append_Memory_Row(&writer, "Thread stacks",
                            app_web_snapshot.stack_total -
                                app_web_snapshot.stack_free,
                            app_web_snapshot.stack_free,
                            app_web_snapshot.stack_total);
  App_Web_Append_Memory_Row(&writer, "Managed heaps",
                            app_web_snapshot.heap_total -
                                app_web_snapshot.heap_free,
                            app_web_snapshot.heap_free,
                            app_web_snapshot.heap_total);
  App_Web_Append_Text(&writer, "</table>");

  App_Web_Append_Text(&writer,
      "<h2>ThreadX byte pools</h2><table><tr><th>Name</th><th>Free</th>"
      "<th>Total</th><th>Fragments</th></tr>");
  for (UINT index = 0U; index < app_web_snapshot.pool_count; index++)
  {
    APP_DIAGNOSTICS_POOL *pool = &app_web_snapshot.pools[index];
    App_Web_Append_Text(&writer, "<tr><td>");
    App_Web_Append_Text(&writer, pool->name);
    App_Web_Append_Text(&writer, "</td><td>");
    App_Web_Append_U32(&writer, pool->free_bytes);
    App_Web_Append_Text(&writer, "</td><td>");
    App_Web_Append_U32(&writer, pool->total_bytes);
    App_Web_Append_Text(&writer, "</td><td>");
    App_Web_Append_U32(&writer, pool->fragments);
    App_Web_Append_Text(&writer, "</td></tr>");
  }
  App_Web_Append_Text(&writer, "</table>");

  App_Web_Append_Text(&writer,
      "<h2>NetX packet pool</h2><table><tr><th>Total</th><th>Free</th>"
      "<th>Empty requests</th><th>Suspensions</th><th>Invalid releases</th>"
      "</tr><tr><td>");
  App_Web_Append_U32(&writer, packet_total);
  App_Web_Append_Text(&writer, "</td><td>");
  App_Web_Append_U32(&writer, packet_free);
  App_Web_Append_Text(&writer, "</td><td>");
  App_Web_Append_U32(&writer, empty_requests);
  App_Web_Append_Text(&writer, "</td><td>");
  App_Web_Append_U32(&writer, empty_suspensions);
  App_Web_Append_Text(&writer, "</td><td>");
  App_Web_Append_U32(&writer, invalid_releases);
  App_Web_Append_Text(&writer, "</td></tr></table>");

  App_Web_Append_Text(&writer,
      "<h2>Threads</h2><table><tr><th>Name</th><th>State</th><th>Priority</th>"
      "<th>Runs</th><th>Stack used</th><th>Stack free</th><th>Total</th></tr>");
  for (UINT index = 0U; index < app_web_snapshot.thread_count; index++)
  {
    APP_DIAGNOSTICS_THREAD *thread = &app_web_snapshot.threads[index];
    App_Web_Append_Text(&writer, "<tr><td>");
    App_Web_Append_Text(&writer, thread->name);
    App_Web_Append_Text(&writer, "</td><td>");
    App_Web_Append_Text(&writer, App_Web_Thread_State_Name(thread->state));
    App_Web_Append_Text(&writer, "</td><td>");
    App_Web_Append_U32(&writer, thread->priority);
    App_Web_Append_Text(&writer, "</td><td>");
    App_Web_Append_U32(&writer, thread->run_count);
    App_Web_Append_Text(&writer, "</td><td>");
    App_Web_Append_U32(&writer, thread->stack_used);
    App_Web_Append_Text(&writer, "</td><td>");
    App_Web_Append_U32(&writer, thread->stack_free);
    App_Web_Append_Text(&writer, "</td><td>");
    App_Web_Append_U32(&writer, thread->stack_total);
    App_Web_Append_Text(&writer, "</td></tr>");
  }
  App_Web_Append_Text(&writer,
      "</table><p class='ok'>Auto refresh enabled.</p></body></html>");

  return App_Web_Writer_Flush(&writer);
}

static UINT App_Web_Socket_Open(VOID)
{
  UINT status;

  status = nx_tcp_socket_create(&app_ip, &app_web_socket,
                                "F746 diagnostics HTTP", NX_IP_NORMAL,
                                NX_FRAGMENT_OKAY, NX_IP_TIME_TO_LIVE,
                                4096U, NX_NULL, NX_NULL);
  if (status != NX_SUCCESS)
  {
    return status;
  }
  app_web_socket_created = 1U;

  status = nx_tcp_socket_transmit_configure(&app_web_socket,
                                             APP_WEB_TX_QUEUE_DEPTH,
                                             NX_IP_PERIODIC_RATE,
                                             APP_NETX_TCP_RETRY_COUNT, 1U);
  if (status == NX_SUCCESS)
  {
    status = nx_tcp_server_socket_listen(&app_ip,
                                         APP_DIAGNOSTICS_WEB_PORT,
                                         &app_web_socket,
                                         APP_WEB_LISTEN_BACKLOG, NX_NULL);
  }
  if (status != NX_SUCCESS)
  {
    (void)nx_tcp_socket_delete(&app_web_socket);
    app_web_socket_created = 0U;
  }
  return status;
}

static UINT App_Web_Socket_Reset(VOID)
{
  UINT disconnect_status;
  UINT status;

  if (app_web_socket_created == 0U)
  {
    return App_Web_Socket_Open();
  }

  /* The response packets are only queued by nx_tcp_socket_send().  Give
     NetX a bounded interval to transmit/ack them before unaccepting the
     socket; NX_NO_WAIT here produced a valid TCP connect followed by an
     empty HTTP response. */
  disconnect_status = nx_tcp_socket_disconnect(&app_web_socket,
                                                APP_WEB_SEND_WAIT);
  if ((disconnect_status != NX_SUCCESS) &&
      (disconnect_status != NX_NOT_CONNECTED))
  {
    Debug_Log_U32("[WEB] graceful disconnect status: ",
                  disconnect_status);
    /* A timed-out close is still forced to a known state by unaccept below. */
  }

  status = nx_tcp_server_socket_unaccept(&app_web_socket);
  if (status != NX_SUCCESS)
  {
    Debug_Log_U32("[WEB] unaccept failed: ", status);
    return status;
  }

  status = nx_tcp_server_socket_relisten(&app_ip,
                                         APP_DIAGNOSTICS_WEB_PORT,
                                         &app_web_socket);
  if (status == NX_CONNECTION_PENDING)
  {
    status = NX_SUCCESS;
  }
  else if (status == NX_INVALID_RELISTEN)
  {
    status = nx_tcp_server_socket_listen(&app_ip,
                                         APP_DIAGNOSTICS_WEB_PORT,
                                         &app_web_socket,
                                         APP_WEB_LISTEN_BACKLOG, NX_NULL);
  }
  else if (status != NX_SUCCESS)
  {
    Debug_Log_U32("[WEB] relisten failed: ", status);
  }
  return status;
}

static VOID App_Web_Thread(ULONG thread_input)
{
  NX_PACKET *request_packet;
  ULONG actual_status;
  UINT status;

  (void)thread_input;

  do
  {
    status = App_Web_Socket_Open();
    if (status != NX_SUCCESS)
    {
      Debug_Log_U32("[WEB] initial socket/listen failed: ", status);
      tx_thread_sleep(APP_WEB_ACCEPT_WAIT);
    }
  } while (status != NX_SUCCESS);

#if (APP_DIAGNOSTICS_UART_ENABLE != 0U)
  Debug_Log_U32("[WEB] diagnostics HTTP port: ",
                APP_DIAGNOSTICS_WEB_PORT);
#endif

  for (;;)
  {
    if (nx_ip_status_check(&app_ip, NX_IP_ADDRESS_RESOLVED, &actual_status,
                           NX_NO_WAIT) != NX_SUCCESS)
    {
      tx_thread_sleep(APP_WEB_ACCEPT_WAIT);
      continue;
    }

    status = nx_tcp_server_socket_accept(&app_web_socket,
                                         APP_WEB_ACCEPT_WAIT);
    if (status != NX_SUCCESS)
    {
      continue;
    }

    request_packet = NX_NULL;
    status = nx_tcp_socket_receive(&app_web_socket, &request_packet,
                                   APP_WEB_REQUEST_WAIT);
    if (request_packet != NX_NULL)
    {
      (void)nx_packet_release(request_packet);
    }
    if (status == NX_SUCCESS)
    {
      status = App_Web_Send_Page(&app_web_socket);
      if (status != NX_SUCCESS)
      {
        Debug_Log_U32("[WEB] page send failed: ", status);
      }
    }

    do
    {
      status = App_Web_Socket_Reset();
      if (status != NX_SUCCESS)
      {
        tx_thread_sleep(APP_WEB_ACCEPT_WAIT);
      }
    } while (status != NX_SUCCESS);
  }
}

#endif /* APP_DIAGNOSTICS_WEB_ENABLE */

/* USER CODE END 1 */
