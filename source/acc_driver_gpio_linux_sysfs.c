// Copyright (c) Acconeer AB, 2016-2018
// All rights reserved

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "acc_driver_gpio_linux_sysfs.h"
#include "acc_device_gpio.h"
#include "acc_log.h"
#include "acc_os.h"
#include "acc_types.h"


/**
 * @brief The module name
 */
#define MODULE		"driver_gpio_linux_sysfs"

/**
 * @brief Paths to the GPIO sysfs files
 */
/**@{*/
#define GPIO_EXPORT_PATH		"/sys/class/gpio/export"
#define GPIO_UNEXPORT_PATH		"/sys/class/gpio/unexport"
#define GPIO_DIRECTION_PATH		"/sys/class/gpio/gpio%u/direction"
#define GPIO_VALUE_PATH			"/sys/class/gpio/gpio%u/value"
#define GPIO_EDGE_PATH			"/sys/class/gpio/gpio%u/edge"
/**@}*/

/**
 * @brief GPIO pin direction
 */
typedef enum
{
	GPIO_DIR_IN,
	GPIO_DIR_OUT,
	GPIO_DIR_UNKNOWN
} gpio_dir_enum_t;

typedef uint32_t gpio_dir_t;

/**
 * @brief GPIO pin information
 */
typedef struct
{
	bool			is_open;
	uint_fast8_t		pin;
	int			dir_fd;
	int			value_fd;
	gpio_dir_t		dir;
	int_fast8_t		value;
	uint_fast8_t		pull;
	acc_os_mutex_t		mutex;
	acc_os_thread_handle_t	handle;
	acc_device_gpio_isr_t		isr;
} gpio_t;


/**
 * @brief Array with information on GPIOs, allocated at runtime
 */
static gpio_t	*gpios;

/**
 * @brief Number of GPIO pins supported by the sysfs interface
 */
static uint_fast8_t	gpio_count;


/**
 * @brief Internal GPIO open
 *
 * Export GPIO to create /sys/class/gpio/gpio#
 * Create /sys/class/gpio/gpio#/value and /sys/class/gpio/gpio#/direction.
 *
 * @param pin GPIO pin
 * @return Status
 */
static acc_status_t internal_gpio_open(uint_fast8_t pin)
{
	int	unexport_fd;
	int	export_fd;
	char	gpio_x[5];
	ssize_t	gpio_x_len;
	ssize_t	bytes_written;
	char	dir_path[sizeof(GPIO_DIRECTION_PATH) + 10];
	char	value_path[sizeof(GPIO_VALUE_PATH) + 10];

	if (pin >= gpio_count)
	{
		ACC_LOG_ERROR("GPIO %" PRIuFAST8 " is not a valid GPIO pin", pin);
		return ACC_STATUS_BAD_PARAM;
	}

	gpio_t *gpio = &gpios[pin];

	if (gpio->is_open)
	{
		return ACC_STATUS_SUCCESS;
	}

	gpio_x_len = snprintf(gpio_x, sizeof(gpio_x), "%" PRIuFAST8, pin);
	snprintf(dir_path, sizeof(dir_path), GPIO_DIRECTION_PATH, pin);
	snprintf(value_path, sizeof(value_path), GPIO_VALUE_PATH, pin);

	// Clean-up of gpio
	unexport_fd = open(GPIO_UNEXPORT_PATH, O_WRONLY);
	bytes_written = write(unexport_fd, gpio_x, gpio_x_len);
	close(unexport_fd);

	export_fd = open(GPIO_EXPORT_PATH, O_WRONLY);
	if (export_fd < 0)
	{
		ACC_LOG_FATAL("Unable to open gpio export: %s", strerror(errno));
		return ACC_STATUS_FAILURE;
	}

	bytes_written = write(export_fd, gpio_x, gpio_x_len);
	if (bytes_written < 0)
	{
		ACC_LOG_ERROR("Could not write to gpio export: %s", strerror(errno));
		close(export_fd);
		return ACC_STATUS_FAILURE;
	}

	if (bytes_written != gpio_x_len)
	{
		ACC_LOG_ERROR("Expected to write %d bytes to gpio export, but wrote: %d", (int)gpio_x_len, (int)bytes_written);
		close(export_fd);
		return ACC_STATUS_FAILURE;
	}

	close(export_fd);

	gpio->dir_fd	= -1;
	gpio->value_fd	= -1;
	gpio->dir	= GPIO_DIR_UNKNOWN;
	gpio->value	= 0;
	gpio->pull	= 0;

	// Wait a maximum of one second for each gpio to open
	for (uint_fast16_t loop_count = 0; loop_count < 100; loop_count++)
	{
		gpio->dir_fd = open(dir_path, O_RDWR);
		if (gpio->dir_fd < 0)
		{
			acc_os_sleep_us(10000);
		}
		else
		{
			ACC_LOG_VERBOSE("Waited %u ms on gpio open direction", (unsigned int)(loop_count * 10));
			break;
		}
	}

	if (gpio->dir_fd < 0)
	{
		ACC_LOG_ERROR("Unable to open gpio%" PRIuFAST8 " direction: %s", pin, strerror(errno));
		return ACC_STATUS_FAILURE;
	}

	// Wait a maximum of one second for each gpio to open
	for (uint_fast16_t loop_count = 0; loop_count < 100; loop_count++)
	{
		gpio->value_fd = open(value_path, O_RDWR);
		if (gpio->value_fd < 0)
		{
			acc_os_sleep_us(10000);
		}
		else
		{
			ACC_LOG_VERBOSE("Waited %u ms on gpio open value", (unsigned int)(loop_count * 10));
			break;
		}
	}

	if (gpio->value_fd < 0)
	{
		ACC_LOG_ERROR("Unable to open gpio%" PRIuFAST8 " value: %s", pin, strerror(errno));
		return ACC_STATUS_FAILURE;
	}

	gpio->is_open = true;

	return ACC_STATUS_SUCCESS;
}


