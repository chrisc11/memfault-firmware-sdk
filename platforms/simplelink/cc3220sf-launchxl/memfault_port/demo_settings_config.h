#pragma once

//! Note, the following variables need to be filled in for the application to compile
// #define SSID_NAME "network"
// #define SECURITY_KEY "password"
// #define MEMFAULT_PROJECT_API_KEY "project_api_key"


#if !defined(SSID_NAME) || !defined(SECURITY_KEY) || !defined(MEMFAULT_PROJECT_API_KEY)
#error "SSID_NAME, SECURITY_KEY, & MEMFAULT_PROJECT_API_KEY must be defined in demo_settings_config.h"
#endif
