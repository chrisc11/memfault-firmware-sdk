//! @file
//!
//! A reference port of Memfault dependency functions
//! for the TI's Simplelink SDK / CC3220SF

#include <time.h>

#include "memfault/core/compiler.h"
#include "memfault/core/debug_log.h"
#include "memfault/core/log.h"
#include "memfault/core/platform/core.h"
#include "memfault/core/platform/device_info.h"
#include "memfault/core/platform/system_time.h"
#include "memfault/core/reboot_tracking.h"
#include "memfault/core/trace_event.h"
#include "memfault/metrics/metrics.h"
#include "memfault/metrics/platform/timer.h"
#include "memfault/panics/assert.h"
#include "memfault/panics/coredump.h"
#include "memfault/panics/fault_handling.h"

#include <ti/display/Display.h> // For Display_vprintf()
#include <ti/drivers/power/PowerCC32XX.h> // For MAP_PRCMHibernateCycleTrigger()
#include <ti/sysbios/family/arm/m3/Hwi.h> // For Hwi_plug()
#include <ti/devices/cc32xx/driverlib/prcm.h>

#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Task.h>
#include <xdc/runtime/Error.h>

extern Display_Handle display;

void memfault_platform_log(eMemfaultPlatformLogLevel level, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  Display_vprintf(display, 0, 0, fmt, args);
  va_end(args);
}

#define SOFTWARE_VERSION "1.0.0+" __TIME__

void memfault_platform_get_device_info(sMemfaultDeviceInfo *info) {
  *info = (sMemfaultDeviceInfo) {
    .device_serial = "DEMOSERIAL",
    .software_type = "wifi-fw",
    .software_version = SOFTWARE_VERSION,
    .hardware_version = "launchxl",
  };
}

void memfault_platform_reboot(void) {
  MAP_PRCMHibernateCycleTrigger();

  // should be impossible to get here
  while (1) { }
}

uint64_t memfault_platform_get_time_since_boot_ms(void) {
  // Note: uses Clock_getTicks() so doesn't rely on RTC being configured
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  return ((uint64_t)ts.tv_sec * 1000 + (ts.tv_nsec / 1000000));
}

bool memfault_platform_time_get_current(sMemfaultCurrentTime *time) {
  // Optionally, can be filled out if UTC time is being tracked by device
  //  *time = {
  //    .type = kMemfaultCurrentTimeType_UnixEpochTimeSec,
  //    .info = {
  //      .unix_timestamp_secs = get_unix_time(),
  //    }
  //  }
  return false; // false because we don't know the current time!
}


static Task_Struct metricTimerTask;
static char metricTimerTaskStack[512];

typedef struct MetricTimerArgs {
  uint32_t period_sec;
  MemfaultPlatformTimerCallback *callback;
} sMetricTimerArgs;

static sMetricTimerArgs s_metric_timer_args;

static Void postSem(UArg arg) {
  Semaphore_post((Semaphore_Handle)arg);
}

void prv_metric_timer_loop(UArg arg0, UArg arg1) {
  MEMFAULT_LOG_DEBUG("Starting Metric Timer Task");

  const sMetricTimerArgs *metric_task_args = (sMetricTimerArgs *)arg0;

  Semaphore_Handle heartbeat_sem = Semaphore_create(0, NULL, Error_IGNORE);
  MEMFAULT_ASSERT(heartbeat_sem != NULL);
  Clock_Params clockParams;
  Clock_Params_init(&clockParams);
  clockParams.arg = (UArg)heartbeat_sem;
  clockParams.startFlag = TRUE;
  clockParams.period = (metric_task_args->period_sec * 1000);
  Clock_Handle clockHandle = Clock_create(postSem, clockParams.period , &clockParams,
                                          Error_IGNORE);

  while (1) {
    Semaphore_pend(heartbeat_sem, BIOS_WAIT_FOREVER);

    metric_task_args->callback();
  }
}

bool memfault_platform_metrics_timer_boot(uint32_t period_sec,
                                          MemfaultPlatformTimerCallback callback) {
  s_metric_timer_args = (sMetricTimerArgs) {
    .period_sec = period_sec,
    .callback = callback,
  };

  Task_Params task_params;
  Task_Params_init(&task_params);
  task_params.stack = metricTimerTaskStack;
  task_params.stackSize = sizeof(metricTimerTaskStack);
  task_params.priority = 2;
  task_params.arg0 = (UArg)&s_metric_timer_args;
  Task_construct(&metricTimerTask, prv_metric_timer_loop,
                 &task_params, NULL);

  return true;
}

// Circular buffer where trace events and heartbeats are stored
static uint8_t s_event_storage[1024];
static uint8_t s_reboot_tracking[MEMFAULT_REBOOT_TRACKING_REGION_SIZE];

static void prv_capture_reboot_reason(void) {
  uint32_t reset_cause = MAP_PRCMSysResetCauseGet();

  eMemfaultRebootReason reset_reason = kMfltRebootReason_Unknown;
  switch (reset_cause) {
    case PRCM_POWER_ON:
      reset_reason = kMfltRebootReason_PowerOnReset;
      break;

    case PRCM_LPDS_EXIT:
      reset_reason = kMfltRebootReason_LowPower;
      break;

    case PRCM_WDT_RESET:
      reset_reason = kMfltRebootReason_Watchdog;
      break;

    case PRCM_CORE_RESET:
    case PRCM_MCU_RESET:
    case PRCM_HIB_EXIT:
      reset_reason = kMfltRebootReason_UserReset;
      break;

    default:
      reset_reason = kMfltRebootReason_Unknown;
  }

  const sResetBootupInfo reset_reason_info = {
    .reset_reason_reg = reset_cause,
    .reset_reason = reset_reason,
  };

  memfault_reboot_tracking_boot(s_reboot_tracking, &reset_reason_info);
}

// circular buffer used for saving logs. These will
// be captured as part of a coredump and displayed in the UI.
static uint8_t s_log_buf_storage[512];

void memfault_port_boot(void) {
  memfault_log_boot(s_log_buf_storage, sizeof(s_log_buf_storage));

  MEMFAULT_LOG_INFO("Initializing Memfault Subsystem");

  // Install Memfault Fault Handlers in Vector Table for Exceptions
  Hwi_plug(3, HardFault_Handler);
  Hwi_plug(4, MemoryManagement_Handler);
  Hwi_plug(5, BusFault_Handler);
  Hwi_plug(6, UsageFault_Handler);

  // Check that enough space has been provisioned to save a coredump &
  // log an error if not
  memfault_coredump_storage_check_size();

  size_t coredump_size = 0;
  bool has_coredump = memfault_coredump_has_valid_coredump(&coredump_size);
  if (has_coredump) {
    MEMFAULT_LOG_DEBUG("Memfault Coredump Present: %d bytes", coredump_size);
  }

  prv_capture_reboot_reason();

  const sMemfaultEventStorageImpl *evt_storage =
      memfault_events_storage_boot(s_event_storage, sizeof(s_event_storage));
  memfault_trace_event_boot(evt_storage);

  // start the heartbeat metrics system
  sMemfaultMetricBootInfo boot_info = { 0 };
  memfault_metrics_boot(evt_storage, &boot_info);

  memfault_reboot_tracking_collect_reset_info(evt_storage);
}