/**
 * @brief Internal GPIO set edge
 *
 * Set GPIO edge to the specified edge on the specified pin.
 *
 * @param pin The pin to set edge on
 * @param edge The edge to be set
 * @return True if edge was successfully set
 */
static bool internal_gpio_set_edge(uint_fast8_t pin, acc_gpio_edge_t edge)
{
	int	edge_fd;
	char	edge_path[sizeof(GPIO_EDGE_PATH) + 10];
	char	gpio_edge[8];
	ssize_t	bytes_written;

	snprintf(edge_path, sizeof(edge_path), GPIO_EDGE_PATH, pin);

	switch (edge)
	{
		case ACC_DEVICE_GPIO_EDGE_NONE:
			strcpy(gpio_edge, "none");
			break;

		case ACC_DEVICE_GPIO_EDGE_FALLING:
			strcpy(gpio_edge, "falling");
			break;

		case ACC_DEVICE_GPIO_EDGE_RISING:
			strcpy(gpio_edge, "rising");
			break;

		case ACC_DEVICE_GPIO_EDGE_BOTH:
			strcpy(gpio_edge, "both");
			break;
	}

	edge_fd = open(edge_path, O_WRONLY);
	if (edge_fd < 0)
	{
		ACC_LOG_ERROR("Unable to open gpio edge: %s", strerror(errno));
		return false;
	}

	bytes_written = write(edge_fd, gpio_edge, sizeof(gpio_edge));
	if (bytes_written < 0)
	{
		ACC_LOG_ERROR("Could not write to gpio edge: %s", strerror(errno));
		close(edge_fd);
		return false;
	}

	close(edge_fd);

	return true;
}


/**
 * @brief Internal GPIO set direction
 *
 * If 'dir' is GPIO_DIR_IN, 'value' is not used.
 *
 * @param gpio GPIO information
 * @param dir The direction to be set
 * @param value The value to be written
 * @return Status
 */
static acc_status_t internal_gpio_set_dir(gpio_t *gpio, gpio_dir_t dir, uint_fast8_t value)
{
	ssize_t	bytes_written;
	char	*dir_str    = (dir == GPIO_DIR_IN) ? "in" : ((value == 0) ? "low" : "high");
	size_t	dir_str_len = strlen(dir_str);

	bytes_written = write(gpio->dir_fd, dir_str, dir_str_len);
	if (bytes_written < 0)
	{
		ACC_LOG_ERROR("Could not write to gpio%" PRIuFAST8 " direction: %s", gpio->pin, strerror(errno));
		return ACC_STATUS_FAILURE;
	}
	else if (bytes_written != (ssize_t)dir_str_len)
	{
		ACC_LOG_ERROR("Expected to write %u bytes to GPIO direction, but wrote: %d bytes", (unsigned int)dir_str_len, (int)bytes_written);
		return ACC_STATUS_FAILURE;
	}

	gpio->dir   = dir;
	gpio->value = value;

	return ACC_STATUS_SUCCESS;
}


