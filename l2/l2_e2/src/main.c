/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

/* STEP 9 - Increase the sleep time from 100ms to 10 minutes  */
#define SLEEP_TIME_MS 100

/* BUTTON1 is the devicetree node identifier for the "sw0" alias */
#define BUTTON1 DT_ALIAS(sw0)
static const struct gpio_dt_spec button1 = GPIO_DT_SPEC_GET(BUTTON1, gpios);

/* LED0_NODE is the devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* STEP 4 - Define the callback function */
void button1_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	/* STEP 5 - Toggle the LED */
	gpio_pin_toggle_dt(&led);
}

/* STEP 5 - Define a variable of type static struct gpio_callback */
static struct gpio_callback button_cb_data;

int main(void)
{
	int ret;

	if (!device_is_ready(led.port)) {
		return -1;
	}

	if (!device_is_ready(button1.port)) {
		return -1;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return -1;
	}

	ret = gpio_pin_configure_dt(&button1, GPIO_INPUT);
	if (ret < 0) {
		return -1;
	}
	
	/* STEP 3 - Configure the interrupt on the button's pin */
	ret = gpio_pin_interrupt_configure_dt(&button1, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		return -1;
	}

	/* STEP 6 - Initialize the static struct gpio_callback variable   */
	gpio_init_callback(&button_cb_data, button1_callback, BIT(button1.pin));

	/* STEP 7 - Add the callback function by calling gpio_add_callback()   */
	gpio_add_callback(button1.port, &button_cb_data);

	while (1) {
		/* STEP 8 - Remove the polling code */
		//bool val = gpio_pin_get_dt(&button);
		//gpio_pin_set_dt(&led, val);

		k_msleep(SLEEP_TIME_MS);
	}
}