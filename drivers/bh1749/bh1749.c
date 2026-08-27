// SPDX-License-Identifier: GPL-2.0
/*
 * ROHM BH1749NUC digital colour sensor driver
 *
 * 7-bit I2C slave addresses:
 *  0x38 (ADDR pin low)
 *  0x39 (ADDR pin high)
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/mutex.h>

#define BH1749_SYS_CTRL                 0x40
#define BH1749_SYS_CTRL_SW_RESET        BIT(7)
#define BH1749_SYS_CTRL_INT_RESET       BIT(6)
#define BH1749_SYS_CTRL_PART_ID_MASK    GENMASK(5, 0)
#define BH1749_PART_ID                  0x0D

#define BH1749_MODE_CTRL1               0x41
#define BH1749_CTRL1_IR_GAIN_X1         (0x01 << 5)
#define BH1749_CTRL1_RGB_GAIN_X1        (0x01 << 3)
#define BH1749_CTRL1_MEAS_120MS         0x02
#define BH1749_MEAS_TIME_MS             120

#define BH1749_CTRL1_DEFAULT            (BH1749_CTRL1_IR_GAIN_X1 | BH1749_CTRL1_RGB_GAIN_X1 | BH1749_CTRL1_MEAS_120MS)

#define BH1749_MODE_CTRL2               0x42
#define BH1749_CTRL2_VALID              BIT(7)
#define BH1749_CTRL2_RGB_EN             BIT(4)

#define BH1749_RED_DATA                 0x50
#define BH1749_GREEN_DATA               0x52
#define BH1749_BLUE_DATA                0x54
#define BH1749_IR_DATA                  0x58
#define BH1749_GREEN2_DATA              0x5A

#define BH1749_MANUFACTURER_ID          0x92
#define BH1749_MANUFACTURER_ROHM        0xE0

struct bh1749_data
{
    struct i2c_client *client;
    struct mutex lock;
};

static int bh1749_read_word(struct bh1749_data *data, u8 reg)
{
    int ret;

    mutex_lock(&data->lock);
    ret = i2c_smbus_read_word_data(data->client, reg);
    mutex_unlock(&data->lock);

    return ret;
}

static ssize_t bh1749_show_channel(struct device *dev, u8 reg, char *buf)
{
    struct bh1749_data *data = i2c_get_clientdata(to_i2c_client(dev));
    int ret;

    ret = bh1749_read_word(data, reg);
    if (ret < 0)
    {
        return ret;
    }
    return sysfs_emit(buf, "%u\n", (u16)ret);
}

#define BH1749_CHANNEL_ATTR(name, reg)      \
static ssize_t name##_show(struct device *dev, struct device_attribute *attr, char *buf) \
{   \
    return bh1749_show_channel(dev, reg, buf);  \
}   \
static DEVICE_ATTR_RO(name)

BH1749_CHANNEL_ATTR(red,    BH1749_RED_DATA);
BH1749_CHANNEL_ATTR(green,  BH1749_GREEN_DATA);
BH1749_CHANNEL_ATTR(blue,   BH1749_BLUE_DATA);
BH1749_CHANNEL_ATTR(ir,     BH1749_IR_DATA);
BH1749_CHANNEL_ATTR(green2, BH1749_GREEN2_DATA);

static ssize_t valid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct bh1749_data *data = i2c_get_clientdata(to_i2c_client(dev));
    int ret;

    ret = i2c_smbus_read_byte_data(data->client, BH1749_MODE_CTRL2);
    if (ret < 0)
        return ret;

    return sysfs_emit(buf, "%d\n", !!(ret & BH1749_CTRL2_VALID));
}
static DEVICE_ATTR_RO(valid);

static struct attribute *bh1749_attrs[] = 
{
    &dev_attr_red.attr,
    &dev_attr_green.attr,
    &dev_attr_blue.attr,
    &dev_attr_ir.attr,
    &dev_attr_green2.attr,
    &dev_attr_valid.attr,
    NULL
};
ATTRIBUTE_GROUPS(bh1749);

static int bh1749_init_chip(struct bh1749_data *data)
{
    struct i2c_client *client = data->client;
    int ret;

    ret = i2c_smbus_write_byte_data(client, BH1749_SYS_CTRL, BH1749_SYS_CTRL_SW_RESET);
    if (ret < 0)
        return ret;

    msleep(10);

    ret = i2c_smbus_write_byte_data(client, BH1749_MODE_CTRL1, BH1749_CTRL1_DEFAULT);
    if (ret < 0)
        return ret;

    ret = i2c_smbus_write_byte_data(client, BH1749_MODE_CTRL2, BH1749_CTRL2_RGB_EN);
    if (ret < 0)
        return ret;

    msleep(BH1749_MEAS_TIME_MS * 2);
    
    return 0;
}

static int bh1749_probe(struct i2c_client *client)
{
    struct device *dev = &client->dev;
    struct bh1749_data *data;
    int ret;

    dev_info(dev, "probe called (addr=0x%02x)\n", client->addr);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA))
    {
        dev_err(dev, "required SMBus transfers not supported\n");
        return -EOPNOTSUPP;
    }

    data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client = client;
    mutex_init(&data->lock);
    i2c_set_clientdata(client, data);

    ret = i2c_smbus_read_byte_data(client, BH1749_MANUFACTURER_ID);
    if (ret < 0)
    {
        dev_err(dev, "failed to read manufacturer id (%d)\n", ret);
        return ret;
    }

    if (ret != BH1749_MANUFACTURER_ROHM)
    {
        dev_err(dev, "unexpected manufacturer id 0x%02x\n", ret);
        return -ENODEV;
    }

    ret = i2c_smbus_read_byte_data(client, BH1749_SYS_CTRL);
    if (ret < 0)
    {
        dev_err(dev, "failed to read system control (%d)\n", ret);
        return ret;
    }

    if ((ret & BH1749_SYS_CTRL_PART_ID_MASK) != BH1749_PART_ID)
    {
        dev_err(dev, "unexpected part id 0x%02x\n", (u8)(ret & BH1749_SYS_CTRL_PART_ID_MASK));
        return -ENODEV;
    }

    ret = bh1749_init_chip(data);
    if (ret < 0)
    {
        dev_err(dev, "chip init failed (%d)\n", ret);
        return ret;
    }

    dev_info(dev, "BH1749NUC ready (part id 0x%02x)\n", BH1749_PART_ID);

    ret = bh1749_read_word(data, BH1749_RED_DATA);
    if (ret >= 0)
        dev_info(dev, "initial red = %u\n", (u16)ret);

    return 0;
}

static void bh1749_remove(struct i2c_client *client)
{
    i2c_smbus_write_byte_data(client, BH1749_MODE_CTRL2, 0x00);
    dev_info(&client->dev, "remove called\n");
}

static const struct i2c_device_id bh1749_id[] = 
{
    { "bh1749" },
    { }
};
MODULE_DEVICE_TABLE(i2c, bh1749_id);

static const struct of_device_id bh1749_of_match[] = 
{
    { .compatible = "rohm,bh1749" },
    { }
};
MODULE_DEVICE_TABLE(of, bh1749_of_match);

static struct i2c_driver bh1749_driver = 
{
    .driver = 
    {
        .name = "bh1749",
        .of_match_table = bh1749_of_match,
        .dev_groups = bh1749_groups,
    },
    .probe = bh1749_probe,
    .remove = bh1749_remove,
    .id_table = bh1749_id,
};

module_i2c_driver(bh1749_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("chan");
MODULE_DESCRIPTION("ROHM BH1749NUC colour sensor driver");
