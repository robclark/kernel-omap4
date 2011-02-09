/*
 *  WMI hotkeys support for Dell All-In-One series
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/input.h>
#include <acpi/acpi_drivers.h>
#include <linux/acpi.h>
#include <linux/string.h>

#define AIO_PREFIX	"dell-wmi-aio: "

MODULE_DESCRIPTION("WMI hotkeys driver for Dell All-In-One series");
MODULE_LICENSE("GPL");

#define EVENT_GUID1 "284A0E6B-380E-472A-921F-E52786257FB4"
#define EVENT_GUID2 "02314822-307C-4F66-bf0E-48AEAEB26CC8"

static char *dell_wmi_aio_guids[] = {
	EVENT_GUID1,
	EVENT_GUID2,
	NULL
};

/* Temporary workaround until the WMI sysfs interface goes in.
   Borrowed from acer-wmi */
MODULE_ALIAS("dmi:*:*Dell*:*:");

MODULE_ALIAS("wmi:"EVENT_GUID1);
MODULE_ALIAS("wmi:"EVENT_GUID2);

struct key_entry {
	char type;		/* See KE_* below */
	u16 code;
	u16 keycode;
};

enum { KE_KEY, KE_SW, KE_IGNORE, KE_END };

/*
 * Certain keys are flagged as KE_IGNORE. All of these are either
 * notifications (rather than requests for change) or are also sent
 * via the keyboard controller so should not be sent again.
 */

static struct key_entry dell_wmi_aio_keymap[] = {
	{ KE_KEY, 0xc0, KEY_VOLUMEUP },
	{ KE_KEY, 0xc1, KEY_VOLUMEDOWN },
	{ KE_END, 0 }
};

static struct input_dev *dell_wmi_aio_input_dev;

static struct key_entry *dell_wmi_aio_get_entry_by_scancode(int code)
{
	struct key_entry *key;

	for (key = dell_wmi_aio_keymap; key->type != KE_END; key++)
		if (code == key->code)
			return key;

	return NULL;
}

static struct key_entry *dell_wmi_aio_get_entry_by_keycode(int keycode)
{
	struct key_entry *key;

	for (key = dell_wmi_aio_keymap; key->type != KE_END; key++)
		if (key->type == KE_KEY && keycode == key->keycode)
			return key;

	return NULL;
}

static int dell_wmi_aio_getkeycode(struct input_dev *dev, int scancode,
			       int *keycode)
{
	struct key_entry *key = dell_wmi_aio_get_entry_by_scancode(scancode);

	if (key && key->type == KE_KEY) {
		*keycode = key->keycode;
		return 0;
	}

	return -EINVAL;
}

static int dell_wmi_aio_setkeycode(struct input_dev *dev, int scancode,
				int keycode)
{
	struct key_entry *key;
	int old_keycode;

	if (keycode < 0 || keycode > KEY_MAX)
		return -EINVAL;

	key = dell_wmi_aio_get_entry_by_scancode(scancode);
	if (key && key->type == KE_KEY) {
		old_keycode = key->keycode;
		key->keycode = keycode;
		set_bit(keycode, dev->keybit);
		if (!dell_wmi_aio_get_entry_by_keycode(old_keycode))
			clear_bit(old_keycode, dev->keybit);
		return 0;
	}
	return -EINVAL;
}

static void dell_wmi_aio_handle_key(unsigned int scancode)
{
	static struct key_entry *key;

	key = dell_wmi_aio_get_entry_by_scancode(scancode);
	if (key) {
		input_report_key(dell_wmi_aio_input_dev, key->keycode, 1);
		input_sync(dell_wmi_aio_input_dev);
		input_report_key(dell_wmi_aio_input_dev, key->keycode, 0);
		input_sync(dell_wmi_aio_input_dev);
	} else if (scancode)
		pr_info(AIO_PREFIX "Unknown key %x pressed\n",
			scancode);
}

static void dell_wmi_aio_notify(u32 value, void *context)
{
	struct acpi_buffer response = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	acpi_status status;

	status = wmi_get_event_data(value, &response);
	if (status != AE_OK) {
		pr_info(AIO_PREFIX "bad event status 0x%x\n", status);
		return;
	}

	obj = (union acpi_object *)response.pointer;
	if (obj) {
		unsigned int scancode;

		switch (obj->type) {
		case ACPI_TYPE_INTEGER:
			/* Most All-In-One correctly return integer scancode */
			scancode = obj->integer.value;
			dell_wmi_aio_handle_key(scancode);
			break;
		case ACPI_TYPE_BUFFER:
			/* Broken machines return the scancode in a buffer */
			if (obj->buffer.pointer && obj->buffer.length > 0) {
				scancode = obj->buffer.pointer[0];
				dell_wmi_aio_handle_key(scancode);
			}
			break;
		}
	}
	kfree(obj);
}

static int __init dell_wmi_aio_input_setup(void)
{
	struct key_entry *key;
	int err;

	dell_wmi_aio_input_dev = input_allocate_device();

	if (!dell_wmi_aio_input_dev)
		return -ENOMEM;

	dell_wmi_aio_input_dev->name = "Dell AIO WMI hotkeys";
	dell_wmi_aio_input_dev->phys = "wmi/input0";
	dell_wmi_aio_input_dev->id.bustype = BUS_HOST;
	dell_wmi_aio_input_dev->getkeycode = dell_wmi_aio_getkeycode;
	dell_wmi_aio_input_dev->setkeycode = dell_wmi_aio_setkeycode;

	for (key = dell_wmi_aio_keymap; key->type != KE_END; key++) {
		switch (key->type) {
		case KE_KEY:
			set_bit(EV_KEY, dell_wmi_aio_input_dev->evbit);
			set_bit(key->keycode, dell_wmi_aio_input_dev->keybit);
			break;
		case KE_SW:
			set_bit(EV_SW, dell_wmi_aio_input_dev->evbit);
			set_bit(key->keycode, dell_wmi_aio_input_dev->swbit);
			break;
		}
	}

	err = input_register_device(dell_wmi_aio_input_dev);

	if (err) {
		input_free_device(dell_wmi_aio_input_dev);
		return err;
	}

	return 0;
}

static char *dell_wmi_aio_find(void)
{
	int i;

	for (i = 0; dell_wmi_aio_guids[i] != NULL; i++)
		if (wmi_has_guid(dell_wmi_aio_guids[i]))
			return dell_wmi_aio_guids[i];

	return NULL;
}

static int __init dell_wmi_aio_init(void)
{
	int err;
	char *guid;

	guid = dell_wmi_aio_find();
	if (guid) {
		err = dell_wmi_aio_input_setup();

		if (err)
			return err;

		err = wmi_install_notify_handler(guid,
						 dell_wmi_aio_notify, NULL);
		if (err) {
			input_unregister_device(dell_wmi_aio_input_dev);
			pr_err(AIO_PREFIX "Unable to register"
			       " notify handler - %d\n", err);
			return err;
		}

	} else
		pr_warning(AIO_PREFIX "No known WMI GUID found\n");

	return 0;
}

static void __exit dell_wmi_aio_exit(void)
{
	char *guid;

	guid = dell_wmi_aio_find();
	if (guid) {
		wmi_remove_notify_handler(guid);
		input_unregister_device(dell_wmi_aio_input_dev);
	}
}

module_init(dell_wmi_aio_init);
module_exit(dell_wmi_aio_exit);
