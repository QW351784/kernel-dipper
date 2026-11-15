// SPDX-License-Identifier: GPL-2.0
/*
 * ION compatibility shim on top of DMA-BUF
 *
 * Legacy Android userspace (gralloc, display HAL, QSEECom) expects
 * /dev/ion and the old ION ioctl ABI. Modern kernels dropped ION in
 * favour of DMA-BUF heaps. This driver re-creates the userspace facing
 * ION device while allocating buffers with the standard DMA-BUF
 * exporter path, so the legacy ABI keeps working on a modern kernel.
 */

#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/ioctl.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <uapi/linux/ion.h>

struct ion_buffer {
	struct list_head attachments;
	struct mutex lock;
	unsigned long len;
	unsigned int flags;
	struct sg_table sg_table;
	int vmap_cnt;
	void *vaddr;
};

struct ion_attachment {
	struct device *dev;
	struct sg_table table;
	struct list_head list;
	bool mapped;
};

#define LOW_ORDER_GFP (GFP_HIGHUSER | __GFP_ZERO)
#define HIGH_ORDER_GFP (((GFP_HIGHUSER | __GFP_ZERO | __GFP_NOWARN \
				| __GFP_NORETRY) & ~__GFP_RECLAIM) \
				| __GFP_COMP)
static gfp_t order_flags[] = {HIGH_ORDER_GFP, HIGH_ORDER_GFP, LOW_ORDER_GFP};
static const unsigned int orders[] = {8, 4, 0};
#define NUM_ORDERS ARRAY_SIZE(orders)

static int dup_sg_table(struct sg_table *from, struct sg_table *to)
{
	struct scatterlist *sg, *new_sg;
	int ret, i;

	ret = sg_alloc_table(to, from->orig_nents, GFP_KERNEL);
	if (ret)
		return ret;

	new_sg = to->sgl;
	for_each_sgtable_sg(from, sg, i) {
		sg_set_page(new_sg, sg_page(sg), sg->length, sg->offset);
		new_sg = sg_next(new_sg);
	}

	return 0;
}

static int ion_attach(struct dma_buf *dmabuf,
		      struct dma_buf_attachment *attachment)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_attachment *a;
	int ret;

	a = kzalloc(sizeof(*a), GFP_KERNEL);
	if (!a)
		return -ENOMEM;

	ret = dup_sg_table(&buffer->sg_table, &a->table);
	if (ret) {
		kfree(a);
		return ret;
	}

	a->dev = attachment->dev;
	INIT_LIST_HEAD(&a->list);
	a->mapped = false;

	attachment->priv = a;

	mutex_lock(&buffer->lock);
	list_add(&a->list, &buffer->attachments);
	mutex_unlock(&buffer->lock);

	return 0;
}

static void ion_detach(struct dma_buf *dmabuf,
		       struct dma_buf_attachment *attachment)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_attachment *a = attachment->priv;

	mutex_lock(&buffer->lock);
	list_del(&a->list);
	mutex_unlock(&buffer->lock);

	sg_free_table(&a->table);
	kfree(a);
}

static struct sg_table *ion_map_dma_buf(struct dma_buf_attachment *attachment,
					enum dma_data_direction direction)
{
	struct ion_attachment *a = attachment->priv;
	struct sg_table *table = &a->table;
	int ret;

	ret = dma_map_sgtable(attachment->dev, table, direction, 0);
	if (ret)
		return ERR_PTR(ret);

	a->mapped = true;
	return table;
}

static void ion_unmap_dma_buf(struct dma_buf_attachment *attachment,
			      struct sg_table *table,
			      enum dma_data_direction direction)
{
	struct ion_attachment *a = attachment->priv;

	a->mapped = false;
	dma_unmap_sgtable(attachment->dev, table, direction, 0);
}

