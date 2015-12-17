#ifndef __CORETEMP_H

#define	__CORETEMP_H

/* kstat definitions */
#define	KSTAT_CORETEMP_MODULE	"coretemp"
#define	KSTAT_CORETEMP_NAME	"coretemp"
#define	KSTAT_CORETEMP_CLASS	"environment"

/* MSR definitions */
#define	MSR_IA32_THERM_STATUS		0x0000019c
#define	MSR_IA32_PACKAGE_THERM_STATUS	0x000001b1
#define	MSR_IA32_TEMPERATURE_TARGET	0x000001a2

#endif /* ifndef __CORETEMP_H */
