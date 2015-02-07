

#include <linux/sysdev.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/pm.h>
#include <linux/pci.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define PREFIX "ACPI: "

#define _COMPONENT			ACPI_PCI_COMPONENT
ACPI_MODULE_NAME("pci_link");
#define ACPI_PCI_LINK_CLASS		"pci_irq_routing"
#define ACPI_PCI_LINK_DEVICE_NAME	"PCI Interrupt Link"
#define ACPI_PCI_LINK_FILE_INFO		"info"
#define ACPI_PCI_LINK_FILE_STATUS	"state"
#define ACPI_PCI_LINK_MAX_POSSIBLE	16

static int acpi_pci_link_add(struct acpi_device *device);
static int acpi_pci_link_remove(struct acpi_device *device, int type);

static const struct acpi_device_id link_device_ids[] = {
	{"PNP0C0F", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, link_device_ids);

static struct acpi_driver acpi_pci_link_driver = {
	.name = "pci_link",
	.class = ACPI_PCI_LINK_CLASS,
	.ids = link_device_ids,
	.ops = {
		.add = acpi_pci_link_add,
		.remove = acpi_pci_link_remove,
	},
};

struct acpi_pci_link_irq {
	u8 active;		/* Current IRQ */
	u8 triggering;		/* All IRQs */
	u8 polarity;		/* All IRQs */
	u8 resource_type;
	u8 possible_count;
	u8 possible[ACPI_PCI_LINK_MAX_POSSIBLE];
	u8 initialized:1;
	u8 reserved:7;
};

struct acpi_pci_link {
	struct list_head		list;
	struct acpi_device		*device;
	struct acpi_pci_link_irq	irq;
	int				refcnt;
};

static LIST_HEAD(acpi_link_list);
static DEFINE_MUTEX(acpi_link_lock);


static acpi_status acpi_pci_link_check_possible(struct acpi_resource *resource,
						void *context)
{
	struct acpi_pci_link *link = context;
	u32 i;

	switch (resource->type) {
	case ACPI_RESOURCE_TYPE_START_DEPENDENT:
	case ACPI_RESOURCE_TYPE_END_TAG:
		return AE_OK;
	case ACPI_RESOURCE_TYPE_IRQ:
		{
			struct acpi_resource_irq *p = &resource->data.irq;
			if (!p || !p->interrupt_count) {
				ACPI_DEBUG_PRINT((ACPI_DB_INFO,
						  "Blank _PRS IRQ resource\n"));
				return AE_OK;
			}
			for (i = 0;
			     (i < p->interrupt_count
			      && i < ACPI_PCI_LINK_MAX_POSSIBLE); i++) {
				if (!p->interrupts[i]) {
					printk(KERN_WARNING PREFIX
					       "Invalid _PRS IRQ %d\n",
					       p->interrupts[i]);
					continue;
				}
				link->irq.possible[i] = p->interrupts[i];
				link->irq.possible_count++;
			}
			link->irq.triggering = p->triggering;
			link->irq.polarity = p->polarity;
			link->irq.resource_type = ACPI_RESOURCE_TYPE_IRQ;
			break;
		}
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		{
			struct acpi_resource_extended_irq *p =
			    &resource->data.extended_irq;
			if (!p || !p->interrupt_count) {
				printk(KERN_WARNING PREFIX
					      "Blank _PRS EXT IRQ resource\n");
				return AE_OK;
			}
			for (i = 0;
			     (i < p->interrupt_count
			      && i < ACPI_PCI_LINK_MAX_POSSIBLE); i++) {
				if (!p->interrupts[i]) {
					printk(KERN_WARNING PREFIX
					       "Invalid _PRS IRQ %d\n",
					       p->interrupts[i]);
					continue;
				}
				link->irq.possible[i] = p->interrupts[i];
				link->irq.possible_count++;
			}
			link->irq.triggering = p->triggering;
			link->irq.polarity = p->polarity;
			link->irq.resource_type = ACPI_RESOURCE_TYPE_EXTENDED_IRQ;
			break;
		}
	default:
		printk(KERN_ERR PREFIX "_PRS resource type 0x%x isn't an IRQ\n",
		       resource->type);
		return AE_OK;
	}

	return AE_CTRL_TERMINATE;
}

static int acpi_pci_link_get_possible(struct acpi_pci_link *link)
{
	acpi_status status;

	status = acpi_walk_resources(link->device->handle, METHOD_NAME__PRS,
				     acpi_pci_link_check_possible, link);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _PRS"));
		return -ENODEV;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Found %d possible IRQs\n",
			  link->irq.possible_count));

	return 0;
}

