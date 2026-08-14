/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
/* STEP 3 - Include the header file of the I2C API */
#include <zephyr/drivers/i2c.h>

/* STEP 4.1 - Include the header file of printk() */
#include <zephyr/sys/printk.h>

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS 3000

/* STEP 8 - Define the I2C slave device address and the addresses of relevant registers */

#define CHIP_ID  0x60
#define SENSOR_CONFIG_VALUE 0x93

/* STEP 6 - Get the node identifier of the sensor */
#define I2C_NODE DT_NODELABEL(ht_sensor)



int hs4003_start_single_measuring(const struct i2c_dt_spec *dev_i2c);
int hs4003_read_measurement(const struct i2c_dt_spec *dev_i2c, float *humidityPercentRelative, float *temperatureCelcius);
uint8_t CalculateCrc8(const uint8_t* data, uint8_t dataLength);



int main(void)
{

	/* STEP 7 - Retrieve the API-specific device structure and make sure that the device is
	 * ready to use  */
	static const struct i2c_dt_spec htSensor = I2C_DT_SPEC_GET(I2C_NODE);
	if (!device_is_ready(htSensor.bus)) {
		printk("I2C bus %s is not ready!\n\r",htSensor.bus->name);
		return -1;
	}


	while (1) {

		/* STEP 12 - Start a single measurement */
		int ret = hs4003_start_single_measuring(&htSensor);
		if (ret != 0) {
			printk("Failed to read register!\n");
			//return -1;
		}

		k_msleep(20); // Wait for measurement to complete

		/* STEP 12 - Read the measurement from the sensor */
		float humidityPercentRelative, temperatureCelcius;

		ret = hs4003_read_measurement(&htSensor, &humidityPercentRelative, &temperatureCelcius);
		if (ret != 0) {
			printk("Failed to get measurement!\n");
			//return ret;
		}
		else {
			printk("Humidity: %.2f, Temperature: %.2f\n", humidityPercentRelative, temperatureCelcius);
		}
		

		k_msleep(SLEEP_TIME_MS);
	}
}






int hs4003_start_single_measuring(const struct i2c_dt_spec *dev_i2c)
{
    uint8_t cmd[1] = { 0xF5 };  // No-hold Humidity and Temperature Measurement

    int ret = i2c_write_dt(dev_i2c, cmd, sizeof(cmd));

    if (ret != 0) {
        printk("HS4003: I2C write failed, err %d\n", ret);
        return ret;
    }

    return 0;
}


int hs4003_read_measurement(const struct i2c_dt_spec *dev_i2c, float *humidityPercentRelative, float *temperatureCelcius)
{
    uint8_t data[5];  // 2 Byte Humidity + 2 Byte Temperatur (je nach Datenblatt ggf. + CRC-Bytes)

    // Kurz warten, bis Messung fertig ist (Wert laut HS4003-Datenblatt prüfen, z.B. ~20ms)
    //k_msleep(20);

    int ret = i2c_read_dt(dev_i2c, data, sizeof(data));
    if (ret != 0) {
        printk("HS4003: I2C read failed, err %d\n", ret);
        return ret;
    }

	if (CalculateCrc8(&data[0], 4) != data[4]) {
		printk("HS4003: CRC check failed!\n");
        return -1;
    }

    /* Formel siehe Datenblatt: Humidity[% RH] = HumidityData[13:0] / (2^14 - 1) * 100 */
    uint16_t raw_humidity = ((data[0] & 0x3F) << 8) | data[1];
    *humidityPercentRelative = ((float)raw_humidity * 100.0f) / 16383.0f;

    /* Formel siehe Datenblatt: Temperature[°C] = (TemperatureData[13:0] / (2^14 - 1) * 165 - 40) */
    uint16_t raw_temperature = ((data[2] & 0x3F) << 8) | data[3];
    *temperatureCelcius = ((float)raw_temperature * 165.0f / 16383.0f - 40.0f);

    return 0;
}



uint8_t CalculateCrc8(const uint8_t* data, uint8_t dataLength) {
    uint16_t g = 0x11d;
    uint16_t crc = 0xff;
    for (int i = 0; i < dataLength; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            crc <<= 1;
            if (crc & (1 << 8)) crc ^= g;
        }
    }
    return crc & 0xff;
}