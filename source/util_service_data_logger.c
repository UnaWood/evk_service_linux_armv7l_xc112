// Copyright (c) Acconeer AB, 2018
// All rights reserved

#include <complex.h>
#include <getopt.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acc_log.h"
#include "acc_rss.h"
#include "acc_service.h"
#include "acc_service_power_bins.h"
#include "acc_service_envelope.h"
#include "acc_service_iq.h"
#include "acc_sweep_configuration.h"

#include "acc_os.h"
#include "acc_version.h"

#define FLOAT_TO_UINT16_SCALE_FACTOR	(1 << 15)
#define DEFAULT_SWEEP_COUNT		0
#define DEFAULT_WAIT_FOR_INTERRUPT	true
#define DEFAULT_RANGE_START_M		0.07f
#define DEFAULT_RANGE_END_M		0.5f


volatile sig_atomic_t interrupted = 0;


typedef enum {
	INVALID_SERVICE = 0,
	POWER_BIN,
	ENVELOPE,
	IQ
} service_type_t;


typedef struct {
	service_type_t service_type;
	uint16_t sweep_count;
	bool wait_for_interrupt;
	float start_m;
	float end_m;
	char *file_path;
} input_t;


static bool parse_options(int argc, char *argv[], input_t *input);
static acc_service_configuration_t set_up_power_bin(input_t *input);
static acc_service_status_t execute_power_bin(acc_service_configuration_t power_bin_configuration, char *file_path, bool wait_for_interrupt, uint16_t sweep_count);
static acc_service_configuration_t set_up_envelope(input_t *input);
static acc_service_status_t execute_envelope(acc_service_configuration_t envelope_configuration, char *file_path, bool wait_for_interrupt, uint16_t sweep_count);
static acc_service_configuration_t set_up_iq(input_t *input);
static acc_service_status_t execute_iq(acc_service_configuration_t iq_configuration, char *file_path, bool wait_for_interrupt, uint16_t sweep_count);


void interrupt_handler(int signum)
{
	if (signum == SIGINT) {
		interrupted = 1;
	}
}


int main(int argc, char *argv[])
{
	input_t input = {INVALID_SERVICE, DEFAULT_SWEEP_COUNT, DEFAULT_WAIT_FOR_INTERRUPT, DEFAULT_RANGE_START_M, DEFAULT_RANGE_END_M, NULL};

	signal(SIGINT, interrupt_handler);

	acc_log_set_level(ACC_LOG_LEVEL_FATAL, NULL);

	if (!acc_rss_activate()) {
		return EXIT_FAILURE;
	}

	if (parse_options(argc, argv, &input) != ACC_STATUS_SUCCESS) {
		if (input.file_path != NULL) {
			acc_os_mem_free(input.file_path);
		}
		return EXIT_FAILURE;
	}

	acc_service_status_t service_status;

	switch (input.service_type) {
		case POWER_BIN:
		{
			acc_service_configuration_t power_bin_configuration = set_up_power_bin(&input);

			if (power_bin_configuration == NULL) {
				if (input.file_path != NULL) {
					acc_os_mem_free(input.file_path);
				}
				return EXIT_FAILURE;
			}

			service_status = execute_power_bin(power_bin_configuration, input.file_path, input.wait_for_interrupt, input.sweep_count);

			if (input.file_path != NULL) {
				acc_os_mem_free(input.file_path);
			}

			if (service_status != ACC_SERVICE_STATUS_OK) {
				printf("execute_power_bin() => (%u) %s\n", (unsigned int)service_status, acc_service_status_name_get(service_status));
				return EXIT_FAILURE;
			}

			acc_service_power_bins_configuration_destroy(&power_bin_configuration);
			break;
		}

		case ENVELOPE:
		{
			acc_service_configuration_t envelope_configuration = set_up_envelope(&input);

			if (envelope_configuration == NULL) {
				if (input.file_path != NULL) {
					acc_os_mem_free(input.file_path);
				}
				return EXIT_FAILURE;
			}

			service_status = execute_envelope(envelope_configuration, input.file_path, input.wait_for_interrupt, input.sweep_count);

			if (input.file_path != NULL) {
				acc_os_mem_free(input.file_path);
			}

			if (service_status != ACC_SERVICE_STATUS_OK) {
				printf("execute_envelope() => (%u) %s\n", (unsigned int)service_status, acc_service_status_name_get(service_status));
				return EXIT_FAILURE;
			}

			acc_service_envelope_configuration_destroy(&envelope_configuration);
			break;
		}

		case IQ:
		{

			acc_service_configuration_t iq_configuration = set_up_iq(&input);

			if (iq_configuration == NULL) {
				if (input.file_path != NULL) {
					acc_os_mem_free(input.file_path);
				}
			}

			service_status = execute_iq(iq_configuration, input.file_path, input.wait_for_interrupt, input.sweep_count);

			if (input.file_path != NULL) {
				acc_os_mem_free(input.file_path);
			}

			if (service_status != ACC_SERVICE_STATUS_OK) {
				printf("execute_iq() => (%u) %s\n", (unsigned int)service_status, acc_service_status_name_get(service_status));
				return EXIT_FAILURE;
			}

			acc_service_iq_configuration_destroy(&iq_configuration);
			break;
		}
		default:
		{
			printf("Invalid service_type %d\n", input.service_type);
			return EXIT_FAILURE;
		}
	}

	acc_rss_deactivate();

	return EXIT_SUCCESS;
}


