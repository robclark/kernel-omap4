/* quick hack for testing get_user()/put_user() */

#include "omap_drv.h"

int ioctl_getputuser(struct drm_device *dev, void *data,
		struct drm_file *file_priv)
{
	struct drm_omap_getputuser *arg = data;
	uint8_t  __user *p1 = (uint8_t  __user *)(unsigned long)(arg->p1);
	uint16_t __user *p2 = (uint16_t __user *)(unsigned long)(arg->p2);
	uint32_t __user *p4 = (uint32_t __user *)(unsigned long)(arg->p4);
	uint64_t __user *p8 = (uint64_t __user *)(unsigned long)(arg->p8);
	uint8_t v1;
	uint16_t v2;
	uint32_t v4;
	uint64_t v8;

	get_user(v1, p1);
	get_user(v2, p2);
	get_user(v4, p4);
	get_user(v8, p8);

	printk(KERN_ERR "v1=%u, v2=%u, v4=%u, v8=%llu\n", v1, v2, v4, v8);

	v1++;
	v2++;
	v4++;
	v8++;

	put_user(v1, p1);
	put_user(v2, p2);
	put_user(v4, p4);
	put_user(v8, p8);

	return 0;
}

int test_ptr(unsigned int **v, unsigned int **p)
{
	return get_user(*v, p);
}

/*
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>

#define DRM_COMMAND_BASE                0x40
#define DRM_IOCTL_BASE			'd'
#define DRM_IOWR(nr,type)		_IOWR(DRM_IOCTL_BASE,nr,type)
#define DRM_OMAP_GETPUTUSER		0x07
#define DRM_IOCTL_OMAP_GETPUTUSER	DRM_IOWR(DRM_COMMAND_BASE + DRM_OMAP_GETPUTUSER, struct drm_omap_getputuser)

#define VOID2U64(x) ((uint64_t)(unsigned long)(x))

struct drm_omap_getputuser {
	uint64_t p1, p2, p4, p8;
};

int main(int argc, char *argv)
{
	uint8_t v1 = -1;
	uint16_t v2 = -1;
	uint32_t v4 = -1;
	uint64_t v8 = -1;
	struct drm_omap_getputuser req = {
		.p1 = VOID2U64(&v1),
		.p2 = VOID2U64(&v2),
		.p4 = VOID2U64(&v4),
		.p8 = VOID2U64(&v8),
	};
	int fd = open("/dev/dri/card0", O_RDWR);
	printf("v1=%u, v2=%u, v4=%u, v8=%llu\n", v1, v2, v4, v8);
	ioctl(fd, DRM_IOCTL_OMAP_GETPUTUSER, &req);
	printf("v1=%u, v2=%u, v4=%u, v8=%llu\n", v1, v2, v4, v8);
	return 0;
}
 */
