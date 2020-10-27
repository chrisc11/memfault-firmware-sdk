/*
 * Copyright (c) 2015-2017, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ======== httpget.c ========
 *  HTTP Client GET example application
 */

/* BSD support */
#include "string.h"
#include <ti/display/Display.h>
#include <ti/net/http/httpclient.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/drivers/net/wifi/fs.h>
#include "semaphore.h"

#include "memfault/core/data_packetizer.h"
#include "memfault/core/debug_log.h"
#include "memfault/core/platform/device_info.h"
#include "memfault/http/root_certs.h"
#include "memfault/core/trace_event.h"
#include "memfault/metrics/metrics.h"

#include "demo_settings_config.h"

#define HOSTNAME              "https://chunks.memfault.com"
#define USER_AGENT            "HTTPClient (ARM; TI-RTOS)"
#define HTTP_MIN_RECV         (256)

#define LOG_HTTP_ERROR(rv) \
  do { \
    MEMFAULT_LOG_ERROR("%s: %d rv=%d", __FILE__, __LINE__, rv); \
    MEMFAULT_TRACE_EVENT_WITH_STATUS(HttpError, rv); \
  } while (0)


//! Installs certificates required to communicate with memfault services to user flash
//!
//! The certs can be found in "memfault/http/root_certs.h". If certificates have already been
//! installed with the Uniflash tool this step is not necessary.
static void prv_install_root_certs_if_necessary(void) {
  #define MEMFAULT_CERT "mflt-root-certs-v0.pem"
  char*           DeviceFileName = MEMFAULT_CERT;
  unsigned long   MaxSize = 63 * 1024; //62.5K is max file size
  unsigned long   Offset = 0;
  int             rv = 0;
  _u32            MasterToken = 0;

  _i32 fd = sl_FsOpen((unsigned char *)DeviceFileName, SL_FS_READ, &MasterToken);
  if (fd >= 0) {
    MEMFAULT_LOG_DEBUG("Memfault Root CA certs found");
    goto cleanup;
  }
  MEMFAULT_LOG_DEBUG("Handle code: %d", fd);

  MEMFAULT_LOG_INFO("Installing Memfault Root CA certs to %s", MEMFAULT_CERT);
  // create a secure file if not exists and open it for write.
  fd = sl_FsOpen((unsigned char *)DeviceFileName,
                 SL_FS_CREATE | SL_FS_OVERWRITE |
                 SL_FS_CREATE_NOSIGNATURE | SL_FS_CREATE_MAX_SIZE(MaxSize),
                 &MasterToken);

  Offset = 0;
  rv = sl_FsWrite(fd, Offset, MEMFAULT_ROOT_CERTS_PEM, sizeof(MEMFAULT_ROOT_CERTS_PEM));
  if (rv < 0) {
    MEMFAULT_LOG_WARN("Cert write failed: %d", rv);
  }

cleanup:
  sl_FsClose(fd, NULL, NULL, 0);
}


static int prv_post_memfault_chunk_data( HTTPClient_Handle httpClientHandle) {
  sMemfaultDeviceInfo device_info;
  memfault_platform_get_device_info(&device_info);

  char request_uri[32] = "/api/v0/chunks/";
  strncat(request_uri, device_info.device_serial, sizeof(request_uri));

  // drain data collected by memfault and post to chunks.memfault.com
  while (1) {
    static char s_chunk_buf[1024];
    size_t buf_len = sizeof(s_chunk_buf);
    bool data_available =
        memfault_packetizer_get_chunk(s_chunk_buf, &buf_len);
    if (!data_available) {
      return 0;
    }

    int rv = HTTPClient_sendRequest(httpClientHandle, HTTP_METHOD_POST,
                                 request_uri, s_chunk_buf, buf_len, 0);
    if (rv != 202) {
      LOG_HTTP_ERROR(rv);
      return rv;
    }
    MEMFAULT_LOG_DEBUG("Successfully posted %d bytes", (int)buf_len);
  }
  return 0;
}


// A simple task loop that periodically checks to see if there is Memfault data to send
//
// If there is data to send, we open a connection to Memfault servers and post the data.
void* httpTask(void* pvParameters) {
  prv_install_root_certs_if_necessary();

  while (1) {
    if (!memfault_packetizer_data_available()) {
      Task_sleep(10000);
      continue;
    }

    MEMFAULT_LOG_DEBUG("Found Memfault Data ... Sending");

    memfault_metrics_heartbeat_timer_start(MEMFAULT_METRICS_KEY(HttpSendTimeMs));


    int16_t statusCode;
    HTTPClient_Handle httpClientHandle = HTTPClient_create(&statusCode, 0);
    if (statusCode < 0) {
      LOG_HTTP_ERROR(statusCode);
    }

    // Set the "User-Agent" http request header
    int16_t ret = HTTPClient_setHeader(
        httpClientHandle, HTTPClient_HFIELD_REQ_USER_AGENT, USER_AGENT,
        strlen(USER_AGENT) + 1, HTTPClient_HFIELD_PERSISTENT);
    if (ret < 0) {
      LOG_HTTP_ERROR(ret);
      break;
    }

    // Set the appropriate "Content-Type" expected by Memfault Backend
    char *content_type = "application/octet-stream";
    ret = HTTPClient_setHeader(
        httpClientHandle, HTTPClient_HFIELD_REQ_CONTENT_TYPE, content_type,
        strlen(content_type) + 1, HTTPClient_HFIELD_PERSISTENT);
    if (ret < 0) {
      LOG_HTTP_ERROR(ret);
      break;
    }

    // Set "Memfault-Project-Key" which is used for device authentication
    ret = HTTPClient_setHeaderByName(
        httpClientHandle, HTTPClient_REQUEST_HEADER_MASK,
        "Memfault-Project-Key", MEMFAULT_PROJECT_API_KEY,
        strlen(MEMFAULT_PROJECT_API_KEY) + 1, HTTPClient_HFIELD_PERSISTENT);
    if (ret < 0) {
      LOG_HTTP_ERROR(ret);
      break;
    }

    HTTPClient_extSecParams tls_params = {
        .rootCa = MEMFAULT_CERT,
    };

    ret = HTTPClient_connect(httpClientHandle, HOSTNAME, &tls_params, 0);
    if (ret < 0) {
      LOG_HTTP_ERROR(ret);
      break;
    }

    if (prv_post_memfault_chunk_data(httpClientHandle) != 0) {
      break;
    }

    ret = HTTPClient_disconnect(httpClientHandle);
    if (ret < 0) {
      LOG_HTTP_ERROR(ret);
      break;
    }

    HTTPClient_destroy(httpClientHandle);
    memfault_metrics_heartbeat_timer_stop(MEMFAULT_METRICS_KEY(HttpSendTimeMs));
  }

  memfault_metrics_heartbeat_timer_stop(MEMFAULT_METRICS_KEY(HttpSendTimeMs));
  return 0;
}
