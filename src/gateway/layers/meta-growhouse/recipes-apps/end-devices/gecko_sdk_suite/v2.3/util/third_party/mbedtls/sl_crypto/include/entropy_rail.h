/**
 *  \file entropy_rail.h
 *
 *  \brief Collect entropy from the RAIL API on Silicon Labs radios.
 *
 *  Copyright (C) 2018, Silicon Labs, http://www.silabs.com
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */
#ifndef MBEDTLS_ENTROPY_RAIL_H
#define MBEDTLS_ENTROPY_RAIL_H

#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif

#include "em_device.h"

#if defined(MBEDTLS_ENTROPY_RAIL_C)

#include "mbedtls/entropy.h"

/***************************************************************************//**
 * \addtogroup rng_module
 * \{
 ******************************************************************************/

/***************************************************************************//**
 * \addtogroup sl_crypto_entropy_rail Silicon Labs RAIL Entropy Source Plugin
 * \brief Collect entropy from the RAIL on Silicon Labs devices.
 *
 * \details The RAIL entropy module implements an entropy source plugin module
 * for mbedTLS that can be used in applications needing random numbers or
 * indirectly using mbedTLS modules that depend on the random number generation
 * interfaces of mbed TLS.
 * The configuration #define @ref MBEDTLS_ENTROPY_RAIL_C will compile the this
 * module.
 * The configuration #defines @ref MBEDTLS_ENTROPY_RAIL_C &&
 * @ref MBEDTLS_ENTROPY_HARDWARE_ALT &&
 * @ref MBEDTLS_ENTROPY_HARDWARE_ALT_RAIL will compile mbedtls_hardware_poll
 * function implemented in this module. This will automatically register the
 * rail entropy module as the default hardware entropy source during runtime
 * initialization.
 *
 * \note
 * The user is responsible for initializing the radio properly before calling
 * into mbedtls API functions that will use the RAIL entropy module. Normally
 * the radio initialization is performed by calling the RAIL API. The wireless
 * stacks from Silicon Labs perform radio initialization internally, in which
 * case the user should just make sure the wireless stacks initializes before
 * user code can call into mbedtls API functions that will use the RAIL entropy
 * module.
 *
 * \{
 ******************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief          RAIL entropy context structure
 */
typedef struct
{
  /* Unused handle (since generic RAIL_EFR32_HANDLE is used).
     Keep it here because compilers require non-empty structs. */
  void *handle;
}
mbedtls_entropy_rail_context;

/**
 * \brief          Initialize a RAIL entropy context
 *
 * \details        This function will initialize a RAIL entropy context.
 *
 * \param ctx      RAIL entropy context to be initialized
 */
void mbedtls_entropy_rail_init( mbedtls_entropy_rail_context *ctx );

/**
 * \brief          Free RAIL entropy context
 *
 * \details        This function will clear the RAIL entropy context.
 *
 * \param ctx      RAIL entropy context to be clear.
 */
void mbedtls_entropy_rail_free( mbedtls_entropy_rail_context *ctx );

/**
 * \brief           Poll for entropy data
 *
 * \details         This function will request entropy data from the RAIL API
 *                  and place it into the output buffer. The len parameter
 *                  tells this function the maximum number of bytes to read.
 *
 *                  Note that the number of bytes read might differ
 *                  from the number of bytes requested.
 *
 *                  The return value should be used to see if the operation was
 *                  successful of if an error was encountered. The content of
 *                  the olen parameter can be used to check
 *                  how many bytes were actually read.
 *
 * \param ctx       RAIL entropy context
 * \param output    Buffer to fill with entropy data from RAIL
 * \param len       Maximum number of bytes to fill in output buffer.
 * \param olen      The actual amount of bytes put into the buffer (Can be 0)
 *
 * \return          0 for success, or MBEDTLS_ERR_ENTROPY_SOURCE_FAILED if
 *                  len is higher than the max RAIL entropy buffer size.
 */
int mbedtls_entropy_rail_poll( mbedtls_entropy_rail_context *ctx,
			      unsigned char *output, size_t len, size_t *olen );

#ifdef __cplusplus
}
#endif

#endif /* MBEDTLS_ENTROPY_RAIL_C */

/** \} (end addtogroup sl_crypto_entropy_rail) */
/** \} (end addtogroup rng_module) */

#endif /* MBEDTLS_ENTROPY_RAIL_H */
