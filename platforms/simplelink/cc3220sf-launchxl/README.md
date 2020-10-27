## Example Summary

This application demonstrates how to integrate the Memfault Firmware SDK with the TI Simplelink
SDK. It was tested against the CC32xx 4.30.00.06 SDK on the CC3220SF-LAUNCHXL development board.

## Example Usage

* Navigate to `memfault_port/demo_settings_config.h` and fill in the required configuration defines
  (`SSID_NAME`, `SECURITY_KEY`, & `MEMFAULT_PROJECT_API_KEY`)
* Build the project, flash it by using the Uniflash tool for cc32xx,
or equivalently, run debug session on the IDE of your choice.
* Open a serial port session (e.g. 'HyperTerminal','puTTY', 'Tera Term', 'miniterm'.) to the appropriate COM port -
listed as 'User UART'.
The COM port can be determined via Device Manager in Windows or via `ls /dev/tty*` in Linux.

The connection should have the following connection settings:

    Baud-rate:    115200
    Data bits:         8
    Stop bits:         1
    Parity:         None
    Flow Control:   None


* Run the example by pressing the reset button or by running debug session through your IDE.
* The example app will run a loop that posts diagnostic data collected to the Memfault Cloud.
* Pressing button `SW2` will generate a test Memfault Error Trace Event
* Pressing button `SW3` will trigger an assert, save a coredump to internal flash, & reboot the
device
* Heartbeats are collected

## Porting to your Application

```c
memfault_port/
├── memfault_port
│   ├── cc32xx_coredump_storage.c
│   ├── memfault_port.c
│   ├── memfault_metrics_heartbeat_config.def
│   └── memfault_trace_reason_user_config.def
```

where

* [`memfault_trace_reason_user_config.def`](memfault_port/memfault_trace_reason_user_config.def) contains user defined
[trace errors](https://mflt.io/error-tracing)
* [`memfault_metrics_heartbeat_config.def`](memfault_port/memfault_metrics_heartbeat_config.def) contains user defined
[heartbeat metrics](https://mflt.io/embedded-metrics)
* [`memfault_port.c`](memfault_port/memfault_port.c) contains an implementation for the dependency functions memfault needs
* [`cc32xx_coredump_storage.c`](memfault_port/cc32xx_coredump_storage.c) contains an implementation that will save coredump information to
  internal flash sectors

* [`cc32xxsf_tirtos.cmd`](cc32xxsf_tirtos.cmd) should also be examined for modifications to the linker script that were
  made to export variables defining the ranges of different memory regions