/**
 * @brief Internal GPIO write
 *
 * Write 'value' to /sys/class/gpio/gpio#/value.
 * GPIO needs to already be an output.
 *
 * @param gpio GPIO information
 * @param value The value to be written
 * @return Status
 */
static acc_status_t internal_gpio_set_value(gpio_t *gpio, uint_fast8_t value)
{
	ssize_t bytes_written;

	if (value > 1)
	{
		value = 1;
	}

	if (gpio->value == value)
	{
		return ACC_STATUS_SUCCESS;
	}

	bytes_written = write(gpio->value_fd, (value == 0) ? "0" : "1", 1);
	if (bytes_written < 0)
	{
		ACC_LOG_ERROR("Could not write to gpio%" PRIuFAST8 " value: %s", gpio->pin, strerror(errno));
		return ACC_STATUS_FAILURE;
	}
	else if (bytes_written != 1)
	{
		ACC_LOG_ERROR("Bytes written to gpio%" PRIuFAST8 " were not 1", gpio->pin);
		return ACC_STATUS_FAILURE;
	}

	gpio->value = value;
	return ACC_STATUS_SUCCESS;
}


/**
 * @brief Internal GPIO close all gpios
 *
 * Unexport GPIO to remove /sys/class/gpio/gpio#.
 */
static void internal_gpio_close_all(void)
{
	int	unexport_fd;
	char	gpio_x[5];
	ssize_t	gpio_x_len;
	ssize_t	bytes_written;
	gpio_t	*gpio;

	unexport_fd = open(GPIO_UNEXPORT_PATH, O_WRONLY);
	if (unexport_fd < 0) {
		ACC_LOG_ERROR("Unable to open gpio unexport: %s", strerror(errno));
		return;
	}

	gpio = gpios;
	for (uint_fast8_t pin = 0; pin < gpio_count; pin++, gpio++)
	{
		if (gpio->is_open)
		{
			if (gpio->dir == GPIO_DIR_OUT)
			{
				internal_gpio_set_value(gpio, gpio->pull);
				internal_gpio_set_dir(gpio, GPIO_DIR_IN, 0);
			}

			close(gpio->dir_fd);
			gpio->dir_fd = -1;

			close(gpio->value_fd);
			gpio->value_fd = -1;

			gpio_x_len = snprintf(gpio_x, sizeof(gpio_x), "%" PRIuFAST8, gpio->pin);

			bytes_written = write(unexport_fd, gpio_x, gpio_x_len);
			if (bytes_written < 0)
			{
				ACC_LOG_ERROR("Could not write to gpio unexport for gpio%" PRIuFAST8 ": %s", gpio->pin, strerror(errno));
			}
			else if (bytes_written != gpio_x_len)
			{
				ACC_LOG_ERROR("Expected to write %d bytes to gpio unexport, but wrote %d for gpio%" PRIuFAST8, (int)gpio_x_len, (int)bytes_written, gpio->pin);
			}

			gpio->is_open = false;
		}
	}

	// Forbidden due to race condition between gpio_close_all and gpio_input/output.
//	acc_os_mem_free(gpios);

	close(unexport_fd);
}


/**
 * @brief Check if an interrupt service routine has been registered with a GPIO
 *
 * @param[in] gpio The GPIO information
 * @return True if an interrupt service routine has been registered
 */
static bool is_isr_registered(const gpio_t *gpio)
{
	if (gpio->mutex != NULL)
	{
		acc_os_mutex_lock(gpio->mutex);
		bool registered = gpio->isr != NULL;
		acc_os_mutex_unlock(gpio->mutex);

		return registered;
	}
	else
	{
		return false;
	}
}


/**
 * @brief Clear an interrupt
 *
 * @param[in] gpio Relevant GPIO information
 * @return True if the interrupt was cleared
 */