static acpi_status acpi_pci_link_check_current(struct acpi_resource *resource,
					       void *context)
{
	int *irq = context;

	switch (resource->type) {
	case ACPI_RESOURCE_TYPE_START_DEPENDENT:
	case ACPI_RESOURCE_TYPE_END_TAG:
		return AE_OK;
	case ACPI_RESOURCE_TYPE_IRQ:
		{
			struct acpi_resource_irq *p = &resource->data.irq;
			if (!p || !p->interrupt_count) {
				/*
				 * IRQ descriptors may have no IRQ# bits set,
				 * particularly those those w/ _STA disabled
				 */
				ACPI_DEBUG_PRINT((ACPI_DB_INFO,
						  "Blank _CRS IRQ resource\n"));
				return AE_OK;
			}
			*irq = p->interrupts[0];
			break;
		}
	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		{
			struct acpi_resource_extended_irq *p =
			    &resource->data.extended_irq;
			if (!p || !p->interrupt_count) {
				/*
				 * extended IRQ descriptors must
				 * return at least 1 IRQ
				 */
				printk(KERN_WARNING PREFIX
					      "Blank _CRS EXT IRQ resource\n");
				return AE_OK;
			}
			*irq = p->interrupts[0];
			break;
		}
		break;
	default:
		printk(KERN_ERR PREFIX "_CRS resource type 0x%x isn't an IRQ\n",
		       resource->type);
		return AE_OK;
	}

	return AE_CTRL_TERMINATE;
}

static int acpi_pci_link_get_current(struct acpi_pci_link *link)
{
	int result = 0;
	acpi_status status;
	int irq = 0;

	link->irq.active = 0;

	/* in practice, status disabled is meaningless, ignore it */
	if (acpi_strict) {
		/* Query _STA, set link->device->status */
		result = acpi_bus_get_status(link->device);
		if (result) {
			printk(KERN_ERR PREFIX "Unable to read status\n");
			goto end;
		}

		if (!link->device->status.enabled) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Link disabled\n"));
			return 0;
		}
	}

	/* 
	 * Query and parse _CRS to get the current IRQ assignment. 
	 */

	status = acpi_walk_resources(link->device->handle, METHOD_NAME__CRS,
				     acpi_pci_link_check_current, &irq);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _CRS"));
		result = -ENODEV;
		goto end;
	}

	if (acpi_strict && !irq) {
		printk(KERN_ERR PREFIX "_CRS returned 0\n");
		result = -ENODEV;
	}

	link->irq.active = irq;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Link at IRQ %d \n", link->irq.active));

      end:
	return result;
}

