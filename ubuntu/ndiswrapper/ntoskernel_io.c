/*
 *  Copyright (C) 2003-2005 Pontus Fuchs, Giridhar Pemmasani
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 */

#include "ntoskernel.h"
#include "ndis.h"
#include "wrapndis.h"
#include "usb.h"
#include "loader.h"
#include "ntoskernel_io_exports.h"

wstdcall void WIN_FUNC(IoAcquireCancelSpinLock,1)
	(KIRQL *irql) __acquires(irql)
{
	spin_lock_bh(&irp_cancel_lock);
	*irql = 0;
}

wstdcall void WIN_FUNC(IoReleaseCancelSpinLock,1)
	(KIRQL irql) __releases(irql)
{
	spin_unlock_bh(&irp_cancel_lock);
}

wstdcall int WIN_FUNC(IoIsWdmVersionAvailable,2)
	(UCHAR major, UCHAR minor)
{
	IOENTER("%d, %x", major, minor);
	if (major == 1 &&
	    (minor == 0x30 || // Windows 2003
	     minor == 0x20 || // Windows XP
	     minor == 0x10)) // Windows 2000
		IOEXIT(return TRUE);
	IOEXIT(return FALSE);
}

wstdcall BOOLEAN WIN_FUNC(IoIs32bitProcess,1)
	(struct irp *irp)
{
#ifdef CONFIG_X86_64
	return FALSE;
#else
	return TRUE;
#endif
}

wstdcall void WIN_FUNC(IoInitializeIrp,3)
	(struct irp *irp, USHORT size, CCHAR stack_count)
{
	IOENTER("irp: %p, %d, %d", irp, size, stack_count);

	memset(irp, 0, size);
	irp->size = size;
	irp->stack_count = stack_count;
	irp->current_location = stack_count;
	IoGetCurrentIrpStackLocation(irp) = IRP_SL(irp, stack_count);
	IOEXIT(return);
}

wstdcall void WIN_FUNC(IoReuseIrp,2)
	(struct irp *irp, NTSTATUS status)
{
	IOENTER("%p, %d", irp, status);
	if (irp) {
		UCHAR alloc_flags;

		alloc_flags = irp->alloc_flags;
		IoInitializeIrp(irp, irp->size, irp->stack_count);
		irp->alloc_flags = alloc_flags;
		irp->io_status.status = status;
	}
	IOEXIT(return);
}

wstdcall struct irp *WIN_FUNC(IoAllocateIrp,2)
	(char stack_count, BOOLEAN charge_quota)
{
	struct irp *irp;
	int irp_size;

	IOENTER("count: %d", stack_count);
	stack_count++;
	irp_size = IoSizeOfIrp(stack_count);
	irp = kmalloc(irp_size, irql_gfp());
	if (irp)
		IoInitializeIrp(irp, irp_size, stack_count);
	IOTRACE("irp %p", irp);
	IOEXIT(return irp);
}

wstdcall BOOLEAN WIN_FUNC(IoCancelIrp,1)
	(struct irp *irp)
{
	typeof(irp->cancel_routine) cancel_routine;

	/* NB: this function may be called at DISPATCH_LEVEL */
	IOTRACE("irp: %p", irp);
	if (!irp)
		return FALSE;
	DUMP_IRP(irp);
	IoAcquireCancelSpinLock(&irp->cancel_irql);
	cancel_routine = xchg(&irp->cancel_routine, NULL);
	IOTRACE("%p", cancel_routine);
	irp->cancel = TRUE;
	if (cancel_routine) {
		struct io_stack_location *irp_sl;
		irp_sl = IoGetCurrentIrpStackLocation(irp);
		IOTRACE("%p, %p", irp_sl, irp_sl->dev_obj);
		/* cancel_routine will release the spin lock */
		__release(irp->cancel_irql);
		LIN2WIN2(cancel_routine, irp_sl->dev_obj, irp);
		/* in usb's cancel, irp->cancel is set to indicate
		 * status of cancel */
		IOEXIT(return xchg(&irp->cancel, TRUE));
	} else {
		IOTRACE("irp %p already canceled", irp);
		IoReleaseCancelSpinLock(irp->cancel_irql);
		IOEXIT(return FALSE);
	}
}

wstdcall void IoQueueThreadIrp(struct irp *irp)
{
	struct nt_thread *thread;
	KIRQL irql;

	thread = get_current_nt_thread();
	if (thread) {
		IOTRACE("thread: %p, task: %p", thread, thread->task);
		irp->flags |= IRP_SYNCHRONOUS_API;
		irql = nt_spin_lock_irql(&thread->lock, DISPATCH_LEVEL);
		InsertTailList(&thread->irps, &irp->thread_list);
		IoIrpThread(irp) = thread;
		nt_spin_unlock_irql(&thread->lock, irql);
	} else
		IoIrpThread(irp) = NULL;
}

wstdcall void IoDequeueThreadIrp(struct irp *irp)
{
	struct nt_thread *thread;
	KIRQL irql;

	thread = IoIrpThread(irp);
	if (thread) {
		irql = nt_spin_lock_irql(&thread->lock, DISPATCH_LEVEL);
		RemoveEntryList(&irp->thread_list);
		nt_spin_unlock_irql(&thread->lock, irql);
	}
}

