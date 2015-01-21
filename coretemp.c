/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright (c) 2012, Joyent, Inc.  All rights reserved.
 */


#include <sys/types.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/open.h>
#include <sys/cred.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/processor.h>
#include <sys/cpuvar.h>
#include <sys/kmem.h>
#include <sys/modctl.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/systm.h>
#include <sys/kstat.h>

#include <sys/auxv.h>
#include <sys/systeminfo.h>
#include <sys/cpuvar.h>
#include <sys/pghw.h>

//#if defined(__x86)
#include <sys/x86_archext.h>
//#pragma warn it is fine
//#endif

static dev_info_t *coretemp_devi;

struct coretemp_kstat_t {
		kstat_named_t chip_id;
		kstat_named_t core_id;
		kstat_named_t core_temp;
		kstat_named_t chip_temp;
		kstat_named_t tj_max;
		kstat_named_t target_temp;
		kstat_named_t valid_reading;
} coretemp_kstat_t = {
		{ "chip_id",		KSTAT_DATA_LONG },
		{ "core_id",		KSTAT_DATA_LONG },
		{ "core_temp",		KSTAT_DATA_LONG },
		{ "chip_temp",		KSTAT_DATA_LONG },
		{ "tj_max",			KSTAT_DATA_LONG },
		{ "target_temp", 	KSTAT_DATA_LONG },
		{ "valid",			KSTAT_DATA_LONG },

};

typedef struct {
	uint32_t msr_index;
	uint64_t *result;
} msr_req_t;

kstat_t *entries[1024];

static int coretemp_fill_fields(cpu_t *cpu);
static int coretemp_rdmsr_on_cpu(cpu_t *cpu, uint32_t msr_index, uint64_t *result);
static void coretemp_cpuid_on_cpu(cpu_t *cpu, uint32_t cpuid_func, struct cpuid_regs *result);

static int coretemp_kstat_update(kstat_t *kstat, int rw) {
	if(rw == KSTAT_WRITE) {
		return (EACCES);
	}

	coretemp_kstat_t.valid_reading.value.i32 = 0;

	if (!is_x86_feature(x86_featureset, X86FSET_MSR)) {
		return (ENXIO);
	}

	
	cpu_t *cpu_ptr = (cpu_t *)kstat->ks_private;

	coretemp_fill_fields(cpu_ptr);

	// misc data
	coretemp_kstat_t.core_id.value.i32 = cpuid_get_pkgcoreid(cpu_ptr);
	coretemp_kstat_t.chip_id.value.i32 = pg_plat_hw_instance_id(cpu_ptr, PGHW_CHIP);
	

	
	return (0);
}

static int coretemp_getinfo(dev_info_t *devi, ddi_info_cmd_t cmd, void *arg, void **result) {
	switch(cmd) {
		case DDI_INFO_DEVT2DEVINFO:
		case DDI_INFO_DEVT2INSTANCE:
			break;
		default:
			return (DDI_FAILURE);
	}

	if(cmd == DDI_INFO_DEVT2INSTANCE) {
		*result = 0;
	} else {
		*result = coretemp_devi;
	}

	return (DDI_SUCCESS);
}

static int coretemp_attach(dev_info_t *devi, ddi_attach_cmd_t cmd) {
	if(cmd != DDI_ATTACH) {
		return (DDI_FAILURE);
	}

	if(!is_x86_feature(x86_featureset, X86FSET_CPUID)) {
		printf("This CPU does not support CPUID instruction\n");
		return (DDI_FAILURE);
	}

	switch(x86_vendor) {
		case X86_VENDOR_Intel:
			break;
		default:
			printf("This CPU vendor is not supported\n");
			return (DDI_FAILURE);
	}



	coretemp_devi = devi;

	printf("Attaching coretemp driver for %d CPU(s)", ncpus);

	// for each cpu initialize kstat
	int i;
	for(i = 0; i < ncpus; i++) {
		char kstat_name[128];
		sprintf(kstat_name, "coretemp%d", i);
		kstat_t *ksp = kstat_create("cpu_info", i, kstat_name, "misc", KSTAT_TYPE_NAMED, 
			sizeof(coretemp_kstat_t) / sizeof(kstat_named_t),
			KSTAT_FLAG_VIRTUAL);
		if(!ksp) {
			printf("Failed to create kstat entry");
			return (DDI_FAILURE);
		}

		entries[i] = ksp;
		ksp->ks_data = (void *)&coretemp_kstat_t;
		ksp->ks_update = coretemp_kstat_update;
		ksp->ks_private = (void *)cpu[i];
		kstat_install(ksp);
	}
	return (DDI_SUCCESS);
}

static int coretemp_detach(dev_info_t *devi, ddi_detach_cmd_t cmd) {
	if (cmd != DDI_DETACH) {
		return (DDI_FAILURE);
	}
	coretemp_devi = NULL;

	int i;
	for(i = 0; i < ncpus; i++) {
		kstat_delete(entries[i]);
	}

	return (DDI_SUCCESS);
}