static void print_usage()
{
	printf("Usage: example_data_to_file [OPTION]...\n\n");
	printf("-h, --help                  this help\n");
	printf("-t, --service-type          service type to be run\n");
	printf("                                0. Power bin\n");
	printf("                                1. Envelope\n");
	printf("                                2. IQ\n");
	printf("-c, --sweep-count           number of sweeps, default application continues until interrupt\n");
	printf("-b, --range-start           retrieve envelope starting at this distance [m], default %"PRIfloat"\n", ACC_LOG_FLOAT_TO_INTEGER(DEFAULT_RANGE_START_M));
	printf("-e, --range-end             retrieve envelope ending at this distance [m], default %"PRIfloat"\n", ACC_LOG_FLOAT_TO_INTEGER(DEFAULT_RANGE_END_M));
	printf("-o, --out                   path to out file, default stdout\n");
	printf("-v, --verbose               set debug level to verbose\n");
}


bool parse_options(int argc, char *argv[], input_t *input)
{
	static struct option long_options[] =
	{
		{"service-type",    required_argument,  0,      't'},
		{"sweep-count",     required_argument,  0,      'c'},
		{"range-start",     required_argument,  0,      'b'},
		{"range-end",       required_argument,  0,      'e'},
		{"out",             required_argument,  0,      'o'},
		{"verbose",         no_argument,        0,      'v'},
		{"help",            no_argument,        0,      'h'},
		{NULL,              0,                  NULL,   0}
	};

	int16_t character_code;
	int32_t option_index = 0;

	while ((character_code = getopt_long(argc, argv, "t:c:b:e:o:vh?", long_options, &option_index)) != -1) {
		switch (character_code) {
			case 't':
			{
				switch (atoi(optarg)) {
					case 0:
					{
						input->service_type = POWER_BIN;
						break;
					}
					case 1:
					{
						input->service_type = ENVELOPE;
						break;
					}
					case 2:
					{
						input->service_type = IQ;
						break;
					}
					default:
						printf("Invalid service type.\n");
						print_usage();
						return ACC_STATUS_FAILURE;
				}
				break;
			}
			case 'c':
			{
				input->sweep_count = atoi(optarg);
				input->wait_for_interrupt = false;
				break;
			}
			case 'b':
			{
				char *next;
				input->start_m = strtof(optarg, &next);
				break;
			}
			case 'e':
			{
				char *next;
				input->end_m = strtof(optarg, &next);
				break;
			}
			case 'o':
			{
				input->file_path = acc_os_mem_alloc(sizeof(char) * (strlen(optarg) + 1));
				if (input->file_path == NULL) {
					printf("Failed allocating memory\n");
					return ACC_STATUS_FAILURE;
				}
				snprintf(input->file_path, strlen(optarg) + 1, "%s", optarg);
				break;
			}
			case 'v':
			{
				acc_log_set_level(ACC_LOG_LEVEL_VERBOSE, NULL);
				break;
			}
			case 'h':
			case '?':
			{
				print_usage();
				return ACC_STATUS_FAILURE;
			}
		}
	}

	if (input->service_type == INVALID_SERVICE) {
		printf("Missing option service type.\n");
		print_usage();
		return ACC_STATUS_FAILURE;
	}

	return ACC_STATUS_SUCCESS;
}