static int ion_begin_cpu_access(struct dma_buf *dmabuf,
				enum dma_data_direction direction)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_attachment *a;

	mutex_lock(&buffer->lock);

	if (buffer->vmap_cnt)
		invalidate_kernel_vmap_range(buffer->vaddr, buffer->len);

	list_for_each_entry(a, &buffer->attachments, list) {
		if (!a->mapped)
			continue;
		dma_sync_sgtable_for_cpu(a->dev, &a->table, direction);
	}
	mutex_unlock(&buffer->lock);

	return 0;
}

static int ion_end_cpu_access(struct dma_buf *dmabuf,
			      enum dma_data_direction direction)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct ion_attachment *a;

	mutex_lock(&buffer->lock);

	if (buffer->vmap_cnt)
		flush_kernel_vmap_range(buffer->vaddr, buffer->len);

	list_for_each_entry(a, &buffer->attachments, list) {
		if (!a->mapped)
			continue;
		dma_sync_sgtable_for_device(a->dev, &a->table, direction);
	}
	mutex_unlock(&buffer->lock);

	return 0;
}

static int ion_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct sg_table *table = &buffer->sg_table;
	unsigned long addr = vma->vm_start;
	struct sg_page_iter piter;
	int ret;

	for_each_sgtable_page(table, &piter, vma->vm_pgoff) {
		struct page *page = sg_page_iter_page(&piter);

		ret = remap_pfn_range(vma, addr, page_to_pfn(page), PAGE_SIZE,
				      vma->vm_page_prot);
		if (ret)
			return ret;
		addr += PAGE_SIZE;
		if (addr >= vma->vm_end)
			return 0;
	}
	return 0;
}

static void *ion_do_vmap(struct ion_buffer *buffer)
{
	struct sg_table *table = &buffer->sg_table;
	int npages = PAGE_ALIGN(buffer->len) / PAGE_SIZE;
	struct page **pages = vmalloc(sizeof(struct page *) * npages);
	struct page **tmp = pages;
	struct sg_page_iter piter;
	void *vaddr;

	if (!pages)
		return ERR_PTR(-ENOMEM);

	for_each_sgtable_page(table, &piter, 0) {
		WARN_ON(tmp - pages >= npages);
		*tmp++ = sg_page_iter_page(&piter);
	}

	vaddr = vmap(pages, npages, VM_MAP, PAGE_KERNEL);
	vfree(pages);

	if (!vaddr)
		return ERR_PTR(-ENOMEM);

	return vaddr;
}

static int ion_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct ion_buffer *buffer = dmabuf->priv;
	void *vaddr;
	int ret = 0;

	mutex_lock(&buffer->lock);
	if (buffer->vmap_cnt) {
		buffer->vmap_cnt++;
		iosys_map_set_vaddr(map, buffer->vaddr);
		goto out;
	}

	vaddr = ion_do_vmap(buffer);
	if (IS_ERR(vaddr)) {
		ret = PTR_ERR(vaddr);
		goto out;
	}

	buffer->vaddr = vaddr;
	buffer->vmap_cnt++;
	iosys_map_set_vaddr(map, buffer->vaddr);
out:
	mutex_unlock(&buffer->lock);

	return ret;
}

static void ion_vunmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct ion_buffer *buffer = dmabuf->priv;

	mutex_lock(&buffer->lock);
	if (!--buffer->vmap_cnt) {
		vunmap(buffer->vaddr);
		buffer->vaddr = NULL;
	}
	mutex_unlock(&buffer->lock);
	iosys_map_clear(map);
}

static void ion_release(struct dma_buf *dmabuf)
{
	struct ion_buffer *buffer = dmabuf->priv;
	struct sg_table *table;
	struct scatterlist *sg;
	int i;

	table = &buffer->sg_table;
	for_each_sgtable_sg(table, sg, i) {
		struct page *page = sg_page(sg);

		__free_pages(page, compound_order(page));
	}
	sg_free_table(table);
	kfree(buffer);
}

