// Copyright (c) Acconeer AB, 2018
// All rights reserved

#ifndef ACC_RSS_H_
#define ACC_RSS_H_

#include <stdbool.h>

#include "acc_definitions.h"


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup RSS Radar System Services, RSS
 *
 * @brief Acconeer Radar System Services, RSS
 *
 * @{
 */


/**
 * @brief Activate the Acconeer Radar System Services, RSS
 *
 * The call to this function must only be made from one thread. After this, full thread safety across
 * all Acconeer services is guaranteed.
 *
 * @return True if RSS is activated
 */
extern bool acc_rss_activate(void);


/**
 * @brief Activate the Acconeer Radar System Services, RSS, with HAL
 *
 * The call to this function must only be made from one thread. After this, full thread safety across
 * all Acconeer services is guaranteed.
 *
 * @return True if RSS is activated
 */
extern bool acc_rss_activate_with_hal(acc_hal_t *hal);


/**
 * @brief Deactivate the Acconeer Radar System Services, RSS
 */
extern void acc_rss_deactivate(void);


/**
 * @brief Get the Acconeer RSS version
 *
 * @return Version
 */
extern const char *acc_rss_version(void);


/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif
