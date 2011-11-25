
#include "drmP.h"

int drm_prime_set_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_prime_handle *args = data;

	if (!drm_core_check_feature(dev, DRIVER_PRIME))
		return -EINVAL;

	dev->driver->prime_set(dev, file_priv, args->handle, &args->fd);
	return 0;
}

int drm_prime_get_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_prime_handle *args = data;

	if (!drm_core_check_feature(dev, DRIVER_PRIME))
		return -EINVAL;

	dev->driver->prime_get(dev, file_priv, args->fd, &args->handle);
	return 0;
}