static bool clear_interrupt(const gpio_t *gpio)
{
	// Position file pointer at beginning of value file.
	if (lseek(gpio->value_fd, 0, SEEK_SET) != 0)
	{
		ACC_LOG_FATAL("Failed to lseek GPIO.");
		return false;
	}

	// Do a dummy read to clear the interrupt.
	uint8_t garbage;
	if (read(gpio->value_fd, &garbage, sizeof(garbage)) != sizeof(garbage))
	{
		ACC_LOG_FATAL("Failed to read GPIO.");
		return false;
	}

	return true;
}


/**
 * @brief Wait until an interrupt is triggered and call the appropriate interrupt service routine
 *
 * @param[in] param Relevant GPIO information
 */
static void wait_for_interrupts(void *param)
{
	gpio_t *gpio = param;
	struct pollfd fds;

	fds.fd = gpio->value_fd;
	fds.events = POLLPRI | POLLERR;

	int timeout_ms = 800;

	while (is_isr_registered(gpio))
	{
		int ret = poll(&fds, 1, timeout_ms);
		if (ret > 0)
		{
			if (!clear_interrupt(gpio))
			{
				ACC_LOG_FATAL("Failed to clear interrupt!");
				return;
			}

			// Call the callback.
			if (is_isr_registered(gpio))
			{
				gpio->isr();
			}
		}
		else if (ret == -1)
		{
			ACC_LOG_FATAL("An error occurred while waiting for interrupt on pin %" PRIuFAST8 ". Error: %s", gpio->pin, strerror(errno));
			return;
		}
	}
}


/**
 * @brief Unregister an interrupt service routine
 *
 * @param pin The GPIO pin where the callback is registered.
 */
static void unregister_isr(uint_fast8_t pin)
{
	gpio_t *gpio = &gpios[pin];

	if (is_isr_registered(gpio))
	{
		acc_os_mutex_lock(gpio->mutex);
		gpio->isr = NULL;
		acc_os_mutex_unlock(gpio->mutex);

		acc_os_thread_cleanup(gpio->handle);
		gpio->handle = NULL;

		acc_os_mutex_destroy(gpio->mutex);
		gpio->mutex = NULL;
	}
}


/**
 * @brief Register an interrupt service routine
 *
 * @param pin The GPIO pin to listen to
 * @param edge The edge that will trigger the isr
 * @param[in] isr The interrupt service routine which will be triggered on an interrupt
 * @return True if the interrupt service routine was successfully registered.
 */
static bool register_isr(uint_fast8_t pin, acc_gpio_edge_t edge, acc_device_gpio_isr_t isr)
{
	acc_status_t	status;
	gpio_t		*gpio = &gpios[pin];

	status = internal_gpio_open(pin);
	if (status != ACC_STATUS_SUCCESS)
	{
		return false;
	}

	if (is_isr_registered(gpio))
	{
		// A callback is already registered so just swap it
		acc_os_mutex_lock(gpio->mutex);
		gpio->isr = isr;
		acc_os_mutex_unlock(gpio->mutex);

		return ACC_STATUS_SUCCESS;
	}

	if (!internal_gpio_set_edge(pin, edge))
	{
		return false;
	}

	gpio->isr = isr;

	gpio->mutex = acc_os_mutex_create();
	if (gpio->mutex == NULL)
	{
		ACC_LOG_ERROR("Failed to create mutex.");
		gpio->isr = NULL;
		return false;
	}

	gpio->handle = acc_os_thread_create(&wait_for_interrupts, gpio);
	if (gpio->handle == NULL)
	{
		ACC_LOG_ERROR("Failed to initiate interrupt handler.");
		acc_os_mutex_destroy(gpio->mutex);
		gpio->mutex = NULL;
		gpio->isr = NULL;
		return false;
	}

	return true;
}


/**
 * @brief Initialize GPIO driver
 *
 * @return Status
 */
