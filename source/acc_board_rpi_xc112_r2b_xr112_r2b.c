// Copyright (c) Acconeer AB, 2018
// All rights reserved

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "acc_board.h"
#include "acc_device_gpio.h"
#include "acc_log.h"
#include "acc_os.h"

#if defined(TARGET_OS_android)
#include "acc_driver_gpio_android.h"
#include "acc_driver_i2c_android.h"
#include "acc_driver_spi_android.h"
#include "acc_os_android.h"
#elif defined(TARGET_OS_linux)
#include "acc_driver_gpio_linux_sysfs.h"
#include "acc_driver_i2c_linux.h"
#include "acc_driver_spi_linux_spidev.h"
#include "acc_os_linux.h"
#else
#error "Target operating system is not supported"
#endif



/**
 * @brief The module name
 */
#define MODULE "board_rpi_xc112_r2b"
#define PIN_HIGH		(1)
#define PIN_LOW			(0)

#define SENSOR_COUNT		(4)	/**< @brief The number of sensors available on the board */

#define PIN_PMU_EN 		(17)	/**< @brief PMU_EN BCM:17 J5:11 */

#define PIN_SS_N		(8)	/**< @brief SPI SSn BCM:8 J5:24 */
#define PIN_SPI_ENABLE_S1_N 	(18)	/**< @brief SPI S1 enable BCM:18 J5:12 */
#define PIN_SPI_ENABLE_S2_N 	(27)	/**< @brief SPI S2 enable BCM:27 J5:13 */
#define PIN_SPI_ENABLE_S3_N 	(22)	/**< @brief SPI S3 enable BCM:22 J5:15 */
#define PIN_SPI_ENABLE_S4_N	(7)	/**< @brief SPI S4 enable BCM:7 J5:26 */

#define PIN_ENABLE_N		(6)	/**< @brief Gpio Enable BCM:4 J5:31 */
#define PIN_ENABLE_S1_3V3	(23)	/**< @brief Gpio Enable S1 BCM:23 J5:16 */
#define PIN_ENABLE_S2_3V3	(5)	/**< @brief Gpio Enable S2 BCM:5 J5:29 */
#define PIN_ENABLE_S3_3V3	(12)	/**< @brief Gpio Enable S3 BCM:12 J5:32 */
#define PIN_ENABLE_S4_3V3	(26)	/**< @brief Gpio Enable S4 BCM:26 J5:37 */

#define PIN_SENSOR_INTERRUPT_S1_3V3 (20)	/**< @brief Gpio Interrupt S1 BCM:20 J5:38, connect to sensor 1 GPIO 5 */
#define PIN_SENSOR_INTERRUPT_S2_3V3 (21)	/**< @brief Gpio Interrupt S2 BCM:21 J5:40, connect to sensor 2 GPIO 5 */
#define PIN_SENSOR_INTERRUPT_S3_3V3 (24)	/**< @brief Gpio Interrupt S3 BCM:24 J5:18, connect to sensor 3 GPIO 5 */
#define PIN_SENSOR_INTERRUPT_S4_3V3 (25)	/**< @brief Gpio Interrupt S4 BCM:25 J5:22, connect to sensor 4 GPIO 5 */

#define ACC_BOARD_REF_FREQ	(24000000)	/**< @brief The reference frequency assumes 26 MHz on reference board */
#define ACC_BOARD_SPI_SPEED	(15000000)	/**< @brief The SPI speed of this board */



/**
 * @brief Sensor states
 */
typedef enum {
	SENSOR_DISABLED,
	SENSOR_ENABLED,
	SENSOR_ENABLED_AND_SELECTED
} acc_board_sensor_state_t;

typedef struct {
	acc_board_sensor_state_t state;
	const uint8_t enable_pin;
	const uint8_t interrupt_pin;
	const uint8_t slave_select_pin;
} acc_sensor_pins_t;

