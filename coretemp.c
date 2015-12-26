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
 * Copyright (c) 2015, Alexandru Pirvulescu
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

#include <coretemp.h>

#include <sys/x86_archext.h>

static dev_info_t *ctemp_devi;

static kmutex_t ctemp_mutex;

struct ctemp_kstat_t {
		kstat_named_t chip_id;
		kstat_named_t core_id;
		kstat_named_t thread_id;
		kstat_named_t core_temp;
		kstat_named_t chip_temp;
		kstat_named_t tj_max;
		kstat_named_t target_temp;
		kstat_named_t valid;
} ctemp_kstat_t = {
		{ "chip_id",		KSTAT_DATA_INT32 },
		{ "core_id",		KSTAT_DATA_INT32 },
		{ "thread_id",		KSTAT_DATA_INT32 },
		{ "core_temp",		KSTAT_DATA_INT32 },
		{ "chip_temp",		KSTAT_DATA_INT32 },
		{ "tj_max",		KSTAT_DATA_INT32 },
		{ "target_temp",	KSTAT_DATA_INT32 },
		{ "valid",		KSTAT_DATA_INT32 },

};

static struct cpuid_regs current_cpuid;

typedef struct {
	uint32_t eax;
	uint32_t edx;
} msr_regs_t;

typedef struct {
	uint32_t msr_index;
	uint64_t *result;
} msr_req_t;

typedef struct {
	int chip_id;
	int core_id;
	cpu_t *cpu;
} ctemp_core_t;

kstat_t *entries[1024];
ctemp_core_t cores[1024];
int ncores = 0;

static int ctemp_fill_fields(cpu_t *cpu);

static int ctemp_rdmsr(cpu_t *cpu,
	uint32_t msr_index, msr_regs_t *result);
static void ctemp_cpuid(cpu_t *cpu, uint32_t cpuid_func,
    struct cpuid_regs *result);

static void ctemp_fill_tj_max(cpu_t *cpu);
static void ctemp_fill_pkg_temp(cpu_t *cpu);
static void ctemp_fill_core_temp(cpu_t *cpu);

static int
ctemp_kstat_update(kstat_t *kstat, int rw)
{
	if (rw == KSTAT_WRITE) {
		return (EACCES);
	}

	ctemp_kstat_t.valid.value.i32 = 0;

	if (!is_x86_feature(x86_featureset, X86FSET_MSR)) {
		return (ENXIO);
	}

	cpu_t *cpu_ptr = (cpu_t *)kstat->ks_private;

	ctemp_fill_fields(cpu_ptr);

	/* misc data */
	ctemp_kstat_t.core_id.value.i32 =
	    cpuid_get_pkgcoreid(cpu_ptr);
	ctemp_kstat_t.chip_id.value.i32 =
	    cpuid_get_chipid(cpu_ptr);
	ctemp_kstat_t.thread_id.value.i32 =
	    cpuid_get_clogid(cpu_ptr);

	return (0);
}

static int
ctemp_getinfo(dev_info_t *devi, ddi_info_cmd_t cmd, void *arg,
    void **result)
{
	switch (cmd) {
		case DDI_INFO_DEVT2DEVINFO:
		case DDI_INFO_DEVT2INSTANCE:
			break;
		default:
			return (DDI_FAILURE);
	}

	if (cmd == DDI_INFO_DEVT2INSTANCE) {
		*result = 0;
	} else {
		*result = ctemp_devi;
	}

	return (DDI_SUCCESS);
}

static int
ctemp_attach(dev_info_t *devi, ddi_attach_cmd_t cmd)
{
	if (cmd != DDI_ATTACH) {
		return (DDI_FAILURE);
	}

	if (!is_x86_feature(x86_featureset, X86FSET_CPUID)) {
		dev_err(devi, CE_NOTE,
		    "This CPU does not support CPUID instruction");
		return (DDI_FAILURE);
	}

	switch (x86_vendor) {
		case X86_VENDOR_Intel:
			break;
		default:
			dev_err(devi, CE_NOTE,
			    "This CPU vendor is not supported");
			return (DDI_FAILURE);
	}



	ctemp_devi = devi;

	int i, j;
	/* find physical cores */

	for (i = 0; i < ncpus; i++) {
		cpu_t *processor = cpu[i];

		int chip_id = cpuid_get_chipid(processor);
		int core_id = cpuid_get_coreid(processor);

		/* see if we already visited this core */
		int visited = 0;
		for (j = 0; j < ncores; j++) {
			if ((cores[j].chip_id == chip_id) &&
			    (cores[j].core_id == core_id)) {
				visited = 1;
				break;
			}
		}
		if (!visited) {
			cores[ncores].chip_id = chip_id;
			cores[ncores].core_id = core_id;
			cores[ncores].cpu = processor;
			ncores++;
		}

	}


	/* initialize a kstat instance for each core */

	for (i = 0; i < ncores; i++) {
		kstat_t *ksp = kstat_create(
		    KSTAT_CORETEMP_MODULE,
		    i,
		    KSTAT_CORETEMP_NAME,
		    KSTAT_CORETEMP_CLASS,
		    KSTAT_TYPE_NAMED,
		    sizeof (ctemp_kstat_t) / sizeof (kstat_named_t),
		    KSTAT_FLAG_VIRTUAL);
		if (!ksp) {
			dev_err(devi, CE_WARN, "Failed to create kstat entry");
			return (DDI_FAILURE);
		}

		entries[i] = ksp;
		ksp->ks_data = (void *)&ctemp_kstat_t;
		ksp->ks_lock = &ctemp_mutex;
		ksp->ks_update = ctemp_kstat_update;
		ksp->ks_private = (void *)cores[i].cpu;
		kstat_install(ksp);
	}
	return (DDI_SUCCESS);
}