static struct cb_ops coretemp_cb_ops = {
	nulldev,
	nulldev,	/* close */
	nodev,		/* strategy */
	nodev,		/* print */
	nodev,		/* dump */
	nodev,
	nodev,		/* write */
	nodev,
	nodev,		/* devmap */
	nodev,		/* mmap */
	nodev,		/* segmap */
	nochpoll,	/* poll */
	ddi_prop_op,
	NULL,
	D_64BIT | D_NEW | D_MP
};

static struct dev_ops coretemp_dv_ops = {
	DEVO_REV,
	0,
	coretemp_getinfo,
	nulldev,	/* identify */
	nulldev,	/* probe */
	coretemp_attach,
	coretemp_detach,
	nodev,		/* reset */
	&coretemp_cb_ops,
	(struct bus_ops *)0,
	NULL,
	ddi_quiesce_not_needed,		/* quiesce */
};

static struct modldrv modldrv = {
	&mod_driverops,
	"coretemp driver 1.0",
	&coretemp_dv_ops
};

static struct modlinkage modl = {
	MODREV_1,
	&modldrv
};

int
_init(void)
{
	return (mod_install(&modl));
};

int
_fini(void)
{
	return (mod_remove(&modl));
};

int
_info(struct modinfo *modinfo)
{
	return (mod_info(&modl, modinfo));
};


// internal stuff

static int coretemp_fill_fields(cpu_t *cpu) {
	uint64_t msr;
	struct cpuid_regs regs;

	switch(x86_vendor) {
		case X86_VENDOR_Intel:
			// check CPUID data first
			coretemp_cpuid_on_cpu(cpu, 0x06, &regs);
			int has_package_temp_monitor = (regs.cp_eax >> 6) & 0x01;
			int has_thermal_monitoring = (regs.cp_eax) & 0x01;

			// initialize with invalid values
			coretemp_kstat_t.tj_max.value.i32 = -1;
			coretemp_kstat_t.chip_temp.value.i32 = -1;
			coretemp_kstat_t.core_temp.value.i32 = -1;
			coretemp_kstat_t.target_temp.value.i32 = -1;

			if(!has_thermal_monitoring) {
				return (0);
			}

			int tj_max = 100;

			int model = cpuid_getmodel(cpu);
			int family = cpuid_getfamily(cpu);
			int stepping = cpuid_getstep(cpu);

			// tj max
			if(model > 0x0e && model != 0x1c && model != 0x26 && model != 0x27 && model != 0x35 && model != 0x36) {
				if(coretemp_rdmsr_on_cpu(cpu, 0x1a2, &msr) == 0) {
					tj_max = (msr >> 16) & 0x7f;
					coretemp_kstat_t.tj_max.value.i32 = tj_max;

					// temp target
					if((model > 0x0e) && (model != 0x1c)) {
						coretemp_kstat_t.target_temp.value.i32 = tj_max - ((msr >> 8) & 0xff);
					}
				}
			}

			if(has_package_temp_monitor) {
				if(coretemp_rdmsr_on_cpu(cpu, 0x1b1, &msr) == 0) {
					int pkg_temp = tj_max - ((msr >> 16) & 0x7f);
					coretemp_kstat_t.chip_temp.value.i32 = pkg_temp;
				}
			}

			if(coretemp_rdmsr_on_cpu(cpu, 0x19c, &msr) == 0) {
				int core_temp = tj_max - ((msr >> 16) & 0x7f);
				coretemp_kstat_t.core_temp.value.i32 = core_temp;
				coretemp_kstat_t.valid_reading.value.i32 = 1;
			}
		default:
			// vendor not implemented
			return (EINVAL);
	}

	return (0);

}

/* Execute RDMSR on specified CPU */
static void coretemp_msr_req(uintptr_t req_ptr, uintptr_t error_ptr) {
	
	label_t ljb;
	uint32_t msr_index = ((msr_req_t *)req_ptr)->msr_index;
	uint64_t *result = ((msr_req_t *)req_ptr)->result;

	int error;

	if(on_fault(&ljb)) {
		dev_err(coretemp_devi, CE_WARN, "Invalid rdmsr(0x%08" PRIx32 ")", (uint32_t)msr_index);
		error = EFAULT;
	} else {
		error = checked_rdmsr(msr_index, result);
	}

	*((int *)error_ptr) = error;

	return;

}

static int coretemp_rdmsr_on_cpu(cpu_t *cpu, uint32_t msr_index, uint64_t *result) {
	int error;

	msr_req_t request;
	request.msr_index = msr_index;
	request.result = result;

	cpu_call(cpu, (cpu_call_func_t)coretemp_msr_req, (uintptr_t)&request, (uintptr_t)&error);

	return (error);
}

/* Execute CPUID on specified CPU */
static void coretemp_cpuid_on_cpu(cpu_t *cpu, uint32_t cpuid_func, struct cpuid_regs *result) {
	result->cp_eax = cpuid_func;
	result->cp_ebx = result->cp_ecx = result->cp_edx = 0;
	(void)cpuid_insn(cpu, result);
}