static acc_sensor_pins_t sensor_pins[SENSOR_COUNT] = {
	{
		.state=SENSOR_DISABLED,
		.enable_pin=PIN_ENABLE_S1_3V3,
		.interrupt_pin=PIN_SENSOR_INTERRUPT_S1_3V3,
		.slave_select_pin=PIN_SPI_ENABLE_S1_N
	},
	{
		.state=SENSOR_DISABLED,
		.enable_pin=PIN_ENABLE_S2_3V3,
		.interrupt_pin=PIN_SENSOR_INTERRUPT_S2_3V3,
		.slave_select_pin=PIN_SPI_ENABLE_S2_N
	},
	{
		.state=SENSOR_DISABLED,
		.enable_pin=PIN_ENABLE_S3_3V3,
		.interrupt_pin=PIN_SENSOR_INTERRUPT_S3_3V3,
		.slave_select_pin=PIN_SPI_ENABLE_S3_N
	},
	{
		.state=SENSOR_DISABLED,
		.enable_pin=PIN_ENABLE_S4_3V3,
		.interrupt_pin=PIN_SENSOR_INTERRUPT_S4_3V3,
		.slave_select_pin=PIN_SPI_ENABLE_S4_N
	}
};


/**
 * @brief Private function to check if there is at least one active sensor
 *
 * @return True if there is at least one active sensor, false otherwise
 */
static bool any_sensor_active();


