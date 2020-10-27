//! @file
//!
//! @brief
//! A port for the platform dependencies needed to save coredumps to
//! internal flash on the CC32xx.
//!
//! The storage and ram region addresses collected come from variables
//! defined in the linker script. See "cc32xxsf_tirtos.cmd" for more details.

#include "memfault/panics/platform/coredump.h"


#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "memfault/core/compiler.h"
#include "memfault/core/math.h"

#include <ti/sysbios/family/arm/m3/Hwi.h>
#include <ti/devices/cc32xx/inc/hw_types.h>
#include <ti/devices/cc32xx/driverlib/flash.h>

#define LINKER_REGION_LEN(s, e) ((uint32_t)&e - (uint32_t)&s)

const sMfltCoredumpRegion *memfault_platform_coredump_get_regions(
    const sCoredumpCrashInfo *crash_info, size_t *num_regions) {
   static sMfltCoredumpRegion s_coredump_regions[4];

   // Linker variables that hold the addresses to the regions we to capture
   extern uint32_t __primary_heap_start__;
   extern uint32_t __primary_heap_end__;

   extern uint32_t __data_start__;
   extern uint32_t __data_end__;

   extern uint32_t __bss_start__;
   extern uint32_t __bss_end__;

   // symbols created automatically by linker for ISR stack
   extern uint32_t __STACK_END;
   extern uint32_t __stack;

   const size_t bss_length = LINKER_REGION_LEN(__bss_start__, __bss_end__);
   s_coredump_regions[0] = MEMFAULT_COREDUMP_MEMORY_REGION_INIT(
       &__bss_start__, bss_length);

   const size_t data_length = LINKER_REGION_LEN(__data_start__, __data_end__);
   s_coredump_regions[1] = MEMFAULT_COREDUMP_MEMORY_REGION_INIT(
           &__data_start__, data_length);


   const size_t heap_length = LINKER_REGION_LEN(__primary_heap_start__, __primary_heap_end__);
   s_coredump_regions[2] = MEMFAULT_COREDUMP_MEMORY_REGION_INIT(
           &__primary_heap_start__, heap_length);

   const size_t stack_length = LINKER_REGION_LEN(__stack, __STACK_END);
   s_coredump_regions[3] = MEMFAULT_COREDUMP_MEMORY_REGION_INIT(
           &__stack, stack_length);


   *num_regions = MEMFAULT_ARRAY_SIZE(s_coredump_regions);
   return &s_coredump_regions[0];
}

// Linker variables defined to reserve a flash
// region for Memfault Coredump Storage
extern uint32_t __coredump_storage_start__;
extern uint32_t __coredump_storage_end__;

#define COREDUMP_FLASH_BASE ((uint32_t)&__coredump_storage_start__)
#define COREDUMP_STORAGE_SIZE LINKER_REGION_LEN(__coredump_storage_start__, __coredump_storage_end__)

// The sector sizes for the CC32XX Internal Flash are 2kB
#define COREDUMP_SECTOR_SIZE 2048

void memfault_platform_coredump_storage_get_info(sMfltCoredumpStorageInfo *info) {
  *info = (sMfltCoredumpStorageInfo) {
    .size = COREDUMP_STORAGE_SIZE,
    .sector_size = COREDUMP_SECTOR_SIZE,
  };
}

bool memfault_platform_coredump_storage_read(uint32_t offset, void *data,
                                             size_t read_len) {
  if ((offset + read_len) > COREDUMP_STORAGE_SIZE) {
    return false;
  }

  const uint8_t *read_ptr = (uint8_t *)(COREDUMP_FLASH_BASE + offset);
  memcpy(data, read_ptr, read_len);
  return true;
}

static int prv_program(void *pulData, unsigned long offset,
                       unsigned long ulCount) {
  uint32_t ulAddress = COREDUMP_FLASH_BASE + offset;
  Hwi_disable();
  int rv = FlashProgram(pulData, ulAddress, ulCount);
  Hwi_enable();
  return rv;
}

static int prv_erase_sector(uint32_t sector_offset) {
  Hwi_disable();
  const int rv = FlashErase(COREDUMP_FLASH_BASE + sector_offset);
  Hwi_enable();
  return rv;
}

bool memfault_platform_coredump_storage_erase(uint32_t offset, size_t erase_size) {
  if ((offset + erase_size) > COREDUMP_STORAGE_SIZE ||
      ((erase_size % COREDUMP_SECTOR_SIZE) != 0)) {
    return false;
  }

  long rv = 0;
  for (size_t i = 0; i < erase_size; i += COREDUMP_SECTOR_SIZE) {
    prv_erase_sector(offset + i);
    if (rv != 0) {
      return rv;
    }
  }

  return rv == 0;
}

bool memfault_platform_coredump_save_begin(void) {
  // This gets called prior to any coredump save operation and is where
  // any initialization work can be done.
  return true;
}

bool memfault_platform_coredump_storage_write(uint32_t offset, const void *data,
                                              size_t data_len) {
  if ((offset + data_len) > COREDUMP_STORAGE_SIZE) {
    return false;
  }

  // The CC32xx flash has a couple requirements for writes:
  //
  //  a) Writes must be word aligned
  //  b) Writes must be in multiples of words
  //  c) Attempts to program a bit from 0 -> 1 will error out with
  //     FLASH_CTRL_FCRIS_INVDRIS


  uint32_t curr_offset = offset; // the current offset being written to
  const uint8_t *data_ptr = data;
  size_t read_offset = 0; // the current offset in data_ptr we are reading from

  long rv;

  // if the program operation is not word aligned we need to
  //   1. Read what is currently programmed (to satisfy c) above)
  //   2. Copy the bytes we want to write into the word aligned buffer
  //   3. Write the word
  if ((offset % 4) != 0) {
    const int write_offset = offset % 4;
    const uint32_t word_aligned_offset = curr_offset - write_offset;

    uint8_t word[4];
    memfault_platform_coredump_storage_read(word_aligned_offset, &word, sizeof(word));

    const int length = MEMFAULT_MIN(sizeof(word) - write_offset, data_len);
    memcpy(&word[write_offset], data_ptr, length);

    rv =  prv_program(&word, word_aligned_offset, sizeof(word));
    if (rv != 0) {
      return false;
    }

    read_offset += length;
    data_len -= length;
    curr_offset += length;
  }

  // The address being written to is now word aligned so we can program
  // multiple words at a time
  const size_t bytes_to_write = MEMFAULT_FLOOR(data_len, 4);
  if (bytes_to_write > 0) {
    rv =  prv_program((uint8_t *)&data_ptr[read_offset], curr_offset, bytes_to_write);
    if (rv != 0) {
      return false;
    }

    data_len -= bytes_to_write;
    read_offset += bytes_to_write;
    curr_offset += bytes_to_write;
  }

  // The address being written is word aligned but we have less than 4 bytes to write.
  // We will need to read the current contents, copy in the bytes we want to change,
  // and program the flash just like we did above
  if (data_len != 0) {
    uint8_t word[4];
    memfault_platform_coredump_storage_read(curr_offset, &word, sizeof(word));
    memcpy(&word, &data_ptr[read_offset], data_len);
    rv = prv_program(&word, curr_offset, sizeof(word));
  }
  return rv == 0;
}

// Note: This function is called while the system is running after a coredump has been
// sent to memfault. To clear a coredump so it is not read again we just need to zero
// out the first byte
void memfault_platform_coredump_storage_clear(void) {
  const uint32_t clear_word = 0x0;
  memfault_platform_coredump_storage_write(0, &clear_word, sizeof(clear_word));
}
