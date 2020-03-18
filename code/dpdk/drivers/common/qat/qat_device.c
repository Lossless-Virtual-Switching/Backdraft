/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Intel Corporation
 */

#include <rte_string_fns.h>
#include <rte_devargs.h>
#include <ctype.h>

#include "qat_device.h"
#include "adf_transport_access_macros.h"
#include "qat_sym_pmd.h"
#include "qat_comp_pmd.h"

/* Hardware device information per generation */
__extension__
struct qat_gen_hw_data qat_gen_config[] =  {
	[QAT_GEN1] = {
		.dev_gen = QAT_GEN1,
		.qp_hw_data = qat_gen1_qps,
		.comp_num_im_bufs_required = QAT_NUM_INTERM_BUFS_GEN1
	},
	[QAT_GEN2] = {
		.dev_gen = QAT_GEN2,
		.qp_hw_data = qat_gen1_qps,
		/* gen2 has same ring layout as gen1 */
		.comp_num_im_bufs_required = QAT_NUM_INTERM_BUFS_GEN2
	},
	[QAT_GEN3] = {
		.dev_gen = QAT_GEN3,
		.qp_hw_data = qat_gen3_qps,
		.comp_num_im_bufs_required = QAT_NUM_INTERM_BUFS_GEN3
	},
};


static struct qat_pci_device qat_pci_devices[RTE_PMD_QAT_MAX_PCI_DEVICES];
static int qat_nb_pci_devices;

/*
 * The set of PCI devices this driver supports
 */

static const struct rte_pci_id pci_id_qat_map[] = {
		{
			RTE_PCI_DEVICE(0x8086, 0x0443),
		},
		{
			RTE_PCI_DEVICE(0x8086, 0x37c9),
		},
		{
			RTE_PCI_DEVICE(0x8086, 0x19e3),
		},
		{
			RTE_PCI_DEVICE(0x8086, 0x6f55),
		},
		{
			RTE_PCI_DEVICE(0x8086, 0x18a1),
		},
		{.device_id = 0},
};

static struct qat_pci_device *
qat_pci_get_dev(uint8_t dev_id)
{
	return &qat_pci_devices[dev_id];
}

static struct qat_pci_device *
qat_pci_get_named_dev(const char *name)
{
	struct qat_pci_device *dev;
	unsigned int i;

	if (name == NULL)
		return NULL;

	for (i = 0; i < RTE_PMD_QAT_MAX_PCI_DEVICES; i++) {
		dev = &qat_pci_devices[i];

		if ((dev->attached == QAT_ATTACHED) &&
				(strcmp(dev->name, name) == 0))
			return dev;
	}

	return NULL;
}

static uint8_t
qat_pci_find_free_device_index(void)
{
	uint8_t dev_id;

	for (dev_id = 0; dev_id < RTE_PMD_QAT_MAX_PCI_DEVICES; dev_id++) {
		if (qat_pci_devices[dev_id].attached == QAT_DETACHED)
			break;
	}
	return dev_id;
}

struct qat_pci_device *
qat_get_qat_dev_from_pci_dev(struct rte_pci_device *pci_dev)
{
	char name[QAT_DEV_NAME_MAX_LEN];

	rte_pci_device_name(&pci_dev->addr, name, sizeof(name));

	return qat_pci_get_named_dev(name);
}

static void qat_dev_parse_cmd(const char *str, struct qat_dev_cmd_param
		*qat_dev_cmd_param)
{
	int i = 0;
	const char *param;

	while (1) {
		char value_str[4] = { };

		param = qat_dev_cmd_param[i].name;
		if (param == NULL)
			return;
		long value = 0;
		const char *arg = strstr(str, param);
		const char *arg2 = NULL;

		if (arg) {
			arg2 = arg + strlen(param);
			if (*arg2 != '=') {
				QAT_LOG(DEBUG, "parsing error '=' sign"
						" should immediately follow %s",
						param);
				arg2 = NULL;
			} else
				arg2++;
		} else {
			QAT_LOG(DEBUG, "%s not provided", param);
		}
		if (arg2) {
			int iter = 0;
			while (iter < 2) {
				if (!isdigit(*(arg2 + iter)))
					break;
				iter++;
			}
			if (!iter) {
				QAT_LOG(DEBUG, "parsing error %s"
					       " no number provided",
					       param);
			} else {
				memcpy(value_str, arg2, iter);
				value = strtol(value_str, NULL, 10);
				if (value > MAX_QP_THRESHOLD_SIZE) {
					QAT_LOG(DEBUG, "Exceeded max size of"
						" threshold, setting to %d",
						MAX_QP_THRESHOLD_SIZE);
					value = MAX_QP_THRESHOLD_SIZE;
				}
				QAT_LOG(DEBUG, "parsing %s = %ld",
						param, value);
			}
		}
		qat_dev_cmd_param[i].val = value;
		i++;
	}
}

