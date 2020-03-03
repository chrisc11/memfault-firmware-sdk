#pragma once

//! @file
//!
//! Copyright (c) Memfault, Inc.
//! See License.txt for details
//!
//! A utility that implements a small subset of the CBOR RFC:
//!  https://tools.ietf.org/html/rfc7049#section-3.7
//!

//! CONTEXT: The Memfault metric events API serializes data out to CBOR. Since the actual CBOR
//! serialization feature set needed by the SDK is a tiny subset of the CBOR RFC, a minimal
//! implementation is implemented here.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//! The context used to track an active cbor encoding operation
//! A consumer of this API should never have to access the structure directly
typedef struct MemfaultCborEncoder sMemfaultCborEncoder;

//! The backing storage to write the encoded data to
//!
//! @param ctx The context provided as part of "memfault_cbor_encoder_init"

//! @param offset The offset within the storage to write to. These offsets are guaranteed to be
//!   sequential. (i.e If the last write wrote 3 bytes at offset 0, the next write_cb will begin at
//!   offset 3) The offset is returned as a convenience. For example if the backing storage is a RAM
//! buffer with no state-tracking of it's own
//! @param buf The payload to write to the storage
//! @param buf_len The size of the payload to write
typedef void (MemfaultCborWriteCallback)(void *ctx, uint32_t offset, const void *buf, size_t buf_len);

//! Initializes the 'encoder' structure. Must be called at the start of any new encoding
//!
//! @param encoder Context structure initialized by this routine for tracking active encoding state
//! @param write_cb The callback to be invoked when a write needs to be performed
//! @param context The context to be provided along with the write_cb
//! @param buf_len The space free in the backing storage being encoded to. The encoder API
//!  will _never_ attempt to write more bytes than this
void memfault_cbor_encoder_init(sMemfaultCborEncoder *encoder, MemfaultCborWriteCallback *cb,
                                void *context, size_t buf_len);

//! Same as "memfault_cbor_encoder_init" but instead of encoding to a buffer will
//! only set the encoder up to compute the total size of the encode
//!
//! When encoding is done and "memfault_cbort_encoder_deinit" is called the total
//! encoding size will be returned
void memfault_cbor_encoder_size_only_init(sMemfaultCborEncoder *encoder);

//! Resets the state of the encoder context
//!
//! @return the number of bytes successfully encoded
size_t memfault_cbor_encoder_deinit(sMemfaultCborEncoder *encoder);

//! Called to begin the encoding of a dictionary (also known as a map, object, hashes)
//!
//! @param encoder The encoder context to use
//! @param num_elements The number of pairs of data items that will be in the dictionary
//!
//! @return true on success, false otherwise
bool memfault_cbor_encode_dictionary_begin(sMemfaultCborEncoder *encoder, size_t num_elements);


//! Called to begin the encoding of an array (also referred to as a list, sequence, or tuple)
//!
//! @param encoder The encoder context to use
//! @param num_elements The number of data items that will be in the array
//!
//! @return true on success, false otherwise
bool memfault_cbor_encode_array_begin(sMemfaultCborEncoder *encoder, size_t num_elements);

//! Called to encode an unsigned 32-bit integer data item
//!
//! @param encoder The encoder context to use
//! @param value The value to store
//!
//! @return true on success, false otherwise
bool memfault_cbor_encode_unsigned_integer(sMemfaultCborEncoder *encoder, uint32_t value);

//! Same as "memfault_cbor_encode_unsigned_integer" but store an unsigned integer instead
bool memfault_cbor_encode_signed_integer(sMemfaultCborEncoder *encoder, int32_t value);

//! Called to encode an arbitrary binary payload
//!
//! @param encoder The encoder context to use
//! @param buf The buffer to store
//! @param buf_len The length of the buffer to store
//!
//! @return true on success, false otherwise
bool memfault_cbor_encode_byte_string(sMemfaultCborEncoder *encoder, const void *buf,
                                      size_t buf_len);

//! Called to encode a NUL terminated C string
//!
//! @param encoder The encoder context to use
//! @param str The string to store
//!
//! @return true on success, false otherwise
bool memfault_cbor_encode_string(sMemfaultCborEncoder *encoder, const char *str);


//! NOTE: For internal use only, included in the header so it's easy for a caller to statically
//! allocate the structure
struct MemfaultCborEncoder {
  bool compute_size_only;
  MemfaultCborWriteCallback *write_cb;
  void *write_cb_ctx;
  size_t buf_len;

  size_t encoded_size;
};

#ifdef __cplusplus
}
#endif