wstdcall void WIN_FUNC(IoFreeIrp,1)
	(struct irp *irp)
{
	IOENTER("irp = %p", irp);
	if (irp->flags & IRP_SYNCHRONOUS_API)
		IoDequeueThreadIrp(irp);
	kfree(irp);

	IOEXIT(return);
}

wstdcall struct irp *WIN_FUNC(IoBuildAsynchronousFsdRequest,6)
	(ULONG major_fn, struct device_object *dev_obj, void *buffer,
	 ULONG length, LARGE_INTEGER *offset,
	 struct io_status_block *user_status)
{
	struct irp *irp;
	struct io_stack_location *irp_sl;

	IOENTER("%p", dev_obj);
	if (!dev_obj)
		IOEXIT(return NULL);
	irp = IoAllocateIrp(dev_obj->stack_count, FALSE);
	if (irp == NULL) {
		WARNING("couldn't allocate irp");
		IOEXIT(return NULL);
	}

	irp_sl = IoGetNextIrpStackLocation(irp);
	irp_sl->major_fn = major_fn;
	IOTRACE("major_fn: %d", major_fn);
	irp_sl->minor_fn = 0;
	irp_sl->flags = 0;
	irp_sl->control = 0;
	irp_sl->dev_obj = dev_obj;
	irp_sl->file_obj = NULL;
	irp_sl->completion_routine = NULL;

	if (dev_obj->flags & DO_DIRECT_IO) {
		irp->mdl = IoAllocateMdl(buffer, length, FALSE, FALSE, irp);
		if (irp->mdl == NULL) {
			IoFreeIrp(irp);
			return NULL;
		}
		MmProbeAndLockPages(irp->mdl, KernelMode,
				    major_fn == IRP_MJ_WRITE ?
				    IoReadAccess : IoWriteAccess);
		IOTRACE("mdl: %p", irp->mdl);
	} else if (dev_obj->flags & DO_BUFFERED_IO) {
		irp->associated_irp.system_buffer = buffer;
		irp->flags = IRP_BUFFERED_IO;
		irp->mdl = NULL;
		IOTRACE("buffer: %p", buffer);
	}
	if (major_fn == IRP_MJ_READ) {
		irp_sl->params.read.length = length;
		irp_sl->params.read.byte_offset = *offset;
	} else if (major_fn == IRP_MJ_WRITE) {
		irp_sl->params.write.length = length;
		irp_sl->params.write.byte_offset = *offset;
	}
	irp->user_status = user_status;
	IOTRACE("irp: %p", irp);
	return irp;
}

wstdcall struct irp *WIN_FUNC(IoBuildSynchronousFsdRequest,7)
	(ULONG major_fn, struct device_object *dev_obj, void *buf,
	 ULONG length, LARGE_INTEGER *offset, struct nt_event *event,
	 struct io_status_block *user_status)
{
	struct irp *irp;

	irp = IoBuildAsynchronousFsdRequest(major_fn, dev_obj, buf, length,
					    offset, user_status);
	if (irp == NULL)
		return NULL;
	irp->user_event = event;
	IoQueueThreadIrp(irp);
	return irp;
}

wstdcall struct irp *WIN_FUNC(IoBuildDeviceIoControlRequest,9)
	(ULONG ioctl, struct device_object *dev_obj,
	 void *input_buf, ULONG input_buf_len, void *output_buf,
	 ULONG output_buf_len, BOOLEAN internal_ioctl,
	 struct nt_event *event, struct io_status_block *io_status)
{
	struct irp *irp;
	struct io_stack_location *irp_sl;
	ULONG buf_len;

	IOENTER("%p, 0x%08x, %d", dev_obj, ioctl, internal_ioctl);
	if (!dev_obj)
		IOEXIT(return NULL);
	irp = IoAllocateIrp(dev_obj->stack_count, FALSE);
	if (irp == NULL) {
		WARNING("couldn't allocate irp");
		return NULL;
	}
	irp_sl = IoGetNextIrpStackLocation(irp);
	irp_sl->params.dev_ioctl.code = ioctl;
	irp_sl->params.dev_ioctl.input_buf_len = input_buf_len;
	irp_sl->params.dev_ioctl.output_buf_len = output_buf_len;
	irp_sl->major_fn = (internal_ioctl) ?
		IRP_MJ_INTERNAL_DEVICE_CONTROL : IRP_MJ_DEVICE_CONTROL;
	IOTRACE("%d", IO_METHOD_FROM_CTL_CODE(ioctl));