static int acpi_pci_link_set(struct acpi_pci_link *link, int irq)
{
	int result;
	acpi_status status;
	struct {
		struct acpi_resource res;
		struct acpi_resource end;
	} *resource;
	struct acpi_buffer buffer = { 0, NULL };

	if (!irq)
		return -EINVAL;

	resource = kzalloc(sizeof(*resource) + 1, irqs_disabled() ? GFP_ATOMIC: GFP_KERNEL);
	if (!resource)
		return -ENOMEM;

	buffer.length = sizeof(*resource) + 1;
	buffer.pointer = resource;

	switch (link->irq.resource_type) {
	case ACPI_RESOURCE_TYPE_IRQ:
		resource->res.type = ACPI_RESOURCE_TYPE_IRQ;
		resource->res.length = sizeof(struct acpi_resource);
		resource->res.data.irq.triggering = link->irq.triggering;
		resource->res.data.irq.polarity =
		    link->irq.polarity;
		if (link->irq.triggering == ACPI_EDGE_SENSITIVE)
			resource->res.data.irq.sharable =
			    ACPI_EXCLUSIVE;
		else
			resource->res.data.irq.sharable = ACPI_SHARED;
		resource->res.data.irq.interrupt_count = 1;
		resource->res.data.irq.interrupts[0] = irq;
		break;

	case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
		resource->res.type = ACPI_RESOURCE_TYPE_EXTENDED_IRQ;
		resource->res.length = sizeof(struct acpi_resource);
		resource->res.data.extended_irq.producer_consumer =
		    ACPI_CONSUMER;
		resource->res.data.extended_irq.triggering =
		    link->irq.triggering;
		resource->res.data.extended_irq.polarity =
		    link->irq.polarity;
		if (link->irq.triggering == ACPI_EDGE_SENSITIVE)
			resource->res.data.irq.sharable =
			    ACPI_EXCLUSIVE;
		else
			resource->res.data.irq.sharable = ACPI_SHARED;
		resource->res.data.extended_irq.interrupt_count = 1;
		resource->res.data.extended_irq.interrupts[0] = irq;
		/* ignore resource_source, it's optional */
		break;
	default:
		printk(KERN_ERR PREFIX "Invalid Resource_type %d\n", link->irq.resource_type);
		result = -EINVAL;
		goto end;

	}
	resource->end.type = ACPI_RESOURCE_TYPE_END_TAG;

	/* Attempt to set the resource */
	status = acpi_set_current_resources(link->device->handle, &buffer);

	/* check for total failure */
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _SRS"));
		result = -ENODEV;
		goto end;
	}

	/* Query _STA, set device->status */
	result = acpi_bus_get_status(link->device);
	if (result) {
		printk(KERN_ERR PREFIX "Unable to read status\n");
		goto end;
	}
	if (!link->device->status.enabled) {
		printk(KERN_WARNING PREFIX
			      "%s [%s] disabled and referenced, BIOS bug\n",
			      acpi_device_name(link->device),
			      acpi_device_bid(link->device));
	}

	/* Query _CRS, set link->irq.active */
	result = acpi_pci_link_get_current(link);
	if (result) {
		goto end;
	}

	/*
	 * Is current setting not what we set?
	 * set link->irq.active
	 */
	if (link->irq.active != irq) {
		/*
		 * policy: when _CRS doesn't return what we just _SRS
		 * assume _SRS worked and override _CRS value.
		 */
		printk(KERN_WARNING PREFIX
			      "%s [%s] BIOS reported IRQ %d, using IRQ %d\n",
			      acpi_device_name(link->device),
			      acpi_device_bid(link->device), link->irq.active, irq);
		link->irq.active = irq;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Set IRQ %d\n", link->irq.active));

      end:
	kfree(resource);
	return result;
}



#define ACPI_MAX_IRQS		256
#define ACPI_MAX_ISA_IRQ	16

#define PIRQ_PENALTY_PCI_AVAILABLE	(0)
#define PIRQ_PENALTY_PCI_POSSIBLE	(16*16)
#define PIRQ_PENALTY_PCI_USING		(16*16*16)
#define PIRQ_PENALTY_ISA_TYPICAL	(16*16*16*16)
#define PIRQ_PENALTY_ISA_USED		(16*16*16*16*16)
#define PIRQ_PENALTY_ISA_ALWAYS		(16*16*16*16*16*16)