acc_status_t acc_board_gpio_init(void)
{
	acc_status_t		status;
	static bool		init_done = false;
	static acc_os_mutex_t	init_mutex = NULL;

	if (init_done) {
		return ACC_STATUS_SUCCESS;
	}

	acc_os_init();
	init_mutex = acc_os_mutex_create();

	acc_os_mutex_lock(init_mutex);
	if (init_done) {
		acc_os_mutex_unlock(init_mutex);
		return ACC_STATUS_SUCCESS;
	}

	/*
	NOTE:
		Observe that initial pull state of PIN_ENABLE_N, PIN_ENABLE_S2_3V3,
		PIN_SS_N, PIN_SPI_ENABLE_S4_N, PIN_I2C_SCL_1 and PIN_I2C_SDA_1 pins are HIGH
		The rest of the pins are LOW
	*/
	if (
		(status = acc_device_gpio_set_initial_pull(PIN_SENSOR_INTERRUPT_S1_3V3, PIN_LOW)) ||
		(status = acc_device_gpio_set_initial_pull(PIN_SENSOR_INTERRUPT_S2_3V3, PIN_LOW)) ||
		(status = acc_device_gpio_set_initial_pull(PIN_SENSOR_INTERRUPT_S3_3V3, PIN_LOW)) ||
		(status = acc_device_gpio_set_initial_pull(PIN_SENSOR_INTERRUPT_S4_3V3, PIN_LOW)) ||
		(status = acc_device_gpio_set_initial_pull(PIN_ENABLE_N, PIN_HIGH)) ||
		(status = acc_device_gpio_set_initial_pull(PIN_ENABLE_S1_3V3, PIN_LOW)) ||
		(status = acc_device_gpio_set_initial_pull(PIN_ENABLE_S2_3V3, PIN_HIGH)) ||
		(status = acc_device_gpio_set_initial_pull(PIN_ENABLE_S3_3V3, PIN_LOW)) ||
		(status = acc_device_gpio_set_initial_pull(PIN_ENABLE_S4_3V3, PIN_LOW)) ||
		(status = acc_device_gpio_set_initial_pull(PIN_SS_N, PIN_HIGH)) ||
		(status = acc_device_gpio_set_initial_pull(PIN_SPI_ENABLE_S1_N, PIN_LOW)) ||
		(status = acc_device_gpio_set_initial_pull(PIN_SPI_ENABLE_S2_N, PIN_LOW)) ||
		(status = acc_device_gpio_set_initial_pull(PIN_SPI_ENABLE_S3_N, PIN_LOW)) ||
		(status = acc_device_gpio_set_initial_pull(PIN_SPI_ENABLE_S4_N, PIN_HIGH)) ||
		(status = acc_device_gpio_set_initial_pull(PIN_PMU_EN, PIN_LOW))
	) {
		ACC_LOG_WARNING("%s: failed to set initial pull with status: %s", __func__, acc_log_status_name(status));
	}
	/*
	NOTE:
		PIN_ENABLE_N is active low and controls the /OE (output enable, active low) of the level shifter).
		The PIN_ENABLE_N is inited two times, first we set it high to disable the chip
		until ENABLE_S1-4 are inited.
		The second time the PIN_ENABLE_N is set low in order for the chip to become enabled.
	*/
	if (
		(status = acc_device_gpio_write(PIN_PMU_EN, PIN_LOW)) ||
		(status = acc_device_gpio_write(PIN_ENABLE_N, PIN_HIGH)) ||
		(status = acc_device_gpio_write(PIN_SS_N, PIN_HIGH)) ||
		(status = acc_device_gpio_input(PIN_SENSOR_INTERRUPT_S1_3V3)) ||
		(status = acc_device_gpio_input(PIN_SENSOR_INTERRUPT_S2_3V3)) ||
		(status = acc_device_gpio_input(PIN_SENSOR_INTERRUPT_S3_3V3)) ||
		(status = acc_device_gpio_input(PIN_SENSOR_INTERRUPT_S4_3V3)) ||
		(status = acc_device_gpio_write(PIN_ENABLE_S1_3V3, PIN_LOW)) ||
		(status = acc_device_gpio_write(PIN_ENABLE_S2_3V3, PIN_LOW)) ||
		(status = acc_device_gpio_write(PIN_ENABLE_S3_3V3, PIN_LOW)) ||
		(status = acc_device_gpio_write(PIN_ENABLE_S4_3V3, PIN_LOW)) ||
		(status = acc_device_gpio_write(PIN_SPI_ENABLE_S1_N, PIN_HIGH)) ||
		(status = acc_device_gpio_write(PIN_SPI_ENABLE_S2_N, PIN_HIGH)) ||
		(status = acc_device_gpio_write(PIN_SPI_ENABLE_S3_N, PIN_HIGH)) ||
		(status = acc_device_gpio_write(PIN_SPI_ENABLE_S4_N, PIN_HIGH))
	) {
		ACC_LOG_ERROR("%s failed with %s", __func__, acc_log_status_name(status));
		acc_os_mutex_unlock(init_mutex);
		return status;
	}

	init_done = true;
	acc_os_mutex_unlock(init_mutex);

	return ACC_STATUS_SUCCESS;
}


acc_status_t acc_board_init(void)
{
	static bool		init_done = false;
	static acc_os_mutex_t	init_mutex = NULL;

	if (init_done) {
		return ACC_STATUS_SUCCESS;
	}

#if defined(TARGET_OS_android)
	acc_driver_os_android_register();
#elif defined(TARGET_OS_linux)
	acc_driver_os_linux_register();
#endif

	acc_os_init();
	init_mutex = acc_os_mutex_create();

	acc_os_mutex_lock(init_mutex);
	if (init_done) {
		acc_os_mutex_unlock(init_mutex);
		return ACC_STATUS_SUCCESS;
	}
#if defined(TARGET_OS_android)
	acc_driver_gpio_android_register(28);
	acc_driver_spi_android_register();
	/* NOTE: The i2c driver for Android is not yet implemented, will return "unsupported" */
	acc_driver_i2c_android_register();
#elif defined(TARGET_OS_linux)
	acc_driver_gpio_linux_sysfs_register(28);
	acc_driver_spi_linux_spidev_register();
	/* i2c driver and device is connected to the eeprom on the board */
	acc_driver_i2c_linux_register();
#else
#error "Target operating system not supported"
#endif

	init_done = true;
	acc_os_mutex_unlock(init_mutex);

	return ACC_STATUS_SUCCESS;
}


