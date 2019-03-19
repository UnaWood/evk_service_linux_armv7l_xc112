// Copyright (c) Acconeer AB, 2018
// All rights reserved

#ifndef ACC_DEFINITIONS_H_
#define ACC_DEFINITIONS_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef uint32_t acc_sensor_id_t;


/**
 * @brief Definition of a sensor control function
 */
typedef bool (*acc_hal_sensor_control_function_t)(acc_sensor_id_t sensor_id);


/**
 * @brief Definition of a hal get frequency function
 */
typedef float (*acc_hal_get_frequency_function_t)(void);


/**
 * @brief Definition of a sensor interrupt service routine (ISR)
 */
typedef void (*acc_hal_sensor_isr_t)(acc_sensor_id_t sensor_id);


typedef enum
{
	ACC_HAL_REGISTER_ISR_STATUS_OK,
	ACC_HAL_REGISTER_ISR_STATUS_UNSUPPORTED,
	ACC_HAL_REGISTER_ISR_STATUS_FAILURE
} acc_hal_register_isr_status_t;


/**
 * @brief Definition of a sensor isr register function
 */
typedef acc_hal_register_isr_status_t (*acc_hal_sensor_register_isr_function_t)(acc_hal_sensor_isr_t isr);


/**
 * @brief Definition of a sensor transfer function
 */
typedef bool (*acc_hal_sensor_transfer_function_t)(acc_sensor_id_t sensor_id, uint8_t *buffer, size_t buffer_size);


typedef struct
{
	acc_hal_sensor_control_function_t        power_on;
	acc_hal_sensor_control_function_t        power_off;
	acc_hal_sensor_control_function_t        is_interrupt_connected;
	acc_hal_sensor_control_function_t        is_interrupt_active;
	acc_hal_sensor_register_isr_function_t   register_isr;
	acc_hal_sensor_transfer_function_t       transfer;
	acc_hal_get_frequency_function_t         get_reference_frequency;
} acc_hal_sensor_device_t;


typedef struct
{
	acc_sensor_id_t         sensor_count;
	size_t                  max_spi_transfer_size;
} acc_hal_properties_t;


typedef struct
{
	acc_hal_sensor_device_t   sensor_device;
	acc_hal_properties_t      properties;
} acc_hal_t;


typedef struct acc_context
{
	void *data;
	uint32_t data_size;
} acc_context_s;


/**
 * @brief RSS context container where serialized data will be stored
 */
typedef struct acc_context *acc_context_t;


#ifdef __cplusplus
}
#endif

#endif
