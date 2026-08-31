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
#include <linux/iio/iio.h>

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

#define BH1749_CHANNEL(_colour, _addr)              \
{                                                   \
    .type = IIO_INTENSITY,                          \
    .modified = 1,                                  \
    .channel2 = IIO_MOD_LIGHT_##_colour,            \
    .address = _addr,                               \
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),   \
}

static const struct iio_chan_spec bh1749_channels[] = 
{
    BH1749_CHANNEL(RED,     BH1749_RED_DATA),
    BH1749_CHANNEL(GREEN,   BH1749_GREEN_DATA),
    BH1749_CHANNEL(BLUE,    BH1749_BLUE_DATA),
    BH1749_CHANNEL(IR,      BH1749_IR_DATA),
};

static int bh1749_read_raw(struct iio_dev *indio_dev,
            struct iio_chan_spec const *chan,
            int *val, int *val2, long mask)
{
    struct bh1749_data *data = iio_priv(indio_dev);
    int ret;

    switch (mask)
    {
        case IIO_CHAN_INFO_RAW:
            mutex_lock(&data->lock);
            ret = i2c_smbus_read_word_data(data->client, chan->address);
            mutex_unlock(&data->lock);

            if (ret < 0)
                return ret;

            *val = (u16)ret;
            return IIO_VAL_INT;

        default:
            return -EINVAL;
    }
}

static const struct iio_info bh1749_info = 
{
    .read_raw = bh1749_read_raw,
};

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

static void bh1749_power_off(void *data_ptr)
{
    struct bh1749_data *data = data_ptr;

    i2c_smbus_write_byte_data(data->client, BH1749_MODE_CTRL2, 0x00);
}

static int bh1749_probe(struct i2c_client *client)
{
    struct device *dev = &client->dev;
    struct bh1749_data *data;
    struct iio_dev *indio_dev;
    int ret;

    dev_info(dev, "probe called (addr=0x%02x)\n", client->addr);

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA))
    {
        dev_err(dev, "required SMBus transfers not supported\n");
        return -EOPNOTSUPP;
    }

    indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
    if (!indio_dev)
        return -ENOMEM;

    data = iio_priv(indio_dev);
    data->client = client;
    mutex_init(&data->lock);
    i2c_set_clientdata(client, indio_dev);

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

    ret = devm_add_action_or_reset(dev, bh1749_power_off, data);
    if (ret)
        return ret;

    indio_dev->info = &bh1749_info;
    indio_dev->name = "bh1749";
    indio_dev->channels = bh1749_channels;
    indio_dev->num_channels = ARRAY_SIZE(bh1749_channels);
    indio_dev->modes = INDIO_DIRECT_MODE;

    ret = devm_iio_device_register(dev, indio_dev);
    if (ret)
    {
        dev_err(dev, "failed to register iio device (%d)\n", ret);
        return ret;
    }

    dev_info(dev, "BH1749NUC registered (part id 0x%02x)\n", BH1749_PART_ID);

    return 0;
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
    .id_table = bh1749_id,
};

module_i2c_driver(bh1749_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("chan");
MODULE_DESCRIPTION("ROHM BH1749NUC colour sensor driver");