static acc_status_t acc_driver_gpio_linux_sysfs_init(void)
{
	static acc_os_mutex_t	init_mutex = NULL;
	static bool		init_done = false;

	if (init_done)
	{
		return ACC_STATUS_SUCCESS;
	}

	acc_os_init();
	init_mutex = acc_os_mutex_create();

	acc_os_mutex_lock(init_mutex);
	if (init_done)
	{
		acc_os_mutex_unlock(init_mutex);
		return ACC_STATUS_SUCCESS;
	}

	gpios = acc_os_mem_calloc(gpio_count, sizeof(gpio_t));
	if (gpios == NULL)
	{
		ACC_LOG_ERROR("Out of memory");
		acc_os_mutex_unlock(init_mutex);
		return ACC_STATUS_OUT_OF_MEMORY;
	}

	for (uint_fast8_t pin = 0; pin < gpio_count; pin++)
	{
		gpios[pin].pin		= pin;
		gpios[pin].dir_fd	= -1;
		gpios[pin].value_fd	= -1;
		gpios[pin].isr		= NULL;
		gpios[pin].handle	= NULL;
		gpios[pin].mutex	= NULL;
	}

	if (atexit(internal_gpio_close_all))
	{
		ACC_LOG_ERROR("Unable to set exit function 'internal_gpio_close_all()'");
		internal_gpio_close_all();
		acc_os_mutex_unlock(init_mutex);
		return ACC_STATUS_FAILURE;
	}

	init_done = true;
	acc_os_mutex_unlock(init_mutex);

	return ACC_STATUS_SUCCESS;
}


/**
 * @brief Inform the driver of the pull up/down level for a GPIO pin after reset
 *
 * This does not change the pull level, but only informs the driver what pull level
 * the pin is configured to have.
 *
 * The GPIO pin numbering is decided by the GPIO driver.
 *
 * @param pin Pin number
 * @param level The pull level 0 or 1
 * @return Status
 */
static acc_status_t acc_driver_gpio_linux_sysfs_set_initial_pull(uint_fast8_t pin, uint_fast8_t level)
{
	acc_status_t	status;
	gpio_t		*gpio;

	status = internal_gpio_open(pin);
	if (status != ACC_STATUS_SUCCESS)
	{
		return status;
	}
	gpio = &gpios[pin];

	gpio->pull = level ? 1 : 0;

	return ACC_STATUS_SUCCESS;
}


/**
 * @brief Set GPIO to input
 *
 * This function sets the direction of a GPIO to input.
 * GPIO parameter is not a pin number but GPIO index (0-X).
 *
 * @param pin GPIO pin to be set to input
 * @return Status
 */
static acc_status_t acc_driver_gpio_linux_sysfs_input(uint_fast8_t pin)
{
	acc_status_t	status;
	gpio_t		*gpio;

	status = internal_gpio_open(pin);
	if (status != ACC_STATUS_SUCCESS)
	{
		return status;
	}
	gpio = &gpios[pin];

	if (gpio->dir == GPIO_DIR_IN)
	{
		return ACC_STATUS_SUCCESS;
	}

	if (gpio->dir == GPIO_DIR_OUT)
	{
		// Needed to prevent glitches in Raspberry Pi when changing back to output
		status = internal_gpio_set_value(gpio, gpio->pull);
		if (status != ACC_STATUS_SUCCESS)
		{
			return status;
		}
	}

	status = internal_gpio_set_dir(gpio, GPIO_DIR_IN, 0);
	if (status != ACC_STATUS_SUCCESS)
	{
		return status;
	}

	return ACC_STATUS_SUCCESS;
}


/**
 * @brief Read from GPIO
 *
 * @param pin GPIO pin to read
 * @param value The value which has been read
 * @return Status
 */