acc_service_configuration_t set_up_power_bin(input_t *input)
{
	acc_service_configuration_t power_bin_configuration = acc_service_power_bins_configuration_create();

	if (power_bin_configuration == NULL) {
		printf("acc_service_power_bin_configuration_create() failed\n");
		return NULL;
	}

	acc_sweep_configuration_t sweep_configuration = acc_service_get_sweep_configuration(power_bin_configuration);

	if (sweep_configuration == NULL) {
		printf("acc_service_get_sweep_configuration() failed\n");
		return NULL;
	}

	float length_m = input->end_m - input->start_m;

	acc_sweep_configuration_requested_range_set(sweep_configuration, input->start_m, length_m);

	return power_bin_configuration;
}


acc_service_status_t execute_power_bin(acc_service_configuration_t power_bin_configuration, char *file_path, bool wait_for_interrupt, uint16_t sweep_count)
{
	acc_service_handle_t handle = acc_service_create(power_bin_configuration);

	if (handle == NULL) {
		printf("acc_service_create failed\n");
		return ACC_SERVICE_STATUS_FAILURE_UNSPECIFIED;
	}

	acc_service_power_bins_metadata_t power_bins_metadata;
	acc_service_power_bins_get_metadata(handle, &power_bins_metadata);

	float power_bins_data[power_bins_metadata.actual_bin_count];

	acc_service_power_bins_result_info_t result_info;
	acc_service_status_t service_status = acc_service_activate(handle);

	if (service_status == ACC_SERVICE_STATUS_OK) {
		FILE *file = stdout;

		if (file_path != NULL) {
			file = fopen(file_path, "w");

			if(file == NULL) {
				printf("opening file failed\n");
				return ACC_SERVICE_STATUS_FAILURE_UNSPECIFIED;
			}
		}

		uint16_t sweeps = 0;

		while ((wait_for_interrupt && interrupted == 0) || sweeps < sweep_count) {
			service_status = acc_service_power_bins_get_next(handle, power_bins_data, power_bins_metadata.actual_bin_count, &result_info);

			if (service_status == ACC_SERVICE_STATUS_OK) {
				for (uint_fast16_t index = 0; index < power_bins_metadata.actual_bin_count; index++) {
					fprintf(file, "%u\t", (unsigned int)(power_bins_data[index] + 0.5));
				}
				fprintf(file, "\n");
			}
			else {
				printf("Power bin data not properly retrieved\n");
			}

			if (!wait_for_interrupt) {
				sweeps++;
			}
		}

		if (file_path == NULL) {
			fflush(stdout);
		}
		else {
			fclose(file);
		}

		service_status = acc_service_deactivate(handle);
	}
	else {
		printf("acc_service_activate() (%u) => %s\n", (unsigned int)service_status, acc_service_status_name_get(service_status));
	}

	acc_service_destroy(&handle);

	return service_status;
}


acc_service_configuration_t set_up_envelope(input_t *input)
{
	acc_service_configuration_t envelope_configuration = acc_service_envelope_configuration_create();

	if (envelope_configuration == NULL) {
		printf("acc_service_envelope_configuration_create() failed\n");
		return NULL;
	}

	acc_sweep_configuration_t sweep_configuration = acc_service_get_sweep_configuration(envelope_configuration);

	if (sweep_configuration == NULL) {
		printf("acc_service_get_sweep_configuration() failed\n");
		return NULL;
	}

	float length_m = input->end_m - input->start_m;

	acc_sweep_configuration_requested_range_set(sweep_configuration, input->start_m, length_m);

	return envelope_configuration;
}