static int
ctemp_detach(dev_info_t *devi, ddi_detach_cmd_t cmd)
{
	if (cmd != DDI_DETACH) {
		return (DDI_FAILURE);
	}
	ctemp_devi = NULL;

	int i;
	for (i = 0; i < ncpus; i++) {
		kstat_delete(entries[i]);
	}

	return (DDI_SUCCESS);
}

static struct dev_ops ctemp_dv_ops = {
	DEVO_REV,
	0,
	ctemp_getinfo,
	nulldev,    			/* identify */
	nulldev,    			/* probe */
	ctemp_attach,
	ctemp_detach,
	nodev,					/* reset */
	NULL,
	NULL,
	NULL,
	ddi_quiesce_not_needed,	/* quiesce */
};

static struct modldrv modldrv = {
	&mod_driverops,
	"coretemp driver 1.0",
	&ctemp_dv_ops
};

static struct modlinkage modl = {
	MODREV_1,
	&modldrv
};


int
_init(void)
{
	return (mod_install(&modl));
}

int
_fini(void)
{
	return (mod_remove(&modl));
}

int
_info(struct modinfo *modinfo)
{
	return (mod_info(&modl, modinfo));
}


/* private stuff */
int
intel_fill_fields(cpu_t *cpu)
{
	/* initialize data with common values */
	ctemp_cpuid(cpu, 0x06, &current_cpuid);

	/* initialize with invalid values */
	ctemp_kstat_t.tj_max.value.i32 = 100;
	ctemp_kstat_t.chip_temp.value.i32 = 0;
	ctemp_kstat_t.core_temp.value.i32 = 0;
	ctemp_kstat_t.target_temp.value.i32 = 0;
	ctemp_kstat_t.valid.value.i32 = 0;

	/* fill Tj_max information */
	ctemp_fill_tj_max(cpu);
	ctemp_fill_core_temp(cpu);
	ctemp_fill_pkg_temp(cpu);

	return (0);
}

int
ctemp_fill_fields(cpu_t *cpu)
{
	switch (x86_vendor) {
	case X86_VENDOR_Intel:
		intel_fill_fields(cpu);
	default:
		/* vendor not implemented (yet! :) ) */
		return (EINVAL);
	}

	return (0);

}

/* Execute RDMSR on specified CPU */
static void
ctemp_msr_req(uintptr_t req_ptr, uintptr_t error_ptr)
{

	label_t ljb;
	uint32_t msr_index;
	uint64_t *result;

	msr_index = ((msr_req_t *)req_ptr)->msr_index;
	result = ((msr_req_t *)req_ptr)->result;

	int error;

	if (on_fault(&ljb)) {
		dev_err(ctemp_devi, CE_WARN,
		    "Invalid rdmsr(0x%08" PRIx32 ")", (uint32_t)msr_index);
		error = EFAULT;
	} else {
		error = checked_rdmsr(msr_index, result);
	}

	*((int *)error_ptr) = error;

	return;

}

static int
ctemp_rdmsr(cpu_t *cpu, uint32_t msr_index, msr_regs_t *result)
{
	int error;

	msr_req_t request;
	request.msr_index = msr_index;
	request.result = (uint64_t *)result;

	cpu_call(cpu, (cpu_call_func_t)ctemp_msr_req,
	    (uintptr_t)&request, (uintptr_t)&error);

	return (error);
}

static void
ctemp_fill_tj_max(cpu_t *cpu)
{
	int model = cpuid_getmodel(cpu);

	if (model < 0x0e) {
		return;
	}

	if ((model == 0x1c) ||
	    (model == 0x26) ||
	    (model == 0x27) ||
	    (model == 0x35) ||
	    (model == 0x36)) {
		return;
	}

	msr_regs_t regs;
	if (ctemp_rdmsr(cpu, MSR_IA32_TEMPERATURE_TARGET, &regs) != 0) {
		return;
	}

	ctemp_kstat_t.tj_max.value.i32 = (regs.eax >> 16) & 0xff;
	ctemp_kstat_t.target_temp.value.i32 =
	    ctemp_kstat_t.tj_max.value.i32 - ((regs.eax >> 8) & 0xff);

}

static void
ctemp_fill_core_temp(cpu_t *cpu)
{
	if ((current_cpuid.cp_eax & 0x01) == 0) {
		return;
	}

	msr_regs_t regs;
	if (ctemp_rdmsr(cpu, MSR_IA32_THERM_STATUS, &regs) != 0) {
		return;
	}

	if (regs.eax & 0x80000000) {
		ctemp_kstat_t.core_temp.value.i32 =
		    ctemp_kstat_t.tj_max.value.i32 - ((regs.eax >> 16) & 0x7f);
		ctemp_kstat_t.valid.value.i32 = 1;
	}

}

static void
ctemp_fill_pkg_temp(cpu_t *cpu)
{
	ctemp_kstat_t.chip_temp.value.i32 = ctemp_kstat_t.core_temp.value.i32;

	if (((current_cpuid.cp_eax >> 6) & 0x01) == 0) {
		return;
	}

	msr_regs_t regs;

	if (ctemp_rdmsr(cpu, MSR_IA32_PACKAGE_THERM_STATUS, &regs) != 0) {
		return;
	}

	ctemp_kstat_t.chip_temp.value.i32 =
	    ctemp_kstat_t.tj_max.value.i32 - ((regs.eax >> 16) & 0x7f);

}

/* Execute CPUID on specified CPU */
static void
ctemp_cpuid(cpu_t *cpu, uint32_t cpuid_func,
    struct cpuid_regs *result)
{
	result->cp_eax = cpuid_func;
	result->cp_ebx = result->cp_ecx = result->cp_edx = 0;
	(void) cpuid_insn(cpu, result);
}
