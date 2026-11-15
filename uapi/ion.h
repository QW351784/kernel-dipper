/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * ION uapi compatibility header
 *
 * Provides the legacy /dev/ion ioctl ABI so that existing userspace
 * (gralloc, display HAL, cameras) keeps working against a
 * DMA-BUF backed implementation.
 */

#ifndef _UAPI_LINUX_ION_H
#define _UAPI_LINUX_ION_H

#include <linux/ioctl.h>
#include <linux/types.h>

typedef int ion_user_handle_t;

/* Legacy heap types (Qualcomm numbering) */
enum ion_heap_type {
	ION_HEAP_TYPE_SYSTEM = 0,
	ION_HEAP_TYPE_SYSTEM_CONTIG = 1,
	ION_HEAP_TYPE_CARVEOUT = 2,
	ION_HEAP_TYPE_CHUNK = 3,
	ION_HEAP_TYPE_DMA = 4,
	ION_HEAP_TYPE_IOMMU = 5,
	ION_HEAP_TYPE_SECURE = 6,
	ION_HEAP_TYPE_QSECCOM = 7,
	ION_NUM_HEAP_IDS = 32,
};

/* Allocation flags */
#define ION_FLAG_CACHED			(1u << 0)
#define ION_FLAG_CACHED_NEEDS_SYNC	(1u << 1)
#define ION_FLAG_PROPAGATE_CACHE	(1u << 2)
#define ION_FLAG_NOZEROED		(1u << 3)
#define ION_FLAG_SECURE			(1u << 4)
#define ION_FLAG_UNCACHED		(1u << 5)

/**
 * struct ion_allocation_data - metadata passed from userspace for allocations
 * @len:		size of the allocation
 * @align:		required alignment of the allocation (bytes)
 * @heap_id_mask:	mask of heap ids to allocate from
 * @flags:		flags passed to heap
 * @handle:		64 bit handle, populated on success
 */
struct ion_allocation_data {
	__u64 len;
	__u32 align;
	__u32 heap_id_mask;
	__u32 flags;
	__u32 handle;
};

struct ion_free_data {
	ion_user_handle_t handle;
};

struct ion_handle_data {
	ion_user_handle_t handle;
};

/**
 * struct ion_fd_data - metadata passed for map/share/import
 * @handle:	handle passed in
 * @fd:		fd populated out
 */
struct ion_fd_data {
	ion_user_handle_t handle;
	__s32 fd;
};

/**
 * struct ion_handle_info - minimal handle info
 */
struct ion_handle_info {
	ion_user_handle_t handle;
	__u32 type;
	__u32 heap_type;
	__u32 flags;
	__u64 size;
};

struct ion_heap_data {
	char name[32];
	__u32 type;
	__u32 heap_id;
	__u32 reserved0;
	__u32 reserved1;
	__u32 reserved2;
};

struct ion_heap_query {
	__u32 cnt;
	__u32 reserved0;
	__u64 heaps;
	__u32 reserved1;
	__u32 reserved2;
};

/* Cache operation types */
#define ION_CLEAN_CACHE			(0)
#define ION_INVALIDATE_CACHE		(1)

/**
 * struct ion_custom_data - metadata passed for custom ioctl
 * @cmd:	custom command to perform
 * @arg:	arguments for the command
 */
struct ion_custom_data {
	__u32 cmd;
	__u64 arg;
};

/* Qualcomm custom commands (kept for compatibility, mostly no-op) */
#define ION_MSM_CUSTOM_SECURE_BUFFER	(0)
#define ION_MSM_CUSTOM_CLEAN_CACHE	(2)
#define ION_MSM_CUSTOM_INVALIDATE_CACHE	(3)
#define ION_MSM_CUSTOM_SECURE_BUFFER_GET	(4)

/* ioctl numbers - preserve the legacy ABI exactly */
#define ION_IOC_MAGIC			'I'

#define ION_IOC_ALLOC			_IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_FREE			_IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data)
#define ION_IOC_MAP			_IOWR(ION_IOC_MAGIC, 2, struct ion_fd_data)
#define ION_IOC_SHARE			_IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data)
#define ION_IOC_IMPORT			_IOWR(ION_IOC_MAGIC, 5, struct ion_fd_data)
#define ION_IOC_SYNC			_IOWR(ION_IOC_MAGIC, 7, struct ion_fd_data)
#define ION_IOC_CUSTOM			_IOWR(ION_IOC_MAGIC, 6, struct ion_custom_data)

#define ION_IOC_HEAP_QUERY		_IOWR(ION_IOC_MAGIC, 8, struct ion_heap_query)

#endif /* _UAPI_LINUX_ION_H */