	switch (IO_METHOD_FROM_CTL_CODE(ioctl)) {
	case METHOD_BUFFERED:
		buf_len = max(input_buf_len, output_buf_len);
		if (buf_len) {
			irp->associated_irp.system_buffer =
				ExAllocatePoolWithTag(NonPagedPool, buf_len, 0);
			if (!irp->associated_irp.system_buffer) {
				IoFreeIrp(irp);
				IOEXIT(return NULL);
			}
			irp->associated_irp.system_buffer = input_buf;
			if (input_buf)
				memcpy(irp->associated_irp.system_buffer,
				       input_buf, input_buf_len);
			irp->flags = IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER;
			if (output_buf)
				irp->flags = IRP_INPUT_OPERATION;
			irp->user_buf = output_buf;
		} else
			irp->user_buf = NULL;
		break;
	case METHOD_IN_DIRECT:
	case METHOD_OUT_DIRECT:
		if (input_buf) {
			irp->associated_irp.system_buffer =
				ExAllocatePoolWithTag(NonPagedPool,
						      input_buf_len, 0);
			if (!irp->associated_irp.system_buffer) {
				IoFreeIrp(irp);
				IOEXIT(return NULL);
			}
			memcpy(irp->associated_irp.system_buffer,
			       input_buf, input_buf_len);
			irp->flags = IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER;
		}
		/* TODO: we are supposed to setup MDL, but USB layer
		 * doesn't use MDLs. Moreover, USB layer mirrors
		 * non-DMAable buffers, so no need to allocate
		 * DMAable buffer here */
		if (output_buf) {
			irp->associated_irp.system_buffer =
				ExAllocatePoolWithTag(NonPagedPool,
						      output_buf_len, 0);
			if (!irp->associated_irp.system_buffer) {
				IoFreeIrp(irp);
				IOEXIT(return NULL);
			}
			irp->flags = IRP_BUFFERED_IO | IRP_DEALLOCATE_BUFFER;
		}
		break;
	case METHOD_NEITHER:
		irp->user_buf = output_buf;
		irp_sl->params.dev_ioctl.type3_input_buf = input_buf;
		break;
	}

	irp->user_status = io_status;
	irp->user_event = event;
	IoQueueThreadIrp(irp);

	IOTRACE("irp: %p", irp);
	IOEXIT(return irp);
}

wfastcall NTSTATUS WIN_FUNC(IofCallDriver,2)
	(struct device_object *dev_obj, struct irp *irp)
{
	struct io_stack_location *irp_sl;
	NTSTATUS status;
	driver_dispatch_t *major_func;
	struct driver_object *drv_obj;

	if (irp->current_location <= 0) {
		ERROR("invalid irp: %p, %d", irp, irp->current_location);
		return STATUS_INVALID_PARAMETER;
	}
	IOTRACE("%p, %p, %p, %d, %d, %p", dev_obj, irp, dev_obj->drv_obj,
		irp->current_location, irp->stack_count,
		IoGetCurrentIrpStackLocation(irp));
	IoSetNextIrpStackLocation(irp);
	DUMP_IRP(irp);
	irp_sl = IoGetCurrentIrpStackLocation(irp);
	drv_obj = dev_obj->drv_obj;
	irp_sl->dev_obj = dev_obj;
	major_func = drv_obj->major_func[irp_sl->major_fn];
	IOTRACE("major_func: %p, dev_obj: %p", major_func, dev_obj);
	if (major_func)
		status = LIN2WIN2(major_func, dev_obj, irp);
	else {
		ERROR("major_function %d is not implemented",
		      irp_sl->major_fn);
		status = STATUS_NOT_SUPPORTED;
	}
	IOEXIT(return status);
}

wfastcall void WIN_FUNC(IofCompleteRequest,2)
	(struct irp *irp, CHAR prio_boost)
{
	struct io_stack_location *irp_sl;

#ifdef IO_DEBUG
	DUMP_IRP(irp);
	if (irp->io_status.status == STATUS_PENDING) {
		ERROR("invalid irp: %p, STATUS_PENDING", irp);
		return;
	}
	if (irp->current_location < 0 ||
	    irp->current_location >= irp->stack_count) {
		ERROR("invalid irp: %p, %d", irp, irp->current_location);
		return;
	}
#endif
	for (irp_sl = IoGetCurrentIrpStackLocation(irp);
	     irp->current_location < irp->stack_count; irp_sl++) {
		struct device_object *dev_obj;
		NTSTATUS status;

		DUMP_IRP(irp);
		if (irp_sl->control & SL_PENDING_RETURNED)
			irp->pending_returned = TRUE;

		/* current_location and dev_obj must be same as when
		 * driver called IoSetCompletionRoutine, which sets
		 * completion routine at next (lower) location, which
		 * is what we are going to call below; so we set
		 * current_location and dev_obj for the previous
		 * (higher) location */
		IoSkipCurrentIrpStackLocation(irp);
		if (irp->current_location < irp->stack_count)
			dev_obj = IoGetCurrentIrpStackLocation(irp)->dev_obj;
		else
			dev_obj = NULL;

		IOTRACE("%d, %d, %p", irp->current_location, irp->stack_count,
			dev_obj);
		if (irp_sl->completion_routine &&
		    ((irp->io_status.status == STATUS_SUCCESS &&
		      irp_sl->control & SL_INVOKE_ON_SUCCESS) ||
		     (irp->io_status.status != STATUS_SUCCESS &&
		      irp_sl->control & SL_INVOKE_ON_ERROR) ||
		     (irp->cancel == TRUE &&
		      irp_sl->control & SL_INVOKE_ON_CANCEL))) {
			IOTRACE("calling completion_routine at: %p, %p",
				irp_sl->completion_routine, irp_sl->context);
			status = LIN2WIN3(irp_sl->completion_routine,
					  dev_obj, irp, irp_sl->context);
			IOTRACE("status: %08X", status);
			if (status == STATUS_MORE_PROCESSING_REQUIRED)
				IOEXIT(return);
		} else {
			/* propagate pending status to next irp_sl */
			if (irp->pending_returned &&
			    irp->current_location < irp->stack_count)
				IoMarkIrpPending(irp);
		}
	}

	if (irp->user_status) {
		irp->user_status->status = irp->io_status.status;
		irp->user_status->info = irp->io_status.info;
	}

	if (irp->user_event) {
		IOTRACE("setting event %p", irp->user_event);
		KeSetEvent(irp->user_event, prio_boost, FALSE);
	}

	if (irp->associated_irp.system_buffer &&
	    (irp->flags & IRP_DEALLOCATE_BUFFER))
		ExFreePool(irp->associated_irp.system_buffer);
	else {
		struct mdl *mdl;
		while ((mdl = irp->mdl)) {
			irp->mdl = mdl->next;
			MmUnlockPages(mdl);
			IoFreeMdl(mdl);
		}
	}
	IOTRACE("freeing irp %p", irp);
	IoFreeIrp(irp);
	IOEXIT(return);
}