static acc_status_t acc_driver_gpio_linux_sysfs_read(uint_fast8_t pin, uint_fast8_t *value)
{
	acc_status_t	status;
	gpio_t		*gpio;

	status = internal_gpio_open(pin);
	if (status != ACC_STATUS_SUCCESS)
	{
		return status;
	}
	gpio = &gpios[pin];

	if (gpio->dir != GPIO_DIR_IN)
	{
		ACC_LOG_ERROR("Cannot read GPIO %" PRIuFAST8 " as it is output/unknown", pin);
		return ACC_STATUS_FAILURE;
	}

	char	value_str[10];
	ssize_t	bytes_read;

	// position file pointer at beginning of value file
	off_t offset = lseek(gpio->value_fd, 0, SEEK_SET);
	if (offset < 0)
	{
		ACC_LOG_ERROR("Unable to lseek() GPIO %" PRIuFAST8 ": (%d) %s", pin, errno, strerror(errno));
		return ACC_STATUS_FAILURE;
	}

	if (offset > 0)
	{
		ACC_LOG_ERROR("lseek() GPIO %" PRIuFAST8 " returned %llu", pin, (unsigned long long)offset);
		return ACC_STATUS_FAILURE;
	}

	// read GPIO input value
	bytes_read = read(gpio->value_fd, value_str, sizeof(value_str));
	if (bytes_read < 0)
	{
		ACC_LOG_ERROR("Unable to read from GPIO %" PRIuFAST8 ": (%d) %s", pin, errno, strerror(errno));
		return ACC_STATUS_FAILURE;
	}
	if (bytes_read == 0)
	{
		ACC_LOG_ERROR("Zero bytes read for GPIO %" PRIuFAST8, pin);
		return ACC_STATUS_FAILURE;
	}

	*value = (value_str[0] != '0') ? 1 : 0;

	return ACC_STATUS_SUCCESS;
}


/**
 * @brief Set GPIO output level
 *
 * This function sets a GPIO to output and the level to low or high.
 *
 * @param pin GPIO pin to be set
 * @param level 0 to 1 to set pin low or high
 * @return Status
 */
static acc_status_t acc_driver_gpio_linux_sysfs_write(uint_fast8_t pin, uint_fast8_t level)
{
	acc_status_t	status;
	gpio_t		*gpio;

	status = internal_gpio_open(pin);
	if (status != ACC_STATUS_SUCCESS)
	{
		return status;
	}

	gpio = &gpios[pin];
	if (gpio->dir == GPIO_DIR_OUT)
	{
		status = internal_gpio_set_value(gpio, level);
		if (status != ACC_STATUS_SUCCESS)
		{
			return status;
		}
	}
	else
	{
		status = internal_gpio_set_dir(gpio, GPIO_DIR_OUT, level);
		if (status != ACC_STATUS_SUCCESS)
		{
			return status;
		}
	}

	return ACC_STATUS_SUCCESS;
}


/**
 * @brief Register an interrupt service routine for a GPIO pin
 *
 * Registers an interrupt service routine which will be called when the specified edge is detected on the selected GPIO pin.
 * If ACC_STATUS_SUCCESS is returned the interrupt service routine will immediately be triggered if the specified edge is detected.
 * If a new interrupt service routine is registered it will replace the old one.
 *
 * The interrupt service routine can be unregistered by register NULL.
 * Unregister an already unregistered interrupt service routine has no effect.
 *
 * @param pin GPIO pin the interrupt service routine will be attached to.
 * @param edge The edge that will trigger the interrupt service routine. Can be set to "falling", "rising" or both.
 * @param isr The function to be called when the specified edge is detected.
 * @return ACC_STATUS_SUCCESS if the interrupt service routine was registered, ACC_STATUS_UNSUPPORTED if the specified pin does not
 *      support interrupts or, ACC_STATUS_FAILURE if the registration failed.
 */
static acc_status_t acc_driver_gpio_linux_sysfs_register_isr(uint_fast8_t pin, acc_gpio_edge_t edge, acc_device_gpio_isr_t isr)
{
	if (isr == NULL)
	{
		unregister_isr(pin);
	}
	else
	{
		if (!register_isr(pin, edge, isr))
		{
			return ACC_STATUS_FAILURE;
		}
	}

	return ACC_STATUS_SUCCESS;
}


/**
 * @brief Request driver to register with appropriate device(s)
 *
 * @param pin_count The maximum number of pins supported
 */
void acc_driver_gpio_linux_sysfs_register(uint_fast16_t pin_count)
{
	gpio_count = pin_count;

	acc_device_gpio_init_func		= acc_driver_gpio_linux_sysfs_init;
	acc_device_gpio_set_initial_pull_func	= acc_driver_gpio_linux_sysfs_set_initial_pull;
	acc_device_gpio_input_func		= acc_driver_gpio_linux_sysfs_input;
	acc_device_gpio_read_func		= acc_driver_gpio_linux_sysfs_read;
	acc_device_gpio_write_func		= acc_driver_gpio_linux_sysfs_write;
	acc_device_gpio_register_isr_func	= acc_driver_gpio_linux_sysfs_register_isr;
}
