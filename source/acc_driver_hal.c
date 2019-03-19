// Copyright (c) Acconeer AB, 2018
// All rights reserved

#include <stdbool.h>
#include <stdint.h>

#include "acc_driver_hal.h"

#include "acc_board.h"
#include "acc_definitions.h"
#include "acc_device_spi.h"
#include "acc_log.h"
#include "acc_types.h"


#define MODULE "driver_hal"

//-----------------------------
// Private declarations
//-----------------------------

static bool sensor_power_on(acc_sensor_id_t sensor_id);
static bool sensor_power_off(acc_sensor_id_t sensor_id);
static acc_hal_register_isr_status_t sensor_register_isr(acc_hal_sensor_isr_t isr);
static bool sensor_transfer(acc_sensor_id_t sensor_id, uint8_t *buffer, size_t buffer_size);

//-----------------------------
// Public definitions
//-----------------------------

bool acc_driver_hal_init(void)
{
	if (acc_board_init() != ACC_STATUS_SUCCESS)
	{
		return false;
	}

	if (acc_board_gpio_init() != ACC_STATUS_SUCCESS)
	{
		return false;
	}

	return true;
}


acc_hal_t acc_driver_hal_get_implementation(void)
{
	acc_hal_t hal;

	hal.properties.sensor_count = acc_board_get_sensor_count();
	hal.properties.max_spi_transfer_size = acc_device_spi_get_max_transfer_size();

	hal.sensor_device.power_on = sensor_power_on;
	hal.sensor_device.power_off = sensor_power_off;
	hal.sensor_device.is_interrupt_connected = acc_board_is_sensor_interrupt_connected;
	hal.sensor_device.is_interrupt_active = acc_board_is_sensor_interrupt_active;
	hal.sensor_device.register_isr = sensor_register_isr;
	hal.sensor_device.transfer = sensor_transfer;
	hal.sensor_device.get_reference_frequency = acc_board_get_ref_freq;

	return hal;
}


//-----------------------------
// Private definitions
//-----------------------------

bool sensor_power_on(acc_sensor_id_t sensor_id)
{
	return acc_board_start_sensor(sensor_id) == ACC_STATUS_SUCCESS;
}


bool sensor_power_off(acc_sensor_id_t sensor_id)
{
	return acc_board_stop_sensor(sensor_id) == ACC_STATUS_SUCCESS;
}


acc_hal_register_isr_status_t sensor_register_isr(acc_hal_sensor_isr_t isr)
{
	acc_status_t status = acc_board_register_isr(isr);

	if (status == ACC_STATUS_SUCCESS)
	{
		return ACC_HAL_REGISTER_ISR_STATUS_OK;
	}
	else if (status == ACC_STATUS_UNSUPPORTED)
	{
		return ACC_HAL_REGISTER_ISR_STATUS_UNSUPPORTED;
	}
	else
	{
		return ACC_HAL_REGISTER_ISR_STATUS_FAILURE;
	}
}


bool sensor_transfer(acc_sensor_id_t sensor_id, uint8_t *buffer, size_t buffer_size)
{
	acc_status_t status;
	uint_fast8_t spi_bus;
	uint_fast8_t spi_device;
	uint32_t     spi_speed;

	acc_board_get_spi_bus_cs(sensor_id, &spi_bus, &spi_device);
	spi_speed = acc_board_get_spi_speed(spi_bus);

	acc_device_spi_lock(spi_bus);

	status = acc_board_chip_select(sensor_id, 1);

	if (status != ACC_STATUS_SUCCESS) {
		ACC_LOG_ERROR("%s failed with %s", __func__, acc_log_status_name(status));
		acc_device_spi_unlock(spi_bus);
		return false;
	}

	status = acc_device_spi_transfer(spi_bus, spi_device, spi_speed, buffer, buffer_size);

	if (status != ACC_STATUS_SUCCESS) {
		acc_device_spi_unlock(spi_bus);
		return false;
	}

	status = acc_board_chip_select(sensor_id, 0);

	if (status != ACC_STATUS_SUCCESS) {
		acc_device_spi_unlock(spi_bus);
		return false;
	}

	acc_device_spi_unlock(spi_bus);

	return true;
}