wstdcall NTSTATUS IoPassIrpDown(struct device_object *dev_obj, struct irp *irp)
{
	IoSkipCurrentIrpStackLocation(irp);
	IOEXIT(return IoCallDriver(dev_obj, irp));
}

wstdcall NTSTATUS IoIrpSyncComplete(struct device_object *dev_obj,
				    struct irp *irp, void *context)
{
	if (irp->pending_returned == TRUE)
		KeSetEvent(context, IO_NO_INCREMENT, FALSE);
	IOEXIT(return STATUS_MORE_PROCESSING_REQUIRED);
}
WIN_FUNC_DECL(IoIrpSyncComplete,3)

wstdcall NTSTATUS IoSyncForwardIrp(struct device_object *dev_obj,
				   struct irp *irp)
{
	struct nt_event event;
	NTSTATUS status;

	IoCopyCurrentIrpStackLocationToNext(irp);
	KeInitializeEvent(&event, SynchronizationEvent, FALSE);
	/* completion function is called as Windows function */
	IoSetCompletionRoutine(irp, WIN_FUNC_PTR(IoIrpSyncComplete,3), &event,
			       TRUE, TRUE, TRUE);
	status = IoCallDriver(dev_obj, irp);
	IOTRACE("%08X", status);
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE,
				      NULL);
		status = irp->io_status.status;
	}
	IOTRACE("%08X", status);
	IOEXIT(return status);
}
WIN_FUNC_DECL(IoSyncForwardIrp,2)

wstdcall NTSTATUS IoAsyncForwardIrp(struct device_object *dev_obj,
				    struct irp *irp)
{
	NTSTATUS status;

	IoCopyCurrentIrpStackLocationToNext(irp);
	status = IoCallDriver(dev_obj, irp);
	IOEXIT(return status);
}
WIN_FUNC_DECL(IoAsyncForwardIrp,2)

wstdcall NTSTATUS IoInvalidDeviceRequest(struct device_object *dev_obj,
					 struct irp *irp)
{
	struct io_stack_location *irp_sl;
	NTSTATUS status;

	irp_sl = IoGetCurrentIrpStackLocation(irp);
	WARNING("%d:%d not implemented", irp_sl->major_fn, irp_sl->minor_fn);
	irp->io_status.status = STATUS_SUCCESS;
	irp->io_status.info = 0;
	status = irp->io_status.status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	IOEXIT(return status);
}
WIN_FUNC_DECL(IoInvalidDeviceRequest,2)

static irqreturn_t io_irq_isr(int irq, void *data ISR_PT_REGS_PARAM_DECL)
{
	struct kinterrupt *interrupt = data;
	BOOLEAN ret;

#ifdef CONFIG_DEBUG_SHIRQ
	if (!interrupt->u.enabled)
		EXIT1(return IRQ_NONE);
#endif
	TRACE6("%p", interrupt);
	nt_spin_lock(interrupt->actual_lock);
	ret = LIN2WIN2(interrupt->isr, interrupt, interrupt->isr_ctx);
	nt_spin_unlock(interrupt->actual_lock);
	if (ret == TRUE)
		EXIT6(return IRQ_HANDLED);
	else
		EXIT6(return IRQ_NONE);
}