static const struct dma_buf_ops ion_buf_ops = {
	.attach = ion_attach,
	.detach = ion_detach,
	.map_dma_buf = ion_map_dma_buf,
	.unmap_dma_buf = ion_unmap_dma_buf,
	.begin_cpu_access = ion_begin_cpu_access,
	.end_cpu_access = ion_end_cpu_access,
	.mmap = ion_mmap,
	.vmap = ion_vmap,
	.vunmap = ion_vunmap,
	.release = ion_release,
};

static struct page *alloc_largest_available(unsigned long size,
					    unsigned int max_order)
{
	struct page *page;
	int i;

	for (i = 0; i < NUM_ORDERS; i++) {
		if (size < (PAGE_SIZE << orders[i]))
			continue;
		if (max_order < orders[i])
			continue;

		page = alloc_pages(order_flags[i], orders[i]);
		if (!page)
			continue;
		return page;
	}
	return NULL;
}

static struct dma_buf *ion_alloc_buffer(unsigned long len, unsigned int flags)
{
	struct ion_buffer *buffer;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);
	unsigned long size_remaining = len;
	unsigned int max_order = orders[0];
	struct dma_buf *dmabuf;
	struct sg_table *table;
	struct scatterlist *sg;
	struct list_head pages;
	struct page *page, *tmp_page;
	int i, ret = -ENOMEM;

	buffer = kzalloc(sizeof(*buffer), GFP_KERNEL);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&buffer->attachments);
	mutex_init(&buffer->lock);
	buffer->len = len;
	buffer->flags = flags;

	INIT_LIST_HEAD(&pages);
	i = 0;
	while (size_remaining > 0) {
		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto free_buffer;
		}

		page = alloc_largest_available(size_remaining, max_order);
		if (!page)
			goto free_buffer;

		list_add_tail(&page->lru, &pages);
		size_remaining -= page_size(page);
		max_order = compound_order(page);
		i++;
	}

	table = &buffer->sg_table;
	if (sg_alloc_table(table, i, GFP_KERNEL))
		goto free_buffer;

	sg = table->sgl;
	list_for_each_entry_safe(page, tmp_page, &pages, lru) {
		sg_set_page(sg, page, page_size(page), 0);
		sg = sg_next(sg);
		list_del(&page->lru);
	}

	exp_info.exp_name = "ion";
	exp_info.ops = &ion_buf_ops;
	exp_info.size = len;
	exp_info.flags = O_RDWR | O_CLOEXEC;
	exp_info.priv = buffer;
	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto free_pages;
	}
	return dmabuf;

free_pages:
	for_each_sgtable_sg(table, sg, i) {
		struct page *p = sg_page(sg);

		__free_pages(p, compound_order(p));
	}
	sg_free_table(table);
free_buffer:
	list_for_each_entry_safe(page, tmp_page, &pages, lru)
		__free_pages(page, compound_order(page));
	kfree(buffer);

	return ERR_PTR(ret);
}

/*
 * Legacy ION treats the "handle" returned by ALLOC as an opaque id and
 * users then convert it to an fd with MAP/SHARE. Many vendor HALs skip
 * that step and use the handle directly as an fd, so we hand out the
 * dma-buf fd as the handle. That makes both styles work.
 */
static long ion_ioctl_alloc(unsigned long arg)
{
	struct ion_allocation_data data;
	struct dma_buf *dmabuf;
	unsigned long len;
	int fd;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
		return -EFAULT;

	len = PAGE_ALIGN(data.len);
	if (!len)
		return -EINVAL;

	dmabuf = ion_alloc_buffer(len, data.flags);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	fd = dma_buf_fd(dmabuf, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		dma_buf_put(dmabuf);
		return fd;
	}

	data.handle = fd;
	if (copy_to_user((void __user *)arg, &data, sizeof(data)))
		return -EFAULT;

	return 0;
}

static long ion_ioctl_fd_passthrough(unsigned long arg)
{
	struct ion_fd_data data;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
		return -EFAULT;

	/* handle is already our fd; hand it straight back */
	data.fd = (__s32)data.handle;
	if (copy_to_user((void __user *)arg, &data, sizeof(data)))
		return -EFAULT;

	return 0;
}

