#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/leds.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>

static u32 my_value;
static struct gpio_desc *my_gpio;

static struct gpio_desc *my_button;
static int my_irq;
static unsigned int irq_count;
static unsigned int bounce_count;
static unsigned long last_jiffies;

#define CHAN_BOUNCE_MS		200
static DEFINE_SPINLOCK(irq_lock);

static dev_t chan_devt;
static struct cdev chan_cdev;
static struct class *chan_class;
static struct led_classdev chan_led;

static ssize_t value_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", my_value);
}

static ssize_t value_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	u32 tmp;
	int ret;

	ret = kstrtou32(buf, 0, &tmp);
	if (ret)
		return ret;

	my_value = tmp;
	if (my_gpio)
	{
		gpiod_set_value(my_gpio, my_value ? 1 : 0);
	}
	dev_info(dev, "value set to %u\n", my_value);
	return count;
}

static DEVICE_ATTR_RW(value);

static ssize_t irq_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned long flags;
	unsigned int count;

	spin_lock_irqsave(&irq_lock, flags);
	count = irq_count;
	spin_unlock_irqrestore(&irq_lock, flags);

	return sysfs_emit(buf, "%u\n", count);
}
static DEVICE_ATTR_RO(irq_count);

static ssize_t bounce_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	unsigned long flags;
	unsigned int bounces;

	spin_lock_irqsave(&irq_lock, flags);
	bounces = bounce_count;
	spin_unlock_irqrestore(&irq_lock, flags);

	return sysfs_emit(buf, "%u\n", bounces);
}
static DEVICE_ATTR_RO(bounce_count);

static int chan_open(struct inode *inode, struct file *filp)
{
	pr_info("chan_drv: open\n");
	return 0;
}

static int chan_release(struct inode *inode, struct file *filp)
{
	pr_info("chan_drv: release\n");
	return 0;
}

static ssize_t chan_read(struct file *filp, char __user *buf,
		size_t len, loff_t *off)
{
	char tmp[16];
	int n;
	
	n = scnprintf(tmp, sizeof(tmp), "%u\n", my_value);
	return simple_read_from_buffer(buf, len, off, tmp, n);
}

static ssize_t chan_write(struct file *filp, const char __user *buf,
		size_t len, loff_t *off)
{
	char tmp[16];
	u32 val;
	int ret;
	
	if (len == 0 || len >= sizeof(tmp))
		return -EINVAL;
	
	if (copy_from_user(tmp, buf, len))
		return -EFAULT;
	
	tmp[len] = '\0';
	
	ret = kstrtou32(strim(tmp), 0, &val);
	if (ret)
		return ret;
	
	my_value = val;
	if (my_gpio)
	{
		gpiod_set_value(my_gpio, val ? 1 : 0);
	}
	pr_info("chan_drv: write %u\n", val);
	return len;
}

static const struct file_operations chan_fops = {
	.owner 	= THIS_MODULE,
	.open	= chan_open,
	.release= chan_release,
	.read	= chan_read,
	.write	= chan_write,
};

static void chan_led_set(struct led_classdev *cdev, enum led_brightness b)
{
	if (my_gpio)
		gpiod_set_value(my_gpio, b ? 1 : 0);

	my_value = b;
}

static enum led_brightness chan_led_get(struct led_classdev *cdev)
{
	return my_value ? LED_ON : LED_OFF;
}

static irqreturn_t chan_button_isr(int irq, void *dev_id)
{
	struct device *dev = dev_id;
	unsigned long flags;
	unsigned int count, bounces;
	bool ignore = false;

	spin_lock_irqsave(&irq_lock, flags);

	if (last_jiffies && time_before(jiffies, last_jiffies + msecs_to_jiffies(CHAN_BOUNCE_MS)))
	{
		bounce_count++;
		ignore = true;
	}
	else
	{
		last_jiffies = jiffies;
		irq_count++;
	}

	count = irq_count;
	bounces = bounce_count;
	spin_unlock_irqrestore(&irq_lock, flags);

	if (ignore)
		return IRQ_HANDLED;

	my_value = !my_value;
	if (my_gpio)
	{
		gpiod_set_value(my_gpio, my_value ? 1 : 0);
	}
	dev_info(dev, "button irq #%u (led=%u, bounced=%u)\n", count, my_value, bounces);

	return IRQ_HANDLED;
}