struct qat_pci_device *
qat_pci_device_allocate(struct rte_pci_device *pci_dev,
		struct qat_dev_cmd_param *qat_dev_cmd_param)
{
	struct qat_pci_device *qat_dev;
	uint8_t qat_dev_id;
	char name[QAT_DEV_NAME_MAX_LEN];
	struct rte_devargs *devargs = pci_dev->device.devargs;

	rte_pci_device_name(&pci_dev->addr, name, sizeof(name));
	snprintf(name+strlen(name), QAT_DEV_NAME_MAX_LEN-strlen(name), "_qat");
	if (qat_pci_get_named_dev(name) != NULL) {
		QAT_LOG(ERR, "QAT device with name %s already allocated!",
				name);
		return NULL;
	}

	qat_dev_id = qat_pci_find_free_device_index();
	if (qat_dev_id == RTE_PMD_QAT_MAX_PCI_DEVICES) {
		QAT_LOG(ERR, "Reached maximum number of QAT devices");
		return NULL;
	}

	qat_dev = qat_pci_get_dev(qat_dev_id);
	memset(qat_dev, 0, sizeof(*qat_dev));
	strlcpy(qat_dev->name, name, QAT_DEV_NAME_MAX_LEN);
	qat_dev->qat_dev_id = qat_dev_id;
	qat_dev->pci_dev = pci_dev;
	switch (qat_dev->pci_dev->id.device_id) {
	case 0x0443:
		qat_dev->qat_dev_gen = QAT_GEN1;
		break;
	case 0x37c9:
	case 0x19e3:
	case 0x6f55:
		qat_dev->qat_dev_gen = QAT_GEN2;
		break;
	case 0x18a1:
		qat_dev->qat_dev_gen = QAT_GEN3;
		break;
	default:
		QAT_LOG(ERR, "Invalid dev_id, can't determine generation");
		return NULL;
	}

	if (devargs && devargs->drv_str)
		qat_dev_parse_cmd(devargs->drv_str, qat_dev_cmd_param);

	rte_spinlock_init(&qat_dev->arb_csr_lock);

	qat_dev->attached = QAT_ATTACHED;

	qat_nb_pci_devices++;

	QAT_LOG(DEBUG, "QAT device %d allocated, name %s, total QATs %d",
			qat_dev->qat_dev_id, qat_dev->name, qat_nb_pci_devices);

	return qat_dev;
}

int
qat_pci_device_release(struct rte_pci_device *pci_dev)
{
	struct qat_pci_device *qat_dev;
	char name[QAT_DEV_NAME_MAX_LEN];

	if (pci_dev == NULL)
		return -EINVAL;

	rte_pci_device_name(&pci_dev->addr, name, sizeof(name));
	snprintf(name+strlen(name), QAT_DEV_NAME_MAX_LEN-strlen(name), "_qat");
	qat_dev = qat_pci_get_named_dev(name);
	if (qat_dev != NULL) {

		/* Check that there are no service devs still on pci device */
		if (qat_dev->sym_dev != NULL)
			return -EBUSY;

		qat_dev->attached = QAT_DETACHED;
		qat_nb_pci_devices--;
	}
	QAT_LOG(DEBUG, "QAT device %s released, total QATs %d",
				name, qat_nb_pci_devices);
	return 0;
}

static int
qat_pci_dev_destroy(struct qat_pci_device *qat_pci_dev,
		struct rte_pci_device *pci_dev)
{
	qat_sym_dev_destroy(qat_pci_dev);
	qat_comp_dev_destroy(qat_pci_dev);
	qat_asym_dev_destroy(qat_pci_dev);
	return qat_pci_device_release(pci_dev);
}

static int qat_pci_probe(struct rte_pci_driver *pci_drv __rte_unused,
		struct rte_pci_device *pci_dev)
{
	int sym_ret = 0, asym_ret = 0, comp_ret = 0;
	int num_pmds_created = 0;
	struct qat_pci_device *qat_pci_dev;
	struct qat_dev_cmd_param qat_dev_cmd_param[] = {
			{ SYM_ENQ_THRESHOLD_NAME, 0 },
			{ ASYM_ENQ_THRESHOLD_NAME, 0 },
			{ COMP_ENQ_THRESHOLD_NAME, 0 },
			{ NULL, 0 },
	};

	QAT_LOG(DEBUG, "Found QAT device at %02x:%02x.%x",
			pci_dev->addr.bus,
			pci_dev->addr.devid,
			pci_dev->addr.function);

	qat_pci_dev = qat_pci_device_allocate(pci_dev, qat_dev_cmd_param);
	if (qat_pci_dev == NULL)
		return -ENODEV;

	sym_ret = qat_sym_dev_create(qat_pci_dev, qat_dev_cmd_param);
	if (sym_ret == 0) {
		num_pmds_created++;

	}
	else
		QAT_LOG(WARNING,
				"Failed to create QAT SYM PMD on device %s",
				qat_pci_dev->name);

	comp_ret = qat_comp_dev_create(qat_pci_dev, qat_dev_cmd_param);
	if (comp_ret == 0)
		num_pmds_created++;
	else
		QAT_LOG(WARNING,
				"Failed to create QAT COMP PMD on device %s",
				qat_pci_dev->name);

	asym_ret = qat_asym_dev_create(qat_pci_dev, qat_dev_cmd_param);
	if (asym_ret == 0)
		num_pmds_created++;
	else
		QAT_LOG(WARNING,
				"Failed to create QAT ASYM PMD on device %s",
				qat_pci_dev->name);

	if (num_pmds_created == 0)
		qat_pci_dev_destroy(qat_pci_dev, pci_dev);

	return 0;
}

static int qat_pci_remove(struct rte_pci_device *pci_dev)
{
	struct qat_pci_device *qat_pci_dev;

	if (pci_dev == NULL)
		return -EINVAL;

	qat_pci_dev = qat_get_qat_dev_from_pci_dev(pci_dev);
	if (qat_pci_dev == NULL)
		return 0;

	return qat_pci_dev_destroy(qat_pci_dev, pci_dev);
}

static struct rte_pci_driver rte_qat_pmd = {
	.id_table = pci_id_qat_map,
	.drv_flags = RTE_PCI_DRV_NEED_MAPPING,
	.probe = qat_pci_probe,
	.remove = qat_pci_remove
};

__rte_weak int
qat_sym_dev_create(struct qat_pci_device *qat_pci_dev __rte_unused,
		struct qat_dev_cmd_param *qat_dev_cmd_param __rte_unused)
{
	return 0;
}

__rte_weak int
qat_asym_dev_create(struct qat_pci_device *qat_pci_dev __rte_unused,
		struct qat_dev_cmd_param *qat_dev_cmd_param __rte_unused)
{
	return 0;
}

__rte_weak int
qat_sym_dev_destroy(struct qat_pci_device *qat_pci_dev __rte_unused)
{
	return 0;
}

__rte_weak int
qat_asym_dev_destroy(struct qat_pci_device *qat_pci_dev __rte_unused)
{
	return 0;
}

__rte_weak int
qat_comp_dev_create(struct qat_pci_device *qat_pci_dev __rte_unused,
		struct qat_dev_cmd_param *qat_dev_cmd_param __rte_unused)
{
	return 0;
}

__rte_weak int
qat_comp_dev_destroy(struct qat_pci_device *qat_pci_dev __rte_unused)
{
	return 0;
}

RTE_PMD_REGISTER_PCI(QAT_PCI_NAME, rte_qat_pmd);
RTE_PMD_REGISTER_PCI_TABLE(QAT_PCI_NAME, pci_id_qat_map);