static long ion_ioctl_free(unsigned long arg)
{
	struct ion_handle_data data;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
		return -EFAULT;

	/* fd lifetime is managed by userspace; nothing to do here */
	return 0;
}

static long ion_ioctl_sync(unsigned long arg)
{
	struct ion_fd_data data;
	struct dma_buf *dmabuf;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
		return -EFAULT;

	dmabuf = dma_buf_get((int)data.handle);
	if (IS_ERR(dmabuf))
		return PTR_ERR(dmabuf);

	dma_buf_begin_cpu_access(dmabuf, DMA_BIDIRECTIONAL);
	dma_buf_end_cpu_access(dmabuf, DMA_BIDIRECTIONAL);
	dma_buf_put(dmabuf);

	return 0;
}

static long ion_ioctl_custom(unsigned long arg)
{
	struct ion_custom_data data;

	if (copy_from_user(&data, (void __user *)arg, sizeof(data)))
		return -EFAULT;

	/*
	 * Cache maintenance custom commands are already handled by
	 * begin/end_cpu_access through the dma-buf ops. Secure buffer
	 * negotiation against the TEE is not implemented in this shim,
	 * so report success for the no-op cases and -EINVAL for the rest.
	 */
	switch (data.cmd) {
	case ION_MSM_CUSTOM_CLEAN_CACHE:
	case ION_MSM_CUSTOM_INVALIDATE_CACHE:
		return 0;
	default:
		return -EINVAL;
	}
}

static long ion_ioctl_heap_query(unsigned long arg)
{
	struct ion_heap_query query;
	struct ion_heap_data heap;
	unsigned int count = 0;
	int ret;

	if (copy_from_user(&query, (void __user *)arg, sizeof(query)))
		return -EFAULT;

	/* advertise a single generic system heap */
	if (query.cnt && query.heaps) {
		memset(&heap, 0, sizeof(heap));
		strscpy(heap.name, "ion_system", sizeof(heap.name));
		heap.type = ION_HEAP_TYPE_SYSTEM;
		heap.heap_id = 0;

		if (copy_to_user((void __user *)(unsigned long)query.heaps,
				 &heap, sizeof(heap)))
			return -EFAULT;
		count = 1;
	}

	query.cnt = count;
	ret = copy_to_user((void __user *)arg, &query, sizeof(query));
	if (ret)
		return -EFAULT;

	return 0;
}

static long ion_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case ION_IOC_ALLOC:
		return ion_ioctl_alloc(arg);
	case ION_IOC_MAP:
	case ION_IOC_SHARE:
	case ION_IOC_IMPORT:
		return ion_ioctl_fd_passthrough(arg);
	case ION_IOC_FREE:
		return ion_ioctl_free(arg);
	case ION_IOC_SYNC:
		return ion_ioctl_sync(arg);
	case ION_IOC_CUSTOM:
		return ion_ioctl_custom(arg);
	case ION_IOC_HEAP_QUERY:
		return ion_ioctl_heap_query(arg);
	default:
		return -ENOTTY;
	}
}

static const struct file_operations ion_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = ion_ioctl,
	.compat_ioctl = ion_ioctl,
};

static struct miscdevice ion_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ion",
	.fops = &ion_fops,
	.mode = 0666,
};

static int __init ion_compat_init(void)
{
	int ret;

	ret = misc_register(&ion_miscdev);
	if (ret)
		pr_err("ion_compat: failed to register /dev/ion (%d)\n", ret);
	else
		pr_info("ion_compat: /dev/ion registered (dma-buf backed)\n");

	return ret;
}

static void __exit ion_compat_exit(void)
{
	misc_deregister(&ion_miscdev);
}

module_init(ion_compat_init);
module_exit(ion_compat_exit);

MODULE_DESCRIPTION("ION compatibility shim over DMA-BUF");
MODULE_LICENSE("GPL v2");
