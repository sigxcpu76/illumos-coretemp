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

#define MAX_CPUS 1024

typedef struct {
  int cpu_index;
  int chip_id;
  int core_id;
} cpu_info_t;

long getKStatNumber(kstat_ctl_t *kernelDesc, char *moduleName, char *recordName, char *fieldName);
char *getKStatString(kstat_ctl_t *kernelDesc, char *moduleName, char *recordName, char *fieldName);
void temp_to_str(char *str, int temp);

int main(int argc, char *argv[1]) {

  kstat_ctl_t *kstat;
  cpu_info_t cpus[MAX_CPUS];
  int cpu_count;
  int cpu_sockets = 0;

  if ((kstat = kstat_open()) == NULL) {
    perror("kstat_open");
    return (1);
  }

  int machine_readable = 0;
  int display_cpu = -1;
  if(argc > 1) {
    if(!strcmp(argv[1], "-p")) {
      machine_readable = 1;
    }
    if(argc > 2) {
      display_cpu = atoi(argv[2]);
    }
  }



  for(cpu_count = 0; cpu_count < MAX_CPUS; cpu_count++) {
    char record_name[128];
    sprintf(record_name, "coretemp%d", cpu_count);
    int chip_id;
    if ((chip_id = getKStatNumber(kstat, "cpu_info", record_name, "chip_id")) < 0) {
      break;
    }
    int core_id = getKStatNumber(kstat, "cpu_info", record_name, "core_id");
    cpus[cpu_count].cpu_index = cpu_count;
    cpus[cpu_count].chip_id = chip_id;
    cpus[cpu_count].core_id = core_id;

    if(chip_id + 1 > cpu_sockets){
      cpu_sockets = chip_id + 1;
    }
  }


  if(!machine_readable) printf("Found %d CPU%s in %d socket%s\n", cpu_count, (cpu_count == 1) ? "" : "s", cpu_sockets, (cpu_sockets == 1) ? "" : "s");

  int cpu_socket, cpu_core, cpu_index;

  for(cpu_socket = 0; cpu_socket < cpu_sockets; cpu_socket++) {
    // find cores for current socket
    for(cpu_core = 0; cpu_core < MAX_CPUS; cpu_core++) {
      cpu_info_t *core_ptr = NULL;
      for(cpu_index = 0; cpu_index < cpu_count; cpu_index++) {
        cpu_info_t *cpu_ptr = &cpus[cpu_index];
        if((cpu_ptr->chip_id == cpu_socket) && (cpu_ptr->core_id == cpu_core)) {
          core_ptr = cpu_ptr;
          break;
        }
      }

      if(core_ptr) {
        char record_name[128];
        sprintf(record_name, "coretemp%d", core_ptr->cpu_index);

        int has_package_temp_monitor = (getKStatNumber(kstat, "cpu_info", record_name, "chip_temp") >= 0);
        int has_thermal_monitoring = (getKStatNumber(kstat, "cpu_info", record_name, "core_temp") >= 0);

        if(has_thermal_monitoring) {
          //int tj_max = getKStatNumber(kstat, "cpu_info", record_name, "tj_max");

          if(core_ptr->core_id == 0) {
            // this is the first core, so print package information, too
            int package_temp = getKStatNumber(kstat, "cpu_info", record_name, "chip_temp");
            if(!machine_readable) {
              printf("Socket #%d", core_ptr->chip_id);
              if(package_temp >= 0) {
                printf(" temp : %d \u00B0C\n", package_temp);
              } else {
                printf("\n");
              }
            }
          }
          // print core information
          int core_temp = getKStatNumber(kstat, "cpu_info", record_name, "core_temp");
          if(!machine_readable) {
            printf("\tCore #%d", cpu_core);
            if(core_temp >= 0) {
              printf(" temp : %d \u00B0C\n", core_temp);
            } else {
              printf("\n");
            }
          } else {
            if(display_cpu == -1) {
              printf("%d %d\n", core_ptr->cpu_index, core_temp);
            } else if(display_cpu == core_ptr->cpu_index) {
              printf("%d\n", core_temp);
            }
          }
        }
      }
    }
  }


  kstat_close(kstat);

  return 0;

}

void temp_to_str(char *str, int temp) {
  if(temp == -1) {
    sprintf(str, "n/a");
  } else {
    sprintf(str, "%d", temp);
  }
}

long getKStatNumber(kstat_ctl_t *kernelDesc, char *moduleName, 
     char *recordName, char *fieldName) {
  kstat_t *kstatRecordPtr;
  kstat_named_t *kstatFields;
  long value;
  int i;
       
  if ((kstatRecordPtr = kstat_lookup(kernelDesc, moduleName, -1, recordName)) ==
       NULL) {
     return(-1);
  }

  if (kstat_read(kernelDesc, kstatRecordPtr, NULL) < 0)
    return(-1);

  kstatFields = KSTAT_NAMED_PTR(kstatRecordPtr);

  for (i=0; i<kstatRecordPtr->ks_ndata; i++) {
    if (strcmp(kstatFields[i].name, fieldName) == 0) {
       switch(kstatFields[i].data_type) {
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
       return(value);
    }
  }
  return(-1);
}

/* Fetch string statistic from kernel */
char *getKStatString(kstat_ctl_t *kernelDesc, char *moduleName, 
     char *recordName, char *fieldName) {
  kstat_t *kstatRecordPtr;
  kstat_named_t *kstatFields;
  char *value;
  int i;
       
  if ((kstatRecordPtr = kstat_lookup(kernelDesc, moduleName, -1, recordName)) ==
       NULL) {
     return(NULL);
  }

  if (kstat_read(kernelDesc, kstatRecordPtr, NULL) < 0)
    return(NULL);

  kstatFields = KSTAT_NAMED_PTR(kstatRecordPtr);

  for (i=0; i<kstatRecordPtr->ks_ndata; i++) {
    if (strcmp(kstatFields[i].name, fieldName) == 0) {
       switch(kstatFields[i].data_type) {
          case KSTAT_DATA_CHAR:
               value = kstatFields[i].value.c;
               break;
          default:
               value = NULL;
       }
       return(value);
    }
  }
  return(NULL);
}