static int acpi_irq_penalty[ACPI_MAX_IRQS] = {
	PIRQ_PENALTY_ISA_ALWAYS,	/* IRQ0 timer */
	PIRQ_PENALTY_ISA_ALWAYS,	/* IRQ1 keyboard */
	PIRQ_PENALTY_ISA_ALWAYS,	/* IRQ2 cascade */
	PIRQ_PENALTY_ISA_TYPICAL,	/* IRQ3 serial */
	PIRQ_PENALTY_ISA_TYPICAL,	/* IRQ4 serial */
	PIRQ_PENALTY_ISA_TYPICAL,	/* IRQ5 sometimes SoundBlaster */
	PIRQ_PENALTY_ISA_TYPICAL,	/* IRQ6 */
	PIRQ_PENALTY_ISA_TYPICAL,	/* IRQ7 parallel, spurious */
	PIRQ_PENALTY_ISA_TYPICAL,	/* IRQ8 rtc, sometimes */
	PIRQ_PENALTY_PCI_AVAILABLE,	/* IRQ9  PCI, often acpi */
	PIRQ_PENALTY_PCI_AVAILABLE,	/* IRQ10 PCI */
	PIRQ_PENALTY_PCI_AVAILABLE,	/* IRQ11 PCI */
	PIRQ_PENALTY_ISA_USED,		/* IRQ12 mouse */
	PIRQ_PENALTY_ISA_USED,		/* IRQ13 fpe, sometimes */
	PIRQ_PENALTY_ISA_USED,		/* IRQ14 ide0 */
	PIRQ_PENALTY_ISA_USED,		/* IRQ15 ide1 */
	/* >IRQ15 */
};

int __init acpi_irq_penalty_init(void)
{
	struct acpi_pci_link *link;
	int i;

	/*
	 * Update penalties to facilitate IRQ balancing.
	 */
	list_for_each_entry(link, &acpi_link_list, list) {

		/*
		 * reflect the possible and active irqs in the penalty table --
		 * useful for breaking ties.
		 */
		if (link->irq.possible_count) {
			int penalty =
			    PIRQ_PENALTY_PCI_POSSIBLE /
			    link->irq.possible_count;

			for (i = 0; i < link->irq.possible_count; i++) {
				if (link->irq.possible[i] < ACPI_MAX_ISA_IRQ)
					acpi_irq_penalty[link->irq.
							 possible[i]] +=
					    penalty;
			}

		} else if (link->irq.active) {
			acpi_irq_penalty[link->irq.active] +=
			    PIRQ_PENALTY_PCI_POSSIBLE;
		}
	}
	/* Add a penalty for the SCI */
	acpi_irq_penalty[acpi_gbl_FADT.sci_interrupt] += PIRQ_PENALTY_PCI_USING;
	return 0;
}

static int acpi_irq_balance = -1;	/* 0: static, 1: balance */

static int acpi_pci_link_allocate(struct acpi_pci_link *link)
{
	int irq;
	int i;

	if (link->irq.initialized) {
		if (link->refcnt == 0)
			/* This means the link is disabled but initialized */
			acpi_pci_link_set(link, link->irq.active);
		return 0;
	}

	/*
	 * search for active IRQ in list of possible IRQs.
	 */
	for (i = 0; i < link->irq.possible_count; ++i) {
		if (link->irq.active == link->irq.possible[i])
			break;
	}
	/*
	 * forget active IRQ that is not in possible list
	 */
	if (i == link->irq.possible_count) {
		if (acpi_strict)
			printk(KERN_WARNING PREFIX "_CRS %d not found"
				      " in _PRS\n", link->irq.active);
		link->irq.active = 0;
	}

	/*
	 * if active found, use it; else pick entry from end of possible list.
	 */
	if (link->irq.active)
		irq = link->irq.active;
	else
		irq = link->irq.possible[link->irq.possible_count - 1];

	if (acpi_irq_balance || !link->irq.active) {
		/*
		 * Select the best IRQ.  This is done in reverse to promote
		 * the use of IRQs 9, 10, 11, and >15.
		 */
		for (i = (link->irq.possible_count - 1); i >= 0; i--) {
			if (acpi_irq_penalty[irq] >
			    acpi_irq_penalty[link->irq.possible[i]])
				irq = link->irq.possible[i];
		}
	}

	/* Attempt to enable the link device at this IRQ. */
	if (acpi_pci_link_set(link, irq)) {
		printk(KERN_ERR PREFIX "Unable to set IRQ for %s [%s]. "
			    "Try pci=noacpi or acpi=off\n",
			    acpi_device_name(link->device),
			    acpi_device_bid(link->device));
		return -ENODEV;
	} else {
		acpi_irq_penalty[link->irq.active] += PIRQ_PENALTY_PCI_USING;
		printk(KERN_WARNING PREFIX "%s [%s] enabled at IRQ %d\n",
		       acpi_device_name(link->device),
		       acpi_device_bid(link->device), link->irq.active);
	}

	link->irq.initialized = 1;
	return 0;
}

