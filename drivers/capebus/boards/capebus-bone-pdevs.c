/*
 * TI Beaglebone capebus controller - Platform adapters
 *
 * Copyright (C) 2012 Pantelis Antoniou <panto@antoniou-consulting.com>
 * Copyright (C) 2012 Texas Instruments Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/consumer.h>
#include <linux/atomic.h>
#include <linux/clk.h>
#include <asm/barrier.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_i2c.h>
#include <linux/spi/spi.h>

#include <linux/capebus/capebus-bone.h>

struct i2c_priv {
	struct i2c_adapter *i2c_adapter;
	phandle parent_handle;
};

static const struct of_device_id of_i2c_dt_match[] = {
	{ .compatible = "i2c-dt", },
	{},
};

static int __devinit i2c_dt_probe(struct platform_device *pdev)
{
	struct i2c_priv *priv = NULL;
	int ret = -EINVAL;
	struct device_node *adap_node;
	u32 val;

	if (pdev->dev.of_node == NULL) {
		dev_err(&pdev->dev, "Only support OF case\n");
		return -ENOMEM;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL) {
		dev_err(&pdev->dev, "Failed to allocate priv\n");
		return -ENOMEM;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "parent", &val);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to find parent property\n");
		goto err_prop_fail;
	}
	priv->parent_handle = val;

	adap_node = of_find_node_by_phandle(priv->parent_handle);
	if (adap_node == NULL) {
		dev_err(&pdev->dev, "Failed to find i2c adapter node\n");
		ret = -EINVAL;
		goto err_node_fail;
	}

	ret = capebus_of_platform_device_enable(adap_node);
	if (ret != 0) {
		dev_info(&pdev->dev, "I2C adapter platform device failed "
				"to enable\n");
		goto err_enable_fail;
	}

	priv->i2c_adapter = of_find_i2c_adapter_by_node(adap_node);
	if (priv->i2c_adapter == NULL) {
		dev_err(&pdev->dev, "Failed to find i2c adapter node\n");
		ret = -EINVAL;
		goto err_adap_fail;
	}

	of_i2c_register_node_devices(priv->i2c_adapter, pdev->dev.of_node);

	of_node_put(adap_node);

	dev_info(&pdev->dev, "Registered bone I2C OK.\n");

	platform_set_drvdata(pdev, priv);

	return 0;
err_adap_fail:
	of_node_put(adap_node);
err_enable_fail:
	/* nothing */
err_node_fail:
	/* nothing */
err_prop_fail:
	devm_kfree(&pdev->dev, priv);
	return ret;
}

static int __devexit i2c_dt_remove(struct platform_device *pdev)
{
	return -EINVAL;	/* not supporting removal yet */
}

static struct platform_driver i2c_dt_driver = {
	.probe		= i2c_dt_probe,
	.remove		= __devexit_p(i2c_dt_remove),
	.driver		= {
		.name	= "i2c-dt",
		.owner	= THIS_MODULE,
		.of_match_table = of_i2c_dt_match,
	},
};

struct spi_priv {
	struct spi_master *master;
	phandle parent_handle;
};

static const struct of_device_id of_spi_dt_match[] = {
	{ .compatible = "spi-dt", },
	{},
};

static int of_dev_node_match(struct device *dev, void *data)
{
        return dev->of_node == data;
}

/* must call put_device() when done with returned i2c_adapter device */
static struct spi_master *of_find_spi_master_by_node(struct device_node *node)
{
	struct device *dev;
	struct spi_master *master;

	dev = class_find_device(&spi_master_class, NULL, node,
					 of_dev_node_match);
	if (!dev)
		return NULL;

	master = container_of(dev, struct spi_master, dev);

	/* TODO: No checks what-so-ever... be careful. */
	return master;
}

static int __devinit spi_dt_probe(struct platform_device *pdev)
{
	struct spi_priv *priv = NULL;
	int ret = -EINVAL;
	struct device_node *master_node;
	u32 val;

	if (pdev->dev.of_node == NULL) {
		dev_err(&pdev->dev, "Only support OF case\n");
		return -ENOMEM;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL) {
		dev_err(&pdev->dev, "Failed to allocate priv\n");
		return -ENOMEM;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "parent", &val);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to find parent property\n");
		goto err_prop_fail;
	}
	priv->parent_handle = val;

	master_node = of_find_node_by_phandle(priv->parent_handle);
	if (master_node == NULL) {
		dev_err(&pdev->dev, "Failed to find spi bus master node\n");
		ret = -EINVAL;
		goto err_node_fail;
	}

	ret = capebus_of_platform_device_enable(master_node);
	if (ret != 0) {
		dev_info(&pdev->dev, "SPI platform device failed to enable\n");
		goto err_enable_fail;
	}

	priv->master = of_find_spi_master_by_node(master_node);
	if (priv->master == NULL) {
		dev_err(&pdev->dev, "Failed to find bus master node\n");
		ret = -EINVAL;
		goto err_master_fail;
	}

	of_register_node_spi_devices(priv->master, pdev->dev.of_node);

	of_node_put(master_node);

	dev_info(&pdev->dev, "Registered bone SPI OK.\n");

	platform_set_drvdata(pdev, priv);

	return 0;
err_master_fail:
	of_node_put(master_node);
err_enable_fail:
	/* nothing */
err_node_fail:
	/* nothing */
err_prop_fail:
	devm_kfree(&pdev->dev, priv);
	return ret;
}

static int __devexit spi_dt_remove(struct platform_device *pdev)
{
	return -EINVAL;	/* not supporting removal yet */
}

static struct platform_driver spi_dt_driver = {
	.probe		= spi_dt_probe,
	.remove		= __devexit_p(spi_dt_remove),
	.driver		= {
		.name	= "spi-dt",
		.owner	= THIS_MODULE,
		.of_match_table = of_spi_dt_match,
	},
};

/*******************************************************************/

struct pruss_priv {
	phandle parent_handle;
};

static const struct of_device_id of_pruss_dt_match[] = {
	{ .compatible = "pruss-dt", },
	{},
};

static int __devinit pruss_dt_probe(struct platform_device *pdev)
{
	struct pruss_priv *priv = NULL;
	int ret = -EINVAL;
	struct device_node *master_node;
	u32 val;

	if (pdev->dev.of_node == NULL) {
		dev_err(&pdev->dev, "Only support OF case\n");
		return -ENOMEM;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL) {
		dev_err(&pdev->dev, "Failed to allocate priv\n");
		return -ENOMEM;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "parent", &val);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to find parent property\n");
		goto err_prop_fail;
	}
	priv->parent_handle = val;

	master_node = of_find_node_by_phandle(priv->parent_handle);
	if (master_node == NULL) {
		dev_err(&pdev->dev, "Failed to find PRUSS node\n");
		ret = -EINVAL;
		goto err_node_fail;
	}

	ret = capebus_of_platform_device_enable(master_node);
	if (ret != 0) {
		dev_info(&pdev->dev, "PRUSS platform device failed to enable\n");
		goto err_enable_fail;
	}

	of_node_put(master_node);

	dev_info(&pdev->dev, "Registered bone PRUSS OK.\n");

	platform_set_drvdata(pdev, priv);

	return 0;

err_enable_fail:
	/* nothing */
err_node_fail:
	/* nothing */
err_prop_fail:
	devm_kfree(&pdev->dev, priv);
	return ret;
}

static int __devexit pruss_dt_remove(struct platform_device *pdev)
{
	return -EINVAL;	/* not supporting removal yet */
}

static struct platform_driver pruss_dt_driver = {
	.probe		= pruss_dt_probe,
	.remove		= __devexit_p(pruss_dt_remove),
	.driver		= {
		.name	= "pruss-dt",
		.owner	= THIS_MODULE,
		.of_match_table = of_pruss_dt_match,
	},
};

/*******************************************************************/