static bool any_sensor_active(){
	for (uint_fast8_t i = 0; i < SENSOR_COUNT; i++) {
		if (sensor_pins[i].state != SENSOR_DISABLED) {
			return true;
		}
	}
	return false;
}


acc_status_t acc_board_start_sensor(acc_sensor_t sensor)
{
	acc_status_t status = ACC_STATUS_FAILURE;
	acc_sensor_pins_t *p_sensor = &sensor_pins[sensor - 1];

	if (p_sensor->state != SENSOR_DISABLED) {
		ACC_LOG_ERROR("Sensor %"PRIsensor" already enabled.", sensor);
		return status;
	}

	if (!any_sensor_active()) {
		// No active sensors yet, set pmu high to start the board
		status = acc_device_gpio_write(PIN_PMU_EN, PIN_HIGH);
		if (status != ACC_STATUS_SUCCESS) {
			ACC_LOG_ERROR("Couldn't enable pmu for sensor %"PRIsensor, sensor);
			return status;
		}
		// Wait for the board to power up
		acc_os_sleep_us(5000);
		status = acc_device_gpio_write(PIN_ENABLE_N, PIN_LOW);
		if (status != ACC_STATUS_SUCCESS) {
			ACC_LOG_ERROR("Couldn't set enable to low for sensor %"PRIsensor, sensor);
			return status;
		}
		acc_os_sleep_us(5000);
	}

	status = acc_device_gpio_write(p_sensor->enable_pin, PIN_HIGH);
	if (status != ACC_STATUS_SUCCESS) {
		ACC_LOG_ERROR("Unable to activate ENABLE on sensor %"PRIsensor, sensor);
		return status;
	}
	acc_os_sleep_us(5000);

	p_sensor->state = SENSOR_ENABLED;

	return status;
}


acc_status_t acc_board_stop_sensor(acc_sensor_t sensor)
{
	acc_status_t status = ACC_STATUS_FAILURE;
	acc_sensor_pins_t *p_sensor = &sensor_pins[sensor - 1];

	if (p_sensor->state != SENSOR_DISABLED) {
		// "unselect" spi slave select
		if (p_sensor->state == SENSOR_ENABLED_AND_SELECTED) {
			status = acc_device_gpio_write(p_sensor->slave_select_pin, PIN_HIGH);
			if (status != ACC_STATUS_SUCCESS) {
				ACC_LOG_ERROR("Failed to deselect sensor %"PRIsensor, sensor);
				return status;
			}
		}

		// Disable sensor
		status = acc_device_gpio_write(p_sensor->enable_pin, PIN_LOW);
		if (status != ACC_STATUS_SUCCESS) {
			// Set the state to enabled since it is not selected and failed to disable
			p_sensor->state = SENSOR_ENABLED;
			ACC_LOG_ERROR("Unable to deactivate ENABLE on sensor %"PRIsensor, sensor);
			return status;
		}
		p_sensor->state = SENSOR_DISABLED;
	} else {
		ACC_LOG_ERROR("Sensor %"PRIsensor" already inactive", sensor);
	}
	if (!any_sensor_active()) {
		// No active sensors, shut down the board to save power
		acc_device_gpio_write(PIN_ENABLE_N, PIN_HIGH);
		acc_device_gpio_write(PIN_PMU_EN, PIN_LOW);
	}

	return status;
}


void acc_board_get_spi_bus_cs(acc_sensor_t sensor, uint_fast8_t *bus, uint_fast8_t *cs)
{
	if ((sensor == 0) || (sensor > SENSOR_COUNT)) {
		*bus = 0xff;
		*cs  = 0xff;
	} else {
		*bus = 0; /* The SPI bus all sensors are using */
		*cs  = 0; /* The SPI CS all sensors are using */
	}
}