static int my_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const char *str;
	u32 val;
	int ret;

	dev_info(dev, "probe called (node=%pOF)\n", dev->of_node);

	ret = of_property_read_u32(dev->of_node, "my-number", &val);
	if (ret)
		dev_warn(dev, "my-number not found (%d)\n", ret);
	else
	{
		dev_info(dev, "my-number = %u\n", val);
		my_value = val;
	}

	ret = of_property_read_string(dev->of_node, "my-string", &str);
	if (ret)
		dev_warn(dev, "my-string not found (%d)\n", ret);
	else
		dev_info(dev, "my-string = %s\n", str);

	my_gpio = devm_gpiod_get(dev, NULL, GPIOD_OUT_LOW);
	if (IS_ERR(my_gpio)){
		dev_err(dev, "GPIO acquire failure (%ld)\n", PTR_ERR(my_gpio));
		return PTR_ERR(my_gpio);
	}
	dev_info(dev, "GPIO acquire success\n");

	my_button = devm_gpiod_get(dev, "button", GPIOD_IN);
	if (IS_ERR(my_button))
	{
		dev_err(dev, "button GPIO acquire failure (%ld)\n", PTR_ERR(my_button));
		return PTR_ERR(my_button);
	}

	my_irq = gpiod_to_irq(my_button);
	if (my_irq < 0)
	{
		dev_err(dev, "failed to map gpio to irq (%d)\n", my_irq);
		return my_irq;
	}
	
	ret = devm_request_threaded_irq(dev, my_irq, NULL, chan_button_isr, IRQF_TRIGGER_RISING | IRQF_ONESHOT, "chan_button", dev);
	if (ret)
	{
		dev_err(dev, "failed to request irq (%d)\n", ret);
		return ret;
	}
	
	dev_info(dev, "button irq %d registered\n", my_irq);
	
	ret = device_create_file(dev, &dev_attr_value);
	if (ret) 
	{
		dev_err(dev, "sysfs make failure (%d)\n", ret);
		return ret;
	}

	ret = device_create_file(dev, &dev_attr_irq_count);
	if (ret)
	{
		dev_err(dev, "irq_count sysfs failure (%d)\n", ret);
		goto err_sysfs;
	}
	
	ret = device_create_file(dev, &dev_attr_bounce_count);
	if (ret)
	{
		dev_err(dev, "bounce_count sysfs failure (%d)\n", ret);
		goto err_irqcount;
	}
	
	ret = alloc_chrdev_region(&chan_devt, 0, 1, "my_device");
	if (ret) 
	{
		dev_err(dev, "alloc_chrdev_region failed (%d)\n", ret);
		goto err_bounce;
	}
	
	cdev_init(&chan_cdev, &chan_fops);
	chan_cdev.owner = THIS_MODULE;
	ret = cdev_add(&chan_cdev, chan_devt, 1);
	if (ret) {
		dev_err(dev, "cdev_add failed (%d)\n", ret);
		goto err_region;
	}
	
	chan_class = class_create("chan_class");
	if (IS_ERR(chan_class))
	{
		ret = -ENODEV;
		goto err_cdev;
	}
	
	if (IS_ERR(device_create(chan_class, NULL, chan_devt, NULL, "my_device")))
	{
		ret = -ENODEV;
		goto err_class;
	}

	chan_led.name = "chan:led";
	chan_led.max_brightness = LED_ON;
	chan_led.brightness_set = chan_led_set;
	chan_led.brightness_get = chan_led_get;
	chan_led.default_trigger = NULL;

	ret = devm_led_classdev_register(dev, &chan_led);
	if (ret) {
		dev_err(dev, "led_classdev register failed (%d)\n", ret);
		goto err_device;
	}
	dev_info(dev, "led ready: /sys/class/leds/%s/brightness\n", chan_led.name);
	
	dev_info(dev, "chardev ready: /dev/my_device (major=%d minor=%d)\n",
		MAJOR(chan_devt), MINOR(chan_devt));	
	dev_info(dev, "sysfs ready: /sys/devices/platform/my_device/value\n");
	return 0;
	
	err_device:
		device_destroy(chan_class, chan_devt);
	err_class:
		class_destroy(chan_class);
	err_cdev:
		cdev_del(&chan_cdev);
	err_region:
		unregister_chrdev_region(chan_devt, 1);
	err_bounce:
		device_remove_file(dev, &dev_attr_bounce_count);
	err_irqcount:
		device_remove_file(dev, &dev_attr_irq_count);
	err_sysfs:
		device_remove_file(dev, &dev_attr_value);
	return ret;
}

static void my_remove(struct platform_device *pdev)
{
	device_destroy(chan_class, chan_devt);
	class_destroy(chan_class);
	cdev_del(&chan_cdev);
	unregister_chrdev_region(chan_devt, 1);
	
	device_remove_file(&pdev->dev, &dev_attr_bounce_count);
	device_remove_file(&pdev->dev, &dev_attr_irq_count);
	device_remove_file(&pdev->dev, &dev_attr_value);
	dev_info(&pdev->dev, "remove called\n");
}

static const struct of_device_id my_of_match[] = {
	{ .compatible = "chan, my-device" },
	{ .compatible = "chan,my-device" },
	{ }
};
MODULE_DEVICE_TABLE(of, my_of_match);

static struct platform_driver my_driver = {
	.probe = my_probe,
	.remove = my_remove,
	.driver = {
		.name = "my_dev_drv",
		.of_match_table = my_of_match,
	},
};

module_platform_driver(my_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("chan");
MODULE_DESCRIPTION("platform driver matching chan,my-device");