acc_service_status_t execute_envelope(acc_service_configuration_t envelope_configuration, char *file_path, bool wait_for_interrupt, uint16_t sweep_count)
{
	acc_service_handle_t handle = acc_service_create(envelope_configuration);

	if (handle == NULL) {
		printf("acc_Service_create failed\n");
		return ACC_SERVICE_STATUS_FAILURE_UNSPECIFIED;
	}

	acc_service_envelope_metadata_t envelope_metadata;
	acc_service_envelope_get_metadata(handle, &envelope_metadata);

	uint16_t envelope_data[envelope_metadata.data_length];

	acc_service_envelope_result_info_t result_info;
	acc_service_status_t service_status = acc_service_activate(handle);

	if (service_status == ACC_SERVICE_STATUS_OK) {
		FILE * file = stdout;

		if (file_path != NULL) {
			file = fopen(file_path, "w");

			if (file == NULL) {
				printf("opening file failed\n");
				return ACC_SERVICE_STATUS_FAILURE_UNSPECIFIED;
			}
		}

		uint16_t sweeps = 0;

		while ((wait_for_interrupt && interrupted == 0) || sweeps < sweep_count) {
			service_status = acc_service_envelope_get_next(handle, envelope_data, envelope_metadata.data_length, &result_info);

			if (service_status == ACC_SERVICE_STATUS_OK) {
				for (uint_fast16_t index = 0; index < envelope_metadata.data_length; index++) {
					fprintf(file, "%u\t", (unsigned int)(envelope_data[index] + 0.5));
				}
				fprintf(file, "\n");
			}
			else {
				printf("Envelope data not properly retrieved\n");
			}

			if (!wait_for_interrupt) {
				sweeps++;
			}
		}

		if (file_path == NULL) {
			fflush(stdout);
		}
		else {
			fclose(file);
		}

		service_status = acc_service_deactivate(handle);
	}
	else {
		printf("acc_service_activate() %u => %s\n", (unsigned int)service_status, acc_service_status_name_get(service_status));
	}

	acc_service_destroy(&handle);

	return service_status;
}


acc_service_configuration_t set_up_iq(input_t *input)
{
	acc_service_configuration_t iq_configuration = acc_service_iq_configuration_create();

	if (iq_configuration == NULL) {
		printf("acc_service_iq_configuration_create() failed\n");
		return NULL;
	}

	acc_sweep_configuration_t sweep_configuration = acc_service_get_sweep_configuration(iq_configuration);

	if (sweep_configuration == NULL) {
		printf("acc_service_get_sweep_configuration() failed\n");
		return NULL;
	}

	float length_m = input->end_m - input->start_m;

	acc_sweep_configuration_requested_range_set(sweep_configuration, input->start_m, length_m);

	return iq_configuration;
}


acc_service_status_t execute_iq(acc_service_configuration_t iq_configuration, char *file_path, bool wait_for_interrupt, uint16_t sweep_count)
{
	acc_service_handle_t handle = acc_service_create(iq_configuration);

	if (handle == NULL) {
		printf("acc_service_create failed\n");
		return ACC_SERVICE_STATUS_FAILURE_UNSPECIFIED;
	}

	acc_service_iq_metadata_t iq_metadata;
	acc_service_iq_get_metadata(handle, &iq_metadata);

	float complex iq_data[iq_metadata.data_length];
	acc_service_iq_result_info_t result_info;

	acc_service_status_t service_status = acc_service_activate(handle);

	if (service_status == ACC_SERVICE_STATUS_OK) {
		FILE * file = stdout;

		if (file_path != NULL) {
			file = fopen(file_path, "w");

			if (file == NULL) {
				printf("opening file failed\n");
				return ACC_SERVICE_STATUS_FAILURE_UNSPECIFIED;
			}
		}

		uint16_t sweeps = 0;

		while ((wait_for_interrupt && interrupted == 0) || sweeps < sweep_count) {
			service_status = acc_service_iq_get_next(handle, iq_data, iq_metadata.data_length, &result_info);

			if (service_status == ACC_SERVICE_STATUS_OK) {
				for (uint_fast16_t index = 0; index < iq_metadata.data_length; index++) {
					fprintf(file, "%"PRIfloat"\t%"PRIfloat"\t", ACC_LOG_FLOAT_TO_INTEGER(crealf(iq_data[index])), ACC_LOG_FLOAT_TO_INTEGER(cimagf(iq_data[index])));
				}
				fprintf(file, "\n");
			}
			else {
				printf("IQ data not properly retrieved\n");
			}

			if (!wait_for_interrupt) {
				sweeps++;
			}
		}

		if (file_path == NULL) {
			fflush(stdout);
		}
		else {
			fclose(file);
		}

		service_status = acc_service_deactivate(handle);
	}
	else {
		printf("acc_service_activate() %u => %s\n", (unsigned int)service_status, acc_service_status_name_get(service_status));
	}

	acc_service_destroy(&handle);

	return service_status;
}
