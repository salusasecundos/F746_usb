#ifndef APP_DIAGNOSTICS_CONFIG_H
#define APP_DIAGNOSTICS_CONFIG_H

/* Master switch. Set to 0 to remove all run-time diagnostics. */
#define APP_DIAGNOSTICS_ENABLE              1U

/* Individual output channels. They are ignored when the master switch is 0. */
#define APP_DIAGNOSTICS_UART_ENABLE         1U
#define APP_DIAGNOSTICS_WEB_ENABLE          1U
#define APP_DIAGNOSTICS_GUI_ENABLE          1U

#define APP_DIAGNOSTICS_UART_PERIOD_SECONDS 30U
#define APP_DIAGNOSTICS_WEB_PORT            80U
#define APP_DIAGNOSTICS_WEB_REFRESH_SECONDS 5U
#define APP_DIAGNOSTICS_GUI_PERIOD_SECONDS  1U

#if (APP_DIAGNOSTICS_ENABLE == 0U)
#undef APP_DIAGNOSTICS_UART_ENABLE
#undef APP_DIAGNOSTICS_WEB_ENABLE
#undef APP_DIAGNOSTICS_GUI_ENABLE
#define APP_DIAGNOSTICS_UART_ENABLE         0U
#define APP_DIAGNOSTICS_WEB_ENABLE          0U
#define APP_DIAGNOSTICS_GUI_ENABLE          0U
#endif

#endif /* APP_DIAGNOSTICS_CONFIG_H */