int acpi_pci_link_allocate_irq(acpi_handle handle, int index, int *triggering,
			       int *polarity, char **name)
{
	int result;
	struct acpi_device *device;
	struct acpi_pci_link *link;

	result = acpi_bus_get_device(handle, &device);
	if (result) {
		printk(KERN_ERR PREFIX "Invalid link device\n");
		return -1;
	}

	link = acpi_driver_data(device);
	if (!link) {
		printk(KERN_ERR PREFIX "Invalid link context\n");
		return -1;
	}

	/* TBD: Support multiple index (IRQ) entries per Link Device */
	if (index) {
		printk(KERN_ERR PREFIX "Invalid index %d\n", index);
		return -1;
	}

	mutex_lock(&acpi_link_lock);
	if (acpi_pci_link_allocate(link)) {
		mutex_unlock(&acpi_link_lock);
		return -1;
	}

	if (!link->irq.active) {
		mutex_unlock(&acpi_link_lock);
		printk(KERN_ERR PREFIX "Link active IRQ is 0!\n");
		return -1;
	}
	link->refcnt++;
	mutex_unlock(&acpi_link_lock);

	if (triggering)
		*triggering = link->irq.triggering;
	if (polarity)
		*polarity = link->irq.polarity;
	if (name)
		*name = acpi_device_bid(link->device);
	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Link %s is referenced\n",
			  acpi_device_bid(link->device)));
	return (link->irq.active);
}

int acpi_pci_link_free_irq(acpi_handle handle)
{
	struct acpi_device *device;
	struct acpi_pci_link *link;
	acpi_status result;

	result = acpi_bus_get_device(handle, &device);
	if (result) {
		printk(KERN_ERR PREFIX "Invalid link device\n");
		return -1;
	}

	link = acpi_driver_data(device);
	if (!link) {
		printk(KERN_ERR PREFIX "Invalid link context\n");
		return -1;
	}

	mutex_lock(&acpi_link_lock);
	if (!link->irq.initialized) {
		mutex_unlock(&acpi_link_lock);
		printk(KERN_ERR PREFIX "Link isn't initialized\n");
		return -1;
	}
#ifdef	FUTURE_USE
	/*
	 * The Link reference count allows us to _DISable an unused link
	 * and suspend time, and set it again  on resume.
	 * However, 2.6.12 still has irq_router.resume
	 * which blindly restores the link state.
	 * So we disable the reference count method
	 * to prevent duplicate acpi_pci_link_set()
	 * which would harm some systems
	 */
	link->refcnt--;
#endif
	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Link %s is dereferenced\n",
			  acpi_device_bid(link->device)));

	if (link->refcnt == 0)
		acpi_evaluate_object(link->device->handle, "_DIS", NULL, NULL);

	mutex_unlock(&acpi_link_lock);
	return (link->irq.active);
}


static int acpi_pci_link_add(struct acpi_device *device)
{
	int result;
	struct acpi_pci_link *link;
	int i;
	int found = 0;

	link = kzalloc(sizeof(struct acpi_pci_link), GFP_KERNEL);
	if (!link)
		return -ENOMEM;

	link->device = device;
	strcpy(acpi_device_name(device), ACPI_PCI_LINK_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_PCI_LINK_CLASS);
	device->driver_data = link;

	mutex_lock(&acpi_link_lock);
	result = acpi_pci_link_get_possible(link);
	if (result)
		goto end;

	/* query and set link->irq.active */
	acpi_pci_link_get_current(link);

	printk(KERN_INFO PREFIX "%s [%s] (IRQs", acpi_device_name(device),
	       acpi_device_bid(device));
	for (i = 0; i < link->irq.possible_count; i++) {
		if (link->irq.active == link->irq.possible[i]) {
			printk(" *%d", link->irq.possible[i]);
			found = 1;
		} else
			printk(" %d", link->irq.possible[i]);
	}

	printk(")");

	if (!found)
		printk(" *%d", link->irq.active);

	if (!link->device->status.enabled)
		printk(", disabled.");

	printk("\n");

	list_add_tail(&link->list, &acpi_link_list);

      end:
	/* disable all links -- to be activated on use */
	acpi_evaluate_object(device->handle, "_DIS", NULL, NULL);
	mutex_unlock(&acpi_link_lock);

	if (result)
		kfree(link);

	return result;
}

