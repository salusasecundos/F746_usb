#ifndef GX_USER_H
#define GX_USER_H

/* Application-specific GUIX footprint and scheduling settings. */
#define GX_THREAD_STACK_SIZE              4096U
#define GX_SYSTEM_THREAD_PRIORITY         12U
#define GX_SYSTEM_THREAD_TIMESLICE        2U
#define GX_SYSTEM_TIMER_MS                20U
#define GX_MAX_QUEUE_EVENTS               32U
#define GX_MAX_DIRTY_AREAS                32U
#define GX_MAX_CONTEXT_NESTING            4U
#define GX_DISABLE_UTF8_SUPPORT

#endif /* GX_USER_H */