wstdcall NTSTATUS WIN_FUNC(IoConnectInterrupt,11)
	(struct kinterrupt **kinterrupt, PKSERVICE_ROUTINE isr, void *isr_ctx,
	 NT_SPIN_LOCK *lock, ULONG vector, KIRQL irql, KIRQL synch_irql,
	 enum kinterrupt_mode mode, BOOLEAN shared, KAFFINITY cpu_mask,
	 BOOLEAN save_fp)
{
	struct kinterrupt *interrupt;
	IOENTER("");
	interrupt = kzalloc(sizeof(*interrupt), GFP_KERNEL);
	if (!interrupt)
		IOEXIT(return STATUS_INSUFFICIENT_RESOURCES);
	interrupt->vector = vector;
	interrupt->cpu_mask = cpu_mask;
	nt_spin_lock_init(&interrupt->lock);
	if (lock)
		interrupt->actual_lock = lock;
	else
		interrupt->actual_lock = &interrupt->lock;
	interrupt->shared = shared;
	interrupt->save_fp = save_fp;
	interrupt->isr = isr;
	interrupt->isr_ctx = isr_ctx;
	InitializeListHead(&interrupt->list);
	interrupt->irql = irql;
	interrupt->synch_irql = synch_irql;
	interrupt->mode = mode;
	if (request_irq(vector, io_irq_isr, shared ? IRQF_SHARED : 0,
			"ndiswrapper", interrupt)) {
		WARNING("request for irq %d failed", vector);
		kfree(interrupt);
		IOEXIT(return STATUS_INSUFFICIENT_RESOURCES);
	}
	*kinterrupt = interrupt;
#ifdef CONFIG_DEBUG_SHIRQ
	interrupt->u.enabled = 1;
#endif
	IOEXIT(return STATUS_SUCCESS);
}

wstdcall void WIN_FUNC(IoDisconnectInterrupt,1)
	(struct kinterrupt *interrupt)
{
#ifdef CONFIG_DEBUG_SHIRQ
	interrupt->u.enabled = 0;
#endif
	free_irq(interrupt->vector, interrupt);
	kfree(interrupt);
}

wstdcall struct mdl *WIN_FUNC(IoAllocateMdl,5)
	(void *virt, ULONG length, BOOLEAN second_buf, BOOLEAN charge_quota,
	 struct irp *irp)
{
	struct mdl *mdl;
	mdl = allocate_init_mdl(virt, length);
	if (!mdl)
		return NULL;
	if (irp) {
		if (second_buf == TRUE) {
			struct mdl *last;

			last = irp->mdl;
			while (last->next)
				last = last->next;
			last->next = mdl;
		} else
			irp->mdl = mdl;
	}
	IOTRACE("%p", mdl);
	return mdl;
}

wstdcall void WIN_FUNC(IoFreeMdl,1)
	(struct mdl *mdl)
{
	IOTRACE("%p", mdl);
	free_mdl(mdl);
}

wstdcall struct io_workitem *WIN_FUNC(IoAllocateWorkItem,1)
	(struct device_object *dev_obj)
{
	struct io_workitem *io_workitem;

	IOENTER("%p", dev_obj);
	io_workitem = kmalloc(sizeof(*io_workitem), irql_gfp());
	if (!io_workitem)
		IOEXIT(return NULL);
	io_workitem->dev_obj = dev_obj;
	IOEXIT(return io_workitem);
}

wstdcall void WIN_FUNC(IoFreeWorkItem,1)
	(struct io_workitem *io_workitem)
{
	kfree(io_workitem);
	IOEXIT(return);
}

wstdcall void WIN_FUNC(IoQueueWorkItem,4)
	(struct io_workitem *io_workitem, void *func,
	 enum work_queue_type queue_type, void *context)
{
	IOENTER("%p, %p", io_workitem, io_workitem->dev_obj);
	io_workitem->worker_routine = func;
	io_workitem->context = context;
	schedule_ntos_work_item(func, io_workitem->dev_obj, context);
	IOEXIT(return);
}

wstdcall void WIN_FUNC(ExQueueWorkItem,2)
	(struct io_workitem *io_workitem, enum work_queue_type queue_type)
{
	IOENTER("%p", io_workitem);
	schedule_ntos_work_item(io_workitem->worker_routine,
				io_workitem->dev_obj, io_workitem->context);
}

