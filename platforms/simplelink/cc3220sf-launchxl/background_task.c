#include <stdint.h>
#include <unistd.h>
#include <time.h>

/* RTOS header files */
#include <xdc/std.h>
#include <xdc/runtime/Error.h>
#include <ti/drivers/GPIO.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/hal/Hwi.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Task.h>

/* Driver Header files */
#include <ti/drivers/GPIO.h>
#include <ti/drivers/I2C.h>

/* Driver configuration */
#include "ti_drivers_config.h"
#include <ti/display/Display.h>
#include <ti/sysbios/hal/Seconds.h>
extern Display_Handle display;

#include "memfault/core/debug_log.h"
#include "memfault/core/platform/core.h"
#include "memfault/core/trace_event.h"
#include "memfault/panics/assert.h"

static Semaphore_Handle s_background_task_sem;

static Void postSem(UArg arg) {
  Semaphore_post((Semaphore_Handle)arg);
}

Semaphore_Handle setupTimer(Clock_Handle *clockHandle, unsigned int ms) {
  s_background_task_sem = Semaphore_create(0, NULL, Error_IGNORE);
  MEMFAULT_ASSERT(s_background_task_sem != NULL);

  /* Create the timer that wakes up the thread that will pend on the sem. */
  Clock_Params clockParams;
  Clock_Params_init(&clockParams);
  clockParams.arg = (UArg)s_background_task_sem;
  clockParams.startFlag = TRUE;
  clockParams.period = ms;
  *clockHandle = Clock_create(postSem, ms, &clockParams, Error_IGNORE);

  return s_background_task_sem;
}

static bool s_sw2_pressed = false;
static bool s_sw3_pressed = false;

static void prv_sw2_irq(uint_least8_t index) {
  s_sw2_pressed = true;
  Semaphore_post(s_background_task_sem);
}

static void prv_sw3_irq(uint_least8_t index) {
  s_sw3_pressed = true;
  Semaphore_post(s_background_task_sem);
}

MEMFAULT_NO_OPT
void example_crash_function(char *argv[]) {
  MEMFAULT_ASSERT(0);
}

MEMFAULT_NO_OPT
void example_crash_frame_1(char *argv[]) {
   example_crash_function(argv);
}

MEMFAULT_NO_OPT
void example_crash_frame_2(char *argv[]) {
  example_crash_frame_1(argv);
}

MEMFAULT_NO_OPT
void example_crash_frame_3(char *argv[]) {
  example_crash_frame_2(argv);
}

void trigger_crash(void) {
  example_crash_frame_3(NULL);
}

void background_thread(void *arg0, void *arg1) {
  Clock_Handle     clockHandle;
  setupTimer(&clockHandle, 1000);

  static int s_loop_count = 0;

  while (1) {
    Semaphore_pend(s_background_task_sem, BIOS_WAIT_FOREVER);
    if (s_sw2_pressed) {
      s_sw2_pressed = false;
      MEMFAULT_LOG_INFO("SW2 Pressed - Recording Trace Event");
      MEMFAULT_TRACE_EVENT(Sw2TestEvent);
    }

    if (s_sw3_pressed) {
      s_sw3_pressed = false;
      MEMFAULT_LOG_INFO("SW3 Pressed - Triggering Crash");
      trigger_crash();
    }

    s_loop_count++;
    if ((s_loop_count % 10) == 0) {
      MEMFAULT_LOG_DEBUG("%s: Uptime: %d", __func__, (int)memfault_platform_get_time_since_boot_ms());
    }
  }
}

void background_thread_start(void) {
  Seconds_Time ts = { 0 };
  Seconds_setTime(&ts);

  // install button press handlers for testing
  GPIO_setCallback(CONFIG_GPIO_BUTTON_0, prv_sw2_irq);
  GPIO_enableInt(CONFIG_GPIO_BUTTON_0); // SW2

  GPIO_setCallback(CONFIG_GPIO_BUTTON_1, prv_sw3_irq);
  GPIO_enableInt(CONFIG_GPIO_BUTTON_1); // SW3

  MEMFAULT_LOG_DEBUG("Staring background threads");
  /* Construct writer/reader Task threads */
  Task_Params taskParams;
  Task_Params_init(&taskParams);
  taskParams.stackSize = 1024;
  taskParams.priority = 2;
  Task_Handle background_task_handle = Task_create(background_thread, &taskParams, Error_IGNORE);
  MEMFAULT_ASSERT(background_task_handle != NULL);
}
