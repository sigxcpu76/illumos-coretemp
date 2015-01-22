#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sys/processor.h>
#include <sys/procset.h>
#include <kstat.h>
#include <stdlib.h>

#include <coretemp.h>

#define	MAX_CPUS	1024

typedef struct {
	int cpu_index;
	int chip_id;
	int core_id;
} cpu_info_t;

long get_kstat_number(kstat_ctl_t *kernelDesc, char *moduleName,
	int instance, char *recordName, char *fieldName);
char *get_kstat_string(kstat_ctl_t *kernelDesc, char *moduleName,
	int instance, char *recordName, char *fieldName);

long read_value(kstat_ctl_t *kstat, int instance, char *field);

cpu_info_t *find_cpu(cpu_info_t *cpus, int cpus_size, int c_socket, int c_core);

int
main(int argc, char *argv[1])
{

	kstat_ctl_t *kstat;
	cpu_info_t cpus[MAX_CPUS];
	int cpu_count, cpu_socket_count;
	cpu_info_t *core_ptr;
	int machine_readable, display_cpu;
	int c_socket, c_core, c_index;
	int temp;
	int has_thermal_monitoring;

	cpu_socket_count = 0;
	machine_readable = 0;
	display_cpu = 1;

	if ((kstat = kstat_open()) == NULL) {
		perror("kstat_open");
		return (1);
	}

	if (argc > 1) {
		if (strcmp(argv[1], "-p") == 0) {
			machine_readable = 1;
		}
		if (argc > 2) {
			display_cpu = atoi(argv[2]);
		}
	}

	for (cpu_count = 0; cpu_count < MAX_CPUS; cpu_count++) {
		int chip_id;
		if ((chip_id = read_value(kstat, cpu_count, "chip_id")) < 0) {
			break;
		}
		int core_id = read_value(kstat, cpu_count, "core_id");
		cpus[cpu_count].cpu_index = cpu_count;
		cpus[cpu_count].chip_id = chip_id;
		cpus[cpu_count].core_id = core_id;

		if (chip_id + 1 > cpu_socket_count) {
			cpu_socket_count = chip_id + 1;
		}
	}

	if (!machine_readable) {
		printf("Found %d CPU%s in %d socket%s\n",
		    cpu_count, (cpu_count == 1) ? "" : "s",
		    cpu_socket_count, (cpu_socket_count == 1) ? "" : "s");
	}

	for (c_socket = 0; c_socket < cpu_socket_count; c_socket++) {
		for (c_core = 0; c_core < MAX_CPUS; c_core++) {
			core_ptr = find_cpu(cpus, cpu_count, c_socket, c_core);

			if (core_ptr == NULL) {
				continue;
			}

			c_index = core_ptr->cpu_index;

			has_thermal_monitoring =
			    (read_value(kstat, c_index, "core_temp") >= 0);

			if (!has_thermal_monitoring) {
				continue;
			}

			if (core_ptr->core_id == 0 && machine_readable == 0) {
				/* first core. display package information */
				temp =
				    read_value(kstat, c_index, "chip_temp");
				printf("Socket #%d", core_ptr->chip_id);
				if (temp >= 0) {
					printf(" temp : %d \u00B0C\n", temp);
				} else {
					printf("\n");
				}
			}

			temp = read_value(kstat, c_index, "core_temp");
			if (machine_readable == 0) {
				printf("\tCore #%d", core_ptr->core_id);
				if (temp >= 0) {
					printf(" temp : %d \u00B0C\n", temp);
				} else {
					printf("\n");
				}
			} else {
				if (display_cpu == -1) {
					printf("%d %d\n",
					    core_ptr->cpu_index, temp);
				} else if (display_cpu == core_ptr->cpu_index) {
					printf("%d\n",
					    temp);
				}
			}
		}
	}

	kstat_close(kstat);

	return (0);
}

cpu_info_t *
find_cpu(cpu_info_t *cpus, int cpus_size, int c_socket, int c_core)
{
	int i;
	cpu_info_t *result;

	for (i = 0; i < cpus_size; i++) {
		result = &cpus[i];
		if (result->chip_id == c_socket &&
		    result->core_id == c_core) {

			return (result);
		}
	}

	return (NULL);
}


long
read_value(kstat_ctl_t *kstat, int instance, char *field)
{
	return (get_kstat_number(kstat, KSTAT_CORETEMP_MODULE,
	    instance, KSTAT_CORETEMP_NAME, field));
}

long
get_kstat_number(kstat_ctl_t *kernelDesc, char *moduleName, int instance,
    char *recordName, char *fieldName)
{
	kstat_t *kstatRecordPtr;
	kstat_named_t *kstatFields;
	long value;
	int i;

	kstatRecordPtr =
	    kstat_lookup(kernelDesc, moduleName, instance, recordName);

	if (kstatRecordPtr == NULL) {
		return (-1);
	}

	if (kstat_read(kernelDesc, kstatRecordPtr, NULL) < 0) {
		return (-1);
	}

	kstatFields = KSTAT_NAMED_PTR(kstatRecordPtr);

	for (i = 0; i < kstatRecordPtr->ks_ndata; i++) {
		if (strcmp(kstatFields[i].name, fieldName) == 0) {
			switch (kstatFields[i].data_type) {
				case KSTAT_DATA_INT32:
					value = kstatFields[i].value.i32;
					break;
				case KSTAT_DATA_UINT32:
					value = kstatFields[i].value.ui32;
					break;
				case KSTAT_DATA_INT64:
					value = kstatFields[i].value.i64;
					break;
				case KSTAT_DATA_UINT64:
					value = kstatFields[i].value.ui64;
					break;
				default:
					value = -1;
			}
			return (value);
		}
	}
	return (-1);
}