static int acpi_pci_link_resume(struct acpi_pci_link *link)
{
	if (link->refcnt && link->irq.active && link->irq.initialized)
		return (acpi_pci_link_set(link, link->irq.active));

	return 0;
}

static int irqrouter_resume(struct sys_device *dev)
{
	struct acpi_pci_link *link;

	list_for_each_entry(link, &acpi_link_list, list) {
		acpi_pci_link_resume(link);
	}
	return 0;
}

static int acpi_pci_link_remove(struct acpi_device *device, int type)
{
	struct acpi_pci_link *link;

	link = acpi_driver_data(device);

	mutex_lock(&acpi_link_lock);
	list_del(&link->list);
	mutex_unlock(&acpi_link_lock);

	kfree(link);
	return 0;
}

static int __init acpi_irq_penalty_update(char *str, int used)
{
	int i;

	for (i = 0; i < 16; i++) {
		int retval;
		int irq;

		retval = get_option(&str, &irq);

		if (!retval)
			break;	/* no number found */

		if (irq < 0)
			continue;

		if (irq >= ARRAY_SIZE(acpi_irq_penalty))
			continue;

		if (used)
			acpi_irq_penalty[irq] += PIRQ_PENALTY_ISA_USED;
		else
			acpi_irq_penalty[irq] = PIRQ_PENALTY_PCI_AVAILABLE;

		if (retval != 2)	/* no next number */
			break;
	}
	return 1;
}

void acpi_penalize_isa_irq(int irq, int active)
{
	if (irq >= 0 && irq < ARRAY_SIZE(acpi_irq_penalty)) {
		if (active)
			acpi_irq_penalty[irq] += PIRQ_PENALTY_ISA_USED;
		else
			acpi_irq_penalty[irq] += PIRQ_PENALTY_PCI_USING;
	}
}

static int __init acpi_irq_isa(char *str)
{
	return acpi_irq_penalty_update(str, 1);
}

__setup("acpi_irq_isa=", acpi_irq_isa);

static int __init acpi_irq_pci(char *str)
{
	return acpi_irq_penalty_update(str, 0);
}

__setup("acpi_irq_pci=", acpi_irq_pci);

static int __init acpi_irq_nobalance_set(char *str)
{
	acpi_irq_balance = 0;
	return 1;
}

__setup("acpi_irq_nobalance", acpi_irq_nobalance_set);

static int __init acpi_irq_balance_set(char *str)
{
	acpi_irq_balance = 1;
	return 1;
}

__setup("acpi_irq_balance", acpi_irq_balance_set);

/* FIXME: we will remove this interface after all drivers call pci_disable_device */
static struct sysdev_class irqrouter_sysdev_class = {
	.name = "irqrouter",
	.resume = irqrouter_resume,
};

static struct sys_device device_irqrouter = {
	.id = 0,
	.cls = &irqrouter_sysdev_class,
};

static int __init irqrouter_init_sysfs(void)
{
	int error;

	if (acpi_disabled || acpi_noirq)
		return 0;

	error = sysdev_class_register(&irqrouter_sysdev_class);
	if (!error)
		error = sysdev_register(&device_irqrouter);

	return error;
}

device_initcall(irqrouter_init_sysfs);

static int __init acpi_pci_link_init(void)
{
	if (acpi_noirq)
		return 0;

	if (acpi_irq_balance == -1) {
		/* no command line switch: enable balancing in IOAPIC mode */
		if (acpi_irq_model == ACPI_IRQ_MODEL_IOAPIC)
			acpi_irq_balance = 1;
		else
			acpi_irq_balance = 0;
	}

	if (acpi_bus_register_driver(&acpi_pci_link_driver) < 0)
		return -ENODEV;

	return 0;
}

subsys_initcall(acpi_pci_link_init);