wstdcall NTSTATUS WIN_FUNC(IoAllocateDriverObjectExtension,4)
	(struct driver_object *drv_obj, void *client_id, ULONG extlen,
	 void **ext)
{
	struct custom_ext *ce;

	IOENTER("%p, %p", drv_obj, client_id);
	ce = kmalloc(sizeof(*ce) + extlen, irql_gfp());
	if (ce == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	IOTRACE("custom_ext: %p", ce);
	ce->client_id = client_id;
	spin_lock_bh(&ntoskernel_lock);
	InsertTailList(&drv_obj->drv_ext->custom_ext, &ce->list);
	spin_unlock_bh(&ntoskernel_lock);

	*ext = (void *)ce + sizeof(*ce);
	IOTRACE("ext: %p", *ext);
	IOEXIT(return STATUS_SUCCESS);
}

wstdcall void *WIN_FUNC(IoGetDriverObjectExtension,2)
	(struct driver_object *drv_obj, void *client_id)
{
	struct custom_ext *ce;
	void *ret;

	IOENTER("drv_obj: %p, client_id: %p", drv_obj, client_id);
	ret = NULL;
	spin_lock_bh(&ntoskernel_lock);
	nt_list_for_each_entry(ce, &drv_obj->drv_ext->custom_ext, list) {
		if (ce->client_id == client_id) {
			ret = (void *)ce + sizeof(*ce);
			break;
		}
	}
	spin_unlock_bh(&ntoskernel_lock);
	IOTRACE("ret: %p", ret);
	return ret;
}

void free_custom_extensions(struct driver_extension *drv_ext)
{
	struct nt_list *ent;

	IOENTER("%p", drv_ext);
	spin_lock_bh(&ntoskernel_lock);
	while ((ent = RemoveHeadList(&drv_ext->custom_ext)))
		kfree(ent);
	spin_unlock_bh(&ntoskernel_lock);
	IOEXIT(return);
}

wstdcall NTSTATUS WIN_FUNC(IoCreateDevice,7)
	(struct driver_object *drv_obj, ULONG dev_ext_length,
	 struct unicode_string *dev_name, DEVICE_TYPE dev_type,
	 ULONG dev_chars, BOOLEAN exclusive, struct device_object **newdev)
{
	struct device_object *dev;
	struct dev_obj_ext *dev_obj_ext;
	int size;

	IOENTER("%p, %u, %p", drv_obj, dev_ext_length, dev_name);

	size = sizeof(*dev) + dev_ext_length + sizeof(*dev_obj_ext);
	dev = allocate_object(size, OBJECT_TYPE_DEVICE, dev_name);
	if (!dev)
		IOEXIT(return STATUS_INSUFFICIENT_RESOURCES);
	if (dev_ext_length)
		dev->dev_ext = dev + 1;
	else
		dev->dev_ext = NULL;

	dev_obj_ext = ((void *)(dev + 1)) + dev_ext_length;
	dev_obj_ext->dev_obj = dev;
	dev_obj_ext->size = 0;
	dev_obj_ext->type = IO_TYPE_DEVICE;
	dev->dev_obj_ext = dev_obj_ext;

	dev->type = dev_type;
	dev->flags = 0;
	dev->size = sizeof(*dev) + dev_ext_length;
	dev->ref_count = 1;
	dev->attached = NULL;
	dev->stack_count = 1;

	dev->drv_obj = drv_obj;
	dev->next = drv_obj->dev_obj;
	drv_obj->dev_obj = dev;

	dev->align_req = 1;
	dev->characteristics = dev_chars;
	dev->io_timer = NULL;
	KeInitializeEvent(&dev->lock, SynchronizationEvent, TRUE);
	dev->vpb = NULL;

	IOTRACE("dev: %p, ext: %p", dev, dev->dev_ext);
	*newdev = dev;
	IOEXIT(return STATUS_SUCCESS);
}

wstdcall NTSTATUS WIN_FUNC(IoCreateUnprotectedSymbolicLink,2)
	(struct unicode_string *link, struct unicode_string *dev_name)
{
	struct ansi_string ansi;

	IOENTER("%p, %p", dev_name, link);
	if (dev_name && (RtlUnicodeStringToAnsiString(&ansi, dev_name, TRUE) ==
			 STATUS_SUCCESS)) {
		IOTRACE("dev_name: %s", ansi.buf);
		RtlFreeAnsiString(&ansi);
	}
	if (link && (RtlUnicodeStringToAnsiString(&ansi, link, TRUE) ==
		     STATUS_SUCCESS)) {
		IOTRACE("link: %s", ansi.buf);
		RtlFreeAnsiString(&ansi);
	}
//	TODO();
	IOEXIT(return STATUS_SUCCESS);
}

wstdcall NTSTATUS WIN_FUNC(IoCreateSymbolicLink,2)
	(struct unicode_string *link, struct unicode_string *dev_name)
{
	IOEXIT(return IoCreateUnprotectedSymbolicLink(link, dev_name));
}

wstdcall NTSTATUS WIN_FUNC(IoDeleteSymbolicLink,1)
	(struct unicode_string *link)
{
	struct ansi_string ansi;

	IOENTER("%p", link);
	if (link && (RtlUnicodeStringToAnsiString(&ansi, link, TRUE) ==
		     STATUS_SUCCESS)) {
		IOTRACE("dev_name: %s", ansi.buf);
		RtlFreeAnsiString(&ansi);
	}
	IOEXIT(return STATUS_SUCCESS);
}

wstdcall void WIN_FUNC(IoDeleteDevice,1)
	(struct device_object *dev)
{
	IOENTER("%p", dev);
	if (dev == NULL)
		IOEXIT(return);
	IOTRACE("drv_obj: %p", dev->drv_obj);
	if (dev->drv_obj) {
		struct device_object *prev;

		prev = dev->drv_obj->dev_obj;
		IOTRACE("dev_obj: %p", prev);
		if (prev == dev)
			dev->drv_obj->dev_obj = dev->next;
		else if (prev) {
			while (prev->next != dev)
				prev = prev->next;
			prev->next = dev->next;
		}
	}
	ObDereferenceObject(dev);
	IOEXIT(return);
}

wstdcall void WIN_FUNC(IoDetachDevice,1)
	(struct device_object *tgt)
{
	struct device_object *tail;

	IOENTER("%p", tgt);
	if (!tgt)
		IOEXIT(return);
	tail = tgt->attached;
	if (!tail)
		IOEXIT(return);
	IOTRACE("tail: %p", tail);

	spin_lock_bh(&ntoskernel_lock);
	tgt->attached = tail->attached;
	IOTRACE("attached:%p", tgt->attached);
	for ( ; tail; tail = tail->attached) {
		IOTRACE("tail:%p", tail);
		tail->stack_count--;
	}
	spin_unlock_bh(&ntoskernel_lock);
	IOEXIT(return);
}

wstdcall struct device_object *WIN_FUNC(IoGetAttachedDevice,1)
	(struct device_object *dev)
{
	IOENTER("%p", dev);
	if (!dev)
		IOEXIT(return NULL);
	spin_lock_bh(&ntoskernel_lock);
	while (dev->attached)
		dev = dev->attached;
	spin_unlock_bh(&ntoskernel_lock);
	IOEXIT(return dev);
}

wstdcall struct device_object *WIN_FUNC(IoGetAttachedDeviceReference,1)
	(struct device_object *dev)
{
	IOENTER("%p", dev);
	if (!dev)
		IOEXIT(return NULL);
	dev = IoGetAttachedDevice(dev);
	ObReferenceObject(dev);
	IOEXIT(return dev);
}

wstdcall struct device_object *WIN_FUNC(IoAttachDeviceToDeviceStack,2)
	(struct device_object *src, struct device_object *tgt)
{
	struct device_object *attached;
	struct dev_obj_ext *src_dev_ext;

	IOENTER("%p, %p", src, tgt);
	attached = IoGetAttachedDevice(tgt);
	IOTRACE("%p", attached);
	src_dev_ext = src->dev_obj_ext;
	spin_lock_bh(&ntoskernel_lock);
	if (attached)
		attached->attached = src;
	src->attached = NULL;
	src->stack_count = attached->stack_count + 1;
	src_dev_ext->attached_to = attached;
	spin_unlock_bh(&ntoskernel_lock);
	IOTRACE("stack_count: %d -> %d", attached->stack_count,
		src->stack_count);
	IOEXIT(return attached);
}

wstdcall NTSTATUS WIN_FUNC(IoGetDeviceProperty,5)
	(struct device_object *pdo, enum device_registry_property dev_property,
	 ULONG buffer_len, void *buffer, ULONG *result_len)
{
	struct ansi_string ansi;
	struct unicode_string unicode;
	struct wrap_device *wd;
	ULONG need;

	IOENTER("dev_obj = %p, dev_property = %d, buffer_len = %u, "
		"buffer = %p, result_len = %p", pdo, dev_property,
		buffer_len, buffer, result_len);

	wd = pdo->reserved;
	switch (dev_property) {
	case DevicePropertyDeviceDescription:
	case DevicePropertyFriendlyName:
	case DevicePropertyDriverKeyName:
		if (wrap_is_pci_bus(wd->dev_bus))
			RtlInitAnsiString(&ansi, "PCI");
		else // if (wrap_is_usb_bus(wd->dev_bus))
			RtlInitAnsiString(&ansi, "USB");
		need = sizeof(wchar_t) * (ansi.max_length + 1);
		if (buffer_len < need) {
			*result_len = need;
			IOEXIT(return STATUS_BUFFER_TOO_SMALL);
		}
		unicode.max_length = buffer_len;
		unicode.buf = buffer;
		if (RtlAnsiStringToUnicodeString(&unicode, &ansi,
						 FALSE) != STATUS_SUCCESS) {
			*result_len = unicode.length;
			IOEXIT(return STATUS_BUFFER_TOO_SMALL);
		}
		IOEXIT(return STATUS_SUCCESS);
	default:
		WARNING("%d not implemented", dev_property);
		IOEXIT(return STATUS_INVALID_PARAMETER_2);
	}
}

wstdcall NTSTATUS WIN_FUNC(IoGetDeviceObjectPointer,4)
	(struct unicode_string *name, ACCESS_MASK desired_access,
	 void *file_obj, struct device_object *dev_obj)
{
	struct common_object_header *coh;

	dev_obj = NULL;
	/* TODO: access is not checked and file_obj is set to NULL */
	file_obj = NULL;
	spin_lock_bh(&ntoskernel_lock);
	nt_list_for_each_entry(coh, &object_list, list) {
		TRACE5("header: %p, type: %d", coh, coh->type);
		if (coh->type != OBJECT_TYPE_DEVICE)
			continue;
		if (!RtlCompareUnicodeString(&coh->name, name, TRUE)) {
			dev_obj = HEADER_TO_OBJECT(coh);
			TRACE5("dev_obj: %p", dev_obj);
			break;
		}
	}
	spin_unlock_bh(&ntoskernel_lock);
	if (dev_obj)
		IOEXIT(return STATUS_SUCCESS);
	else
		IOEXIT(return STATUS_OBJECT_NAME_INVALID);
}

/* NOTE: Make sure to compile with -freg-struct-return, so gcc will
 * return union in register, like Windows */
wstdcall union power_state WIN_FUNC(PoSetPowerState,3)
	(struct device_object *dev_obj, enum power_state_type type,
	 union power_state state)
{
	IOEXIT(return state);
}

wstdcall NTSTATUS WIN_FUNC(PoCallDriver,2)
	(struct device_object *dev_obj, struct irp *irp)
{
	return IoCallDriver(dev_obj, irp);
}

wstdcall NTSTATUS WIN_FUNC(PoRequestPowerIrp,6)
	(struct device_object *dev_obj, UCHAR minor_fn,
	 union power_state power_state, void *completion_func,
	 void *context, struct irp **pirp)
{
	struct irp *irp;
	struct io_stack_location *irp_sl;

	TRACE1("%p, %d, %p", dev_obj, dev_obj->stack_count, dev_obj->drv_obj);
	irp = IoAllocateIrp(dev_obj->stack_count, FALSE);
	if (!irp)
		return STATUS_INSUFFICIENT_RESOURCES;
	irp_sl = IoGetNextIrpStackLocation(irp);
	irp_sl->major_fn = IRP_MJ_POWER;
	irp_sl->minor_fn = minor_fn;
	if (minor_fn == IRP_MN_WAIT_WAKE)
		irp_sl->params.power.type = SystemPowerState;
	else
		irp_sl->params.power.type = DevicePowerState;
	irp_sl->params.power.state = power_state;
	irp_sl->completion_routine = completion_func;
	irp->io_status.status = STATUS_NOT_SUPPORTED;
	*pirp = irp;
	return PoCallDriver(dev_obj, irp);
}

wstdcall void WIN_FUNC(PoStartNextPowerIrp,1)
	(struct irp *irp)
{
	IOENTER("irp = %p", irp);
	IOEXIT(return);
}

wstdcall void WIN_FUNC(IoInitializeRemoveLockEx,5)
	(struct io_remove_lock *lock, ULONG alloc_tag, ULONG max_locked_min,
	 ULONG high_mark, ULONG lock_size)
{
	TODO();
}

wstdcall void *WIN_FUNC(IoAllocateErrorLogEntry,2)
	(void *io_object, UCHAR entry_size)
{
	/* not implemented fully */
	void *ret = kmalloc(sizeof(struct io_error_log_packet) + entry_size,
			    irql_gfp());
	TRACE2("%p", ret);
	if (ret)
		return ret + sizeof(struct io_error_log_packet);
	else
		return NULL;
}

wstdcall void WIN_FUNC(IoWriteErrorLogEntry,1)
	(void *entry)
{
	/* TODO: log error with codes and message */
	ERROR("");
}

wstdcall void WIN_FUNC(IoFreeErrorLogEntry,1)
	(void *entry)
{
	TRACE2("%p", entry);
	kfree(entry - sizeof(struct io_error_log_packet));
}

wstdcall NTSTATUS WIN_FUNC(IoAcquireRemoveLockEx,5)
	(struct io_remove_lock lock, void *tag, char *file, ULONG line,
	 ULONG lock_size)
{
	TODO();
	IOEXIT(return STATUS_SUCCESS);
}

wstdcall NTSTATUS WIN_FUNC(IoReleaseRemoveLockEx,3)
	(struct io_remove_lock lock, void *tag, ULONG lock_size)
{
	TODO();
	IOEXIT(return STATUS_SUCCESS);
}

wstdcall NTSTATUS WIN_FUNC(IoRegisterDeviceInterface,4)
	(struct device_object *pdo, struct guid *guid_class,
	 struct unicode_string *reference, struct unicode_string *link)
{
	struct ansi_string ansi;

	/* TODO: check if pdo is valid */
	RtlInitAnsiString(&ansi, "ndis");
	ENTER1("pdo: %p, ref: %p, link: %p, %x, %x, %x", pdo, reference, link,
	       guid_class->data1, guid_class->data2, guid_class->data3);
	return RtlAnsiStringToUnicodeString(link, &ansi, TRUE);
}

wstdcall NTSTATUS WIN_FUNC(IoSetDeviceInterfaceState,2)
	(struct unicode_string *link, BOOLEAN enable)
{
	ENTER1("link: %p, enable: %d", link, enable);
	return STATUS_SUCCESS;
}

wstdcall NTSTATUS WIN_FUNC(IoOpenDeviceRegistryKey,4)
	(struct device_object *dev_obj, ULONG type, ACCESS_MASK mask,
	 void **handle)
{
	ENTER1("dev_obj: %p", dev_obj);
	*handle = dev_obj;
	return STATUS_SUCCESS;
}

wstdcall NTSTATUS WIN_FUNC(IoWMIRegistrationControl,2)
	(struct device_object *dev_obj, ULONG action)
{
	ENTER2("%p, %d", dev_obj, action);
	EXIT2(return STATUS_SUCCESS);
}

wstdcall void WIN_FUNC(IoInvalidateDeviceRelations,2)
	(struct device_object *dev_obj, enum device_relation_type type)
{
	INFO("%p, %d", dev_obj, type);
	TODO();
}

wstdcall void WIN_FUNC(IoInvalidateDeviceState,1)
	(struct device_object *pdo)
{
	INFO("%p", pdo);
	TODO();
}