static int of_is_printable_string(const void *data, int len)
{
	const char *s = data;
	const char *ss, *se;

	/* zero length is not */
	if (len == 0)
		return 0;

	/* must terminate with zero */
	if (s[len - 1] != '\0')
		return 0;

	se = s + len;

	while (s < se) {
		ss = s;
		while (s < se && *s && isprint(*s))
			s++;

		/* not zero, or not done yet */
		if (*s != '\0' || s == ss)
			return 0;

		s++;
	}

	return 1;
}

static char *of_dump_prop(struct property *prop)
{
	const void *data = prop->value;
	int len = prop->length;
	int i, tbuflen;
	const char *p = data;
	const char *s;
	char *buf, *bufp, *bufe;

	/* play it safe */
	buf = kmalloc(PAGE_SIZE + len * 8, GFP_KERNEL);
	if (buf == NULL)
		return NULL;
	bufp = buf;
	bufe = buf + PAGE_SIZE + len * 8;

#undef append_sprintf
#define append_sprintf(format, ...) \
	do { \
		tbuflen = snprintf(NULL, 0, format, ## __VA_ARGS__); \
		if (bufp + tbuflen + 1 >= bufe) \
			goto err_out; \
		snprintf(bufp, tbuflen + 1, format, ## __VA_ARGS__); \
		bufp += tbuflen; \
	} while(0)

	if (len == 0) {
		/* nothing; just terminate */
		*buf = '\0';
		return buf;
	}

	append_sprintf(" = ");

	if (of_is_printable_string(data, len)) {
		s = data;
		do {
			append_sprintf("\"%s\"", s);
			s += strlen(s) + 1;
			if (s < (const char *)data + len)
				append_sprintf(", ");
		} while (s < (const char *)data + len);

	} else if ((len % 4) == 0) {
		append_sprintf("<");
		for (i = 0; i < len; i += 4)
			append_sprintf("0x%08x%s",
				be32_to_cpu(*(uint32_t *)(p + i)),
				i < (len - 4) ? " " : "");
		append_sprintf(">");
	} else {
		append_sprintf("[");
		for (i = 0; i < len; i++)
			append_sprintf("%02x%s", *(p + i),
					i < len - 1 ? " " : "");
		append_sprintf("]");
	}

	return buf;
err_out:
	kfree(buf);
	return NULL;
#undef append_sprintf
}

static void of_dump_tree(struct device *dev, int level,
		struct device_node *node)
{
	struct device_node *child;
	struct property *prop;
	static const char *leveltab = "\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";
	int maxlevel = strlen(leveltab);
	const char *thislevel;
	const char *nextlevel;
	char *propstr;

	thislevel = leveltab + maxlevel - level;
	if (thislevel < leveltab)
		thislevel = leveltab;
	nextlevel = thislevel - 1;
	if (nextlevel < leveltab)
		nextlevel = leveltab;

	dev_info(dev, "%s%s {\n", thislevel, node->name);

	for_each_property_of_node(node, prop) {

		if (strcmp(prop->name, "name") == 0)
			continue;

		propstr = of_dump_prop(prop);
		dev_info(dev, "%s%s%s;\n", nextlevel, prop->name,
				propstr ? propstr : "*ERROR*");
		kfree(propstr);
	}

	for_each_child_of_node(node, child)
		of_dump_tree(dev, level + 1, child);

	dev_info(dev, "%s};", thislevel);
}

struct dt_fragment {
	struct list_head node;
	struct device_node *target_node;
	int status_changed : 1;
	int status_new : 1;

	struct device_node *saved_tree;
};

struct dt_overlay_priv {
	struct list_head frag_list;
};

static const struct of_device_id of_dt_overlay_match[] = {
	{ .compatible = "dt-overlay", },
	{},
};

#define for_each_property_of_node_safe(dn, pp, ppn) \
	for (pp = dn->properties, ppn = pp ? pp->next : NULL; pp != NULL; pp = ppn, ppn = pp->next)

static void of_overlay_node(struct device *dev, struct device_node *target, struct device_node *overlay)
{
	struct device_node *child;
	struct device_node **childp;
	struct property *prop, *propn;
	struct property **propp;
	char *propstr;
	int found;

	dev_info(dev, "Overlaying target %p with overlay %p\n", target, overlay);

	while ((prop = overlay->properties) != NULL) {
		/* remove head prop */
		overlay->properties = prop->next;
		prop->next = NULL;

		found = 0;
		propp = &target->properties;
		while (*propp) {

			/* found? */
			if (of_prop_cmp((*propp)->name, prop->name) == 0) {
				found = 1;
				break;
			}

			propp = &(*propp)->next;
		}

		if (found) {
			propn = (*propp)->next;
			prop->next = propn;
			/* propn is now dead */

		}

		*propp = prop;

		propstr = of_dump_prop(prop);
		dev_info(dev, "%c %s%s;\n", found ? 'U' : 'I', prop->name, propstr);
		kfree(propstr);
	}

	/* remove each child */
	while ((child = overlay->child) != NULL) {
		overlay->child = child->sibling;
		child->sibling = NULL;

		found = 0;
		childp = &target->child;
		while (*childp) {

			/* found? */
			if ((*childp)->name && of_node_cmp((*childp)->name, child->name) == 0) {
				found = 1;
				break;
			}

			childp = &(*childp)->sibling;
		}

		if (found) {
			dev_info(dev, "overlaying node '%s'\n", child->name);
			of_overlay_node(dev, (*childp), child);
		} else {
			*childp = child;
			child->parent = target;

			dev_info(dev, "inserted node '%s'\n", child->name);
		}
	}
}

static int __devinit dt_overlay_probe(struct platform_device *pdev)
{
	struct dt_overlay_priv *priv = NULL;
	struct dt_fragment *frag;
	int ret = -EINVAL;
	struct device_node *target_node, *node, *tnode;
	u32 val;
	int prev_status, new_status;
	struct platform_device *target_pdev, *parent_pdev;
	struct list_head *lh, *lhp;
	unsigned long flags;

	frag = NULL;
	target_node = NULL;
	ret = 0;

	if (pdev->dev.of_node == NULL) {
		dev_err(&pdev->dev, "Only support OF case\n");
		return -ENOMEM;
	}

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL) {
		dev_err(&pdev->dev, "Failed to allocate priv\n");
		return -ENOMEM;
	}
	INIT_LIST_HEAD(&priv->frag_list);

	frag = NULL;
	target_node = NULL;
	ret = 0;

	/* iterate over fragment children */
	for_each_child_of_node(pdev->dev.of_node, node) {

		frag = NULL;
		target_node = NULL;

		frag = devm_kzalloc(&pdev->dev, sizeof(*frag), GFP_KERNEL);
		if (frag == NULL) {
			dev_err(&pdev->dev, "Failed to allocate frag\n");
			ret = -ENOMEM;
			goto err_fail;
		}

		ret = of_property_read_u32(node, "target", &val);
		if (ret != 0) {
			dev_err(&pdev->dev, "Failed to find target property\n");
			goto err_fail;
		}

		target_node = of_find_node_by_phandle(val);
		if (target_node == NULL) {
			dev_err(&pdev->dev, "Failed to find target node\n");
			ret = -EINVAL;
			goto err_fail;
		}

		frag->target_node = target_node;

		prev_status = of_device_is_available(target_node);

		tnode = of_get_child_by_name(node, "content");

		dev_info(&pdev->dev, "overlay %p (before)\n", tnode);
		of_dump_tree(&pdev->dev, 1, tnode);
		dev_info(&pdev->dev, "\n");

		dev_info(&pdev->dev, "target %p (before)\n", target_node);
		of_dump_tree(&pdev->dev, 1, target_node);
		dev_info(&pdev->dev, "\n");

		write_lock_irqsave(&devtree_lock, flags);
		of_overlay_node(&pdev->dev, target_node, tnode);
		write_unlock_irqrestore(&devtree_lock, flags);

		dev_info(&pdev->dev, "target %p (after)\n", target_node);
		of_dump_tree(&pdev->dev, 1, target_node);
		dev_info(&pdev->dev, "\n");

		of_node_put(tnode);

		new_status = of_device_is_available(target_node);

		dev_info(&pdev->dev, "target prev %d new %d\n",
				prev_status, new_status);

		if (prev_status != new_status) {

			if (new_status) {
				parent_pdev = of_find_device_by_node(target_node->parent);

				target_pdev = of_platform_device_create(target_node, NULL,
						parent_pdev ? &parent_pdev->dev : NULL);
				if (target_pdev == NULL) {
					dev_info(&pdev->dev, "failed to enable platform_device\n");
					platform_device_put(parent_pdev);
					goto err_fail;
				}
			} else {
				target_pdev = of_find_device_by_node(target_node);
				if (target_pdev != NULL) {
					dev_info(&pdev->dev, "Cannot find platform_device yet\n");
					ret = -ENODEV;
					goto err_fail;
				}
				platform_device_unregister(target_pdev);
				platform_device_put(target_pdev);
			}

			frag->status_changed = 1;
			frag->status_new = new_status;

		}

		list_add_tail(&frag->node, &priv->frag_list);
	}

	dev_info(&pdev->dev, "Completed.\n");

	platform_set_drvdata(pdev, priv);

	return 0;

err_fail:
	of_node_put(target_node);
	devm_kfree(&pdev->dev, frag);

	/* remove all in reverse */
	list_for_each_prev_safe(lh, lhp, &priv->frag_list) {
		frag = list_entry(lh, struct dt_fragment, node);
		list_del(lh);

		/* TODO: remove overlay */

		of_node_put(frag->target_node);
		devm_kfree(&pdev->dev, frag);
	}
	return ret;
}

static int __devexit dt_overlay_remove(struct platform_device *pdev)
{
	return -EINVAL;	/* not supporting removal yet */
}

static struct platform_driver dt_overlay_driver = {
	.probe		= dt_overlay_probe,
	.remove		= __devexit_p(dt_overlay_remove),
	.driver		= {
		.name	= "dt-overlay",
		.owner	= THIS_MODULE,
		.of_match_table = of_dt_overlay_match,
	},
};

/*
 *
 */
struct bone_capebus_pdev_driver {
	struct platform_driver *driver;
	unsigned int registered : 1;
	/* more? */
};

static struct bone_capebus_pdev_driver pdev_drivers[] = {
	{
		.driver		= &i2c_dt_driver,
	},
	{
		.driver		= &spi_dt_driver,
	},
	{
		.driver		= &pruss_dt_driver,
	},
	{
		.driver		= &dt_overlay_driver,
	},
	{
		.driver		= NULL,
	}
};

int bone_capebus_register_pdev_adapters(struct bone_capebus_bus *bus)
{
	struct bone_capebus_pdev_driver *drvp;
	int err;

	/* first check if we do it twice */
	for (drvp = pdev_drivers; drvp->driver != NULL; drvp++)
		if (drvp->registered)
			return -EBUSY;

	for (drvp = pdev_drivers; drvp->driver != NULL; drvp++) {

		err = platform_driver_register(drvp->driver);
		if (err != 0)
			goto err_out;

		drvp->registered = 1;

		dev_info(bus->dev, "Registered %s "
				"platform driver\n", drvp->driver->driver.name);
	}

	return 0;

err_out:
	dev_err(bus->dev, "Failed to register %s "
			"platform driver\n", drvp->driver->driver.name);

	/* unregister */
	while (--drvp >= pdev_drivers) {

		if (!drvp->registered)
			continue;

		platform_driver_unregister(drvp->driver);
	}

	return err;
}

void bone_capebus_unregister_pdev_adapters(struct bone_capebus_bus *bus)
{
	struct bone_capebus_pdev_driver *drvp;

	/* unregister */
	drvp = &pdev_drivers[ARRAY_SIZE(pdev_drivers)];
	while (--drvp >= pdev_drivers) {

		if (drvp->driver == NULL)	/* skip terminator */
			continue;

		if (!drvp->registered)
			continue;

		platform_driver_unregister(drvp->driver);

		drvp->registered = 0;
	}
}
