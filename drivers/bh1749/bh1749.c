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

#define BH1749_SYS_CTRL                 0x40
#define BH1749_SYS_CTRL_PART_ID_MASK    GENMASK(5, 0)

#define BH1749_MANUFACTURER_ID          0x92
#define BH1749_MANUFACTURER_ROHM        0xE0

static int bh1749_probe(struct i2c_client *client)
{
    struct device *dev = &client->dev;
    int ret;

    dev_info(dev, "probe called (addr=0x%02x)\n", client->addr);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
    {
        dev_err(dev, "SMBus byte data not supported\n");
        return -EOPNOTSUPP;
    }

    ret = i2c_smbus_read_byte_data(client, BH1749_MANUFACTURER_ID);
    if (ret < 0)
    {
        dev_err(dev, "failed to read manufacturer id (%d)\n", ret);
        return ret;
    }

    dev_info(dev, "manufacturer id = 0x%02x\n", ret);

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

    dev_info(dev, "part id = 0x%02x (raw 0x%02x)\n", (u8)(ret & BH1749_SYS_CTRL_PART_ID_MASK), (u8)ret);

    dev_info(dev, "chip detected\n");
    return 0;
}

static void bh1749_remove(struct i2c_client *client)
{
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
    },
    .probe = bh1749_probe,
    .remove = bh1749_remove,
    .id_table = bh1749_id,
};

module_i2c_driver(bh1749_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("chan");
MODULE_DESCRIPTION("ROHM BH1749NUC colour sensor driver");