acc_status_t acc_board_chip_select(acc_sensor_t sensor, uint_fast8_t cs_assert)
{
	acc_status_t status = ACC_STATUS_FAILURE;
	acc_sensor_pins_t *p_sensor = &sensor_pins[sensor - 1];

	if (cs_assert) {
		if (p_sensor->state == SENSOR_ENABLED) {
			// Since only one sensor can be active, loop through all the other sensors and deselect the active one
			for (uint_fast8_t i = 0; i < SENSOR_COUNT; i++) {
				if ((i != (sensor - 1)) && (sensor_pins[i].state == SENSOR_ENABLED_AND_SELECTED)) {
					status = acc_device_gpio_write(sensor_pins[i].slave_select_pin, PIN_HIGH);
					if (status != ACC_STATUS_SUCCESS) {
						ACC_LOG_ERROR("Failed to deselect sensor %"PRIsensor", status %d", sensor, status);
						return ACC_STATUS_FAILURE;
					}
					sensor_pins[i].state = SENSOR_ENABLED;
				}
			}

			// Select the sensor
			status = acc_device_gpio_write(p_sensor->slave_select_pin, PIN_LOW);
			if (status != ACC_STATUS_SUCCESS) {
				ACC_LOG_ERROR("Failed to select sensor %"PRIsensor", status %d", sensor, status);
				return status;
			}
			p_sensor->state = SENSOR_ENABLED_AND_SELECTED;

			status = ACC_STATUS_SUCCESS;
		}
		else if (p_sensor->state == SENSOR_DISABLED) {
			ACC_LOG_ERROR("Failed to select sensor %"PRIsensor", it is disabled", sensor);
		}
		else if (p_sensor->state == SENSOR_ENABLED_AND_SELECTED) {
			ACC_LOG_DEBUG("Sensor %"PRIsensor" already selected", sensor);
			status = ACC_STATUS_SUCCESS;
		}
		else {
			ACC_LOG_ERROR("Unknown state when selecting sensor %"PRIsensor, sensor);
		}

		return status;
	} else {
		if (p_sensor->state == SENSOR_ENABLED_AND_SELECTED) {
			status = acc_device_gpio_write(p_sensor->slave_select_pin, PIN_HIGH);
			if (status != ACC_STATUS_SUCCESS) {
				ACC_LOG_ERROR("Failed to deselect sensor %"PRIsensor", status %d", sensor, status);
				return status;
			}
			p_sensor->state = SENSOR_ENABLED;
		}
	}

	return ACC_STATUS_SUCCESS;
}


acc_sensor_t acc_board_get_sensor_count(void)
{
	return SENSOR_COUNT;
}


acc_status_t acc_board_register_isr(acc_board_isr_t isr)
{
	ACC_UNUSED(isr);

	return ACC_STATUS_UNSUPPORTED;
}


bool acc_board_is_sensor_interrupt_connected(acc_sensor_t sensor)
{
	ACC_UNUSED(sensor);

	return true;
}


bool acc_board_is_sensor_interrupt_active(acc_sensor_t sensor)
{
	acc_status_t status;
	uint_fast8_t value;

	status = acc_device_gpio_read(sensor_pins[sensor - 1].interrupt_pin, &value);
	if (status != ACC_STATUS_SUCCESS) {
		ACC_LOG_ERROR("Could not obtain GPIO interrupt value for sensor %"PRIsensor" with status: %s.", sensor, acc_log_status_name(status));
		return false;
	}

	return value != 0;
}


float acc_board_get_ref_freq(void)
{
	return ACC_BOARD_REF_FREQ;
}


uint32_t acc_board_get_spi_speed(uint_fast8_t bus)
{
	ACC_UNUSED(bus);

	return ACC_BOARD_SPI_SPEED;
}


acc_status_t acc_board_set_ref_freq(float ref_freq)
{
	ACC_UNUSED(ref_freq);

	return ACC_STATUS_UNSUPPORTED;
}
