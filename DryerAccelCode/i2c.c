/*
 *  Some of the code in this file was copied from ST Micro.  Below is their required information.
 *
 * @attention
 *
 * <h2><center>&copy; COPYRIGHT(c) 2018 STMicroelectronics</center></h2>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the name of STMicroelectronics nor the names of its
 *      contributors may be used to endorse or promote products derived from
 *      this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

// applibs_versions.h defines the API struct versions to use for applibs APIs.
#include "applibs_versions.h"

#include <applibs/log.h>
#include <applibs/i2c.h>

#include <hw/sample_appliance.h>
#include "deviceTwin.h"
#include "azure_iot_utilities.h"
#include "build_options.h"
#include "i2c.h"
#include "lsm6dso_reg.h"

// mqtt
#include "mqtt_utilities.h"

const char* MQTT_ADDRESS = "ece1894.eastus.cloudapp.azure.com";
const char* MQTT_TOPIC = "DryerTelemetry";
int mqtt_message_counter = -1; // counts mqtt sequence

/* Private variables ---------------------------------------------------------*/
static axis3bit16_t data_raw_acceleration;
static axis3bit16_t data_raw_angular_rate;
static axis3bit16_t raw_angular_rate_calibration;
static float acceleration_mg[3];
static float angular_rate_dps[3];

static uint8_t whoamI, rst;
int accelTimerFd;
const uint8_t lsm6dsOAddress = LSM6DSO_ADDRESS;     // Addr = 0x6A
lsm6dso_ctx_t dev_ctx;

float altitude;

// Status variables
uint8_t lsm6dso_status = 1;
uint8_t RTCore_status = 1;

//Extern variables
int i2cFd = -1;
extern int epollFd;
extern volatile sig_atomic_t terminationRequired;

//Private functions

// Routines to read/write to the LSM6DSO device
static int32_t platform_write(int *fD, uint8_t reg, uint8_t *bufp, uint16_t len);
static int32_t platform_read(int *fD, uint8_t reg, uint8_t *bufp, uint16_t len);

/// <summary>
///     Sleep for delayTime ms
/// </summary>
void HAL_Delay(int delayTime) {
	struct timespec ts;
	ts.tv_sec = 0;
	ts.tv_nsec = delayTime * 10000;
	nanosleep(&ts, NULL);
}

/// <summary>
///     Print latest data from on-board sensors.
/// </summary>
void AccelTimerEventHandler(EventData *eventData)
{
	uint8_t reg;

#ifdef IOT_HUB_APPLICATION
	static bool firstPass = true;
#endif
	// Consume the event.  If we don't do this we'll come right back 
	// to process the same event again
	if (ConsumeTimerFdEvent(accelTimerFd) != 0) {
		terminationRequired = true;
		return;
	}

	// Read the sensors on the lsm6dso device

	//Read output only if new xl value is available
	lsm6dso_xl_flag_data_ready_get(&dev_ctx, &reg);
	if (reg)
	{
		// Read acceleration field data
		memset(data_raw_acceleration.u8bit, 0x00, 3 * sizeof(int16_t));
		lsm6dso_acceleration_raw_get(&dev_ctx, data_raw_acceleration.u8bit);

		acceleration_mg[0] = lsm6dso_from_fs4_to_mg(data_raw_acceleration.i16bit[0]);
		acceleration_mg[1] = lsm6dso_from_fs4_to_mg(data_raw_acceleration.i16bit[1]);
		acceleration_mg[2] = lsm6dso_from_fs4_to_mg(data_raw_acceleration.i16bit[2]);

		Log_Debug("\nLSM6DSO: Acceleration [mg]  : %.4lf, %.4lf, %.4lf\n",
			acceleration_mg[0], acceleration_mg[1], acceleration_mg[2]);
	}

	lsm6dso_gy_flag_data_ready_get(&dev_ctx, &reg);
	if (reg)
	{
		// Read angular rate field data
		memset(data_raw_angular_rate.u8bit, 0x00, 3 * sizeof(int16_t));
		lsm6dso_angular_rate_raw_get(&dev_ctx, data_raw_angular_rate.u8bit);

		// Before we store the mdps values subtract the calibration data we captured at startup.
		angular_rate_dps[0] = (lsm6dso_from_fs2000_to_mdps(data_raw_angular_rate.i16bit[0] - raw_angular_rate_calibration.i16bit[0])) / 1000.0;
		angular_rate_dps[1] = (lsm6dso_from_fs2000_to_mdps(data_raw_angular_rate.i16bit[1] - raw_angular_rate_calibration.i16bit[1])) / 1000.0;
		angular_rate_dps[2] = (lsm6dso_from_fs2000_to_mdps(data_raw_angular_rate.i16bit[2] - raw_angular_rate_calibration.i16bit[2])) / 1000.0;

		

		Log_Debug("LSM6DSO: Angular rate [dps] : %4.2f, %4.2f, %4.2f\r\n",
			angular_rate_dps[0], angular_rate_dps[1], angular_rate_dps[2]);

	}
	
	// 6 * (7 characters of float representation + period + comma separator) + sequence number (10 digits in max int so 10 characters)
	int mqtt_message_size = 6*(9*sizeof(char)) + 10*sizeof(char); 

	char *mqtt_message_string = (char*)malloc(mqtt_message_size);
	sprintf(mqtt_message_string, "%d,%f,%f,%f,%f,%f,%f", mqtt_message_counter,
		acceleration_mg[0], 
		acceleration_mg[1], 
		acceleration_mg[2],
		angular_rate_dps[0],
		angular_rate_dps[1],
		angular_rate_dps[2]
	);

	// send message
	Log_Debug(mqtt_message_string);
	MQTTPublish(MQTT_TOPIC, mqtt_message_string);

// The ALTITUDE value calculated is actually "Pressure Altitude". This lacks correction for temperature (and humidity)
// "pressure altitude" calculator located at: https://www.weather.gov/epz/wxcalc_pressurealtitude
// "pressure altitude" formula is defined at: https://www.weather.gov/media/epz/wxcalc/pressureAltitude.pdf
// altitude in feet = 145366.45 * (1 - (hPa / 1013.25) ^ 0.190284) feet
// altitude in meters = 145366.45 * 0.3048 * (1 - (hPa / 1013.25) ^ 0.190284) meters
//
// weather.com formula
//altitude = 44307.69396 * (1 - powf((atm / 1013.25), 0.190284));  // pressure altitude in meters
// Bosch's formula
	//altitude = 44330 * (1 - powf((pressure_hPa / 1013.25), 1 / 5.255));  // pressure altitude in meters

#ifdef M0_INTERCORE_COMMS
	Log_Debug("ALSPT19: Ambient Light[Lux] : %.2f\r\n", light_sensor);
#endif 

// #ifdef OLED_SD1306
// 	//// OLED
// 	update_oled();
// #endif 
#ifdef IOT_HUB_APPLICATION

		// We've seen that the first read of the Accelerometer data is garbage.  If this is the first pass
		// reading data, don't report it to Azure.  Since we're graphing data in Azure, this data point
		// will skew the data.
		if (!firstPass) {

			// Allocate memory for a telemetry message to Azure
			char *pjsonBuffer = (char *)malloc(JSON_BUFFER_SIZE);
			if (pjsonBuffer == NULL) {
				Log_Debug("ERROR: not enough memory to send telemetry");
			}

			snprintf(pjsonBuffer, JSON_BUFFER_SIZE, "{\"gX\":\"%.2lf\", \"gY\":\"%.2lf\", \"gZ\":\"%.2lf\", \"aX\": \"%.2f\", \"aY\": \"%.2f\", \"aZ\": \"%.2f\", \"pressure\": \"%.2f\", \"light_intensity\": \"%.2f\", \"altitude\": \"%.2f\", \"temp\": \"%.2f\",  \"rssi\": \"%d\"}",
				acceleration_mg[0], acceleration_mg[1], acceleration_mg[2], angular_rate_dps[0], angular_rate_dps[1], angular_rate_dps[2]);//, pressure_hPa, light_sensor, altitude, lsm6dsoTemperature_degC, network_data.rssi);

			Log_Debug("\n[Info] Sending telemetry: %s\n", pjsonBuffer);
			AzureIoT_SendMessage(pjsonBuffer);
			free(pjsonBuffer);

		}

		firstPass = false;

#endif 

}

int initMQTTI2c(void) {
	MQTTInit(MQTT_ADDRESS, "1833", MQTT_TOPIC);
}

/// <summary>
///     Initializes the I2C interface.
/// </summary>
/// <returns>0 on success, or -1 on failure</returns>
int initI2c(void) {

	//initMQTTI2c(); // start mqtt connection
	// Begin MT3620 I2C init 

	i2cFd = I2CMaster_Open(AVNET_MT3620_SK_ISU2_I2C);
	if (i2cFd < 0) {
		Log_Debug("ERROR: I2CMaster_Open: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	int result = I2CMaster_SetBusSpeed(i2cFd, I2C_BUS_SPEED_STANDARD);
	if (result != 0) {
		Log_Debug("ERROR: I2CMaster_SetBusSpeed: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	result = I2CMaster_SetTimeout(i2cFd, 100);
	if (result != 0) {
		Log_Debug("ERROR: I2CMaster_SetTimeout: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

#ifdef OLED_SD1306
	// Start OLED
	if (oled_init())
	{
		Log_Debug("OLED not found!\n");
	}
	else
	{
		Log_Debug("OLED found!\n");
	}

	// Draw AVNET logo
	//oled_draw_logo();
	oled_i2c_bus_status(CLEAR_BUFFER);
#endif

	// Start lsm6dso specific init

	// Initialize lsm6dso mems driver interface
	dev_ctx.write_reg = platform_write;
	dev_ctx.read_reg = platform_read;
	dev_ctx.handle = &i2cFd;

	// Check device ID
	HAL_Delay(5000); //pf

	// Check device ID
	lsm6dso_device_id_get(&dev_ctx, &whoamI);
	if (whoamI != LSM6DSO_ID) {
		Log_Debug("LSM6DSO not found!\n");
#ifdef OLED_SD1306
		// OLED update
		lsm6dso_status = 1;
		oled_i2c_bus_status(LSM6DSO_STATUS_DISPLAY);
#endif
		return -1;
	}
	else {
		Log_Debug("LSM6DSO Found!\n");

#ifdef OLED_SD1306
		// OLED update
		lsm6dso_status = 0;
		oled_i2c_bus_status(LSM6DSO_STATUS_DISPLAY);
#endif
	}
		
	 // Restore default configuration
	lsm6dso_reset_set(&dev_ctx, PROPERTY_ENABLE);
	do {
		lsm6dso_reset_get(&dev_ctx, &rst);
	} while (rst);

	 // Disable I3C interface
	lsm6dso_i3c_disable_set(&dev_ctx, LSM6DSO_I3C_DISABLE);

	// Enable Block Data Update
	lsm6dso_block_data_update_set(&dev_ctx, PROPERTY_ENABLE);

	 // Set Output Data Rate
	lsm6dso_xl_data_rate_set(&dev_ctx, LSM6DSO_XL_ODR_12Hz5);
	lsm6dso_gy_data_rate_set(&dev_ctx, LSM6DSO_GY_ODR_12Hz5);

	 // Set full scale
	lsm6dso_xl_full_scale_set(&dev_ctx, LSM6DSO_4g);
	lsm6dso_gy_full_scale_set(&dev_ctx, LSM6DSO_2000dps);

	 // Configure filtering chain(No aux interface)
	// Accelerometer - LPF1 + LPF2 path	
	lsm6dso_xl_hp_path_on_out_set(&dev_ctx, LSM6DSO_LP_ODR_DIV_100);
	lsm6dso_xl_filter_lp2_set(&dev_ctx, PROPERTY_ENABLE);

	// Read the raw angular rate data from the device to use as offsets.  We're making the assumption that the device
	// is stationary.

	uint8_t reg;
	
	Log_Debug("LSM6DSO: Calibrating angular rate . . .\n"); 
	Log_Debug("LSM6DSO: Please make sure the device is stationary.\n");

	do {
		// Read the calibration values
		lsm6dso_gy_flag_data_ready_get(&dev_ctx, &reg);
		if (reg)
		{
			// Read angular rate field data to use for calibration offsets
			memset(data_raw_angular_rate.u8bit, 0x00, 3 * sizeof(int16_t));
			lsm6dso_angular_rate_raw_get(&dev_ctx, raw_angular_rate_calibration.u8bit);
		}

		// Read the angular data rate again and verify that after applying the calibration, we have 0 angular rate in all directions

		lsm6dso_gy_flag_data_ready_get(&dev_ctx, &reg);
		if (reg)
		{
			// Read angular rate field data
			memset(data_raw_angular_rate.u8bit, 0x00, 3 * sizeof(int16_t));
			lsm6dso_angular_rate_raw_get(&dev_ctx, data_raw_angular_rate.u8bit);

			// Before we store the mdps values subtract the calibration data we captured at startup.
			angular_rate_dps[0] = lsm6dso_from_fs2000_to_mdps(data_raw_angular_rate.i16bit[0] - raw_angular_rate_calibration.i16bit[0]);
			angular_rate_dps[1] = lsm6dso_from_fs2000_to_mdps(data_raw_angular_rate.i16bit[1] - raw_angular_rate_calibration.i16bit[1]);
			angular_rate_dps[2] = lsm6dso_from_fs2000_to_mdps(data_raw_angular_rate.i16bit[2] - raw_angular_rate_calibration.i16bit[2]);
		}
		
		// If the angular values after applying the offset are not 0.7 or less, then do it again!
	} while ((angular_rate_dps[0] >= 0.7f) && (angular_rate_dps[1] >= 0.7f) && (angular_rate_dps[2] >= 0.7f));

	Log_Debug("LSM6DSO: Calibrating angular rate complete!\n");


	// Init the epoll interface to periodically run the AccelTimerEventHandler routine where we read the sensors

	// Define the period in the build_options.h file
	struct timespec accelReadPeriod = { .tv_sec = ACCEL_READ_PERIOD_SECONDS,.tv_nsec = ACCEL_READ_PERIOD_NANO_SECONDS };
	// event handler data structures. Only the event handler field needs to be populated.
	static EventData accelEventData = { .eventHandler = &AccelTimerEventHandler };
	accelTimerFd = -1;
	accelTimerFd = CreateTimerFdAndAddToEpoll(epollFd, &accelReadPeriod, &accelEventData, EPOLLIN);
	if (accelTimerFd < 0) {
		return -1;
	}
	
	MQTTPublish(MQTT_TOPIC, "-1,-1,-1,-1,-1,-1,-1");

	return 0;
}

/// <summary>
///     Closes the I2C interface File Descriptors.
/// </summary>
void closeI2c(void) {
	MQTTStop();
	CloseFdAndPrintError(i2cFd, "i2c");
	CloseFdAndPrintError(accelTimerFd, "accelTimer");
}

/// <summary>
///     Writes data to the lsm6dso i2c device
/// </summary>
/// <returns>0</returns>

static int32_t platform_write(int *fD, uint8_t reg, uint8_t *bufp,
	uint16_t len)
{

#ifdef ENABLE_READ_WRITE_DEBUG
	Log_Debug("platform_write()\n");
	Log_Debug("reg: %0x\n", reg);
	Log_Debug("len: %0x\n", len);
	Log_Debug("bufp contents: ");
	for (int i = 0; i < len; i++) {

		Log_Debug("%0x: ", bufp[i]);
	}
	Log_Debug("\n");
#endif 

	// Construct a new command buffer that contains the register to write to, then the data to write
	uint8_t cmdBuffer[len + 1];
	cmdBuffer[0] = reg;
	for (int i = 0; i < len; i++) {
		cmdBuffer[i + 1] = bufp[i];
	}

#ifdef ENABLE_READ_WRITE_DEBUG
	Log_Debug("cmdBuffer contents: ");
	for (int i = 0; i < len + 1; i++) {

		Log_Debug("%0x: ", cmdBuffer[i]);
	}
	Log_Debug("\n");
#endif

	// Write the data to the device
	int32_t retVal = I2CMaster_Write(*fD, lsm6dsOAddress, cmdBuffer, (size_t)len + 1);
	if (retVal < 0) {
		Log_Debug("ERROR: platform_write: errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}
#ifdef ENABLE_READ_WRITE_DEBUG
	Log_Debug("Wrote %d bytes to device.\n\n", retVal);
#endif
	return 0;
}

/// <summary>
///     Reads generic device register from the i2c interface
/// </summary>
/// <returns>0</returns>

/*
 * @brief  Read generic device register (platform dependent)
 *
 * @param  handle    customizable argument. In this examples is used in
 *                   order to select the correct sensor bus handler.
 * @param  reg       register to read
 * @param  bufp      pointer to buffer that store the data read
 * @param  len       number of consecutive register to read
 *
 */
static int32_t platform_read(int *fD, uint8_t reg, uint8_t *bufp,
	uint16_t len)
{

#ifdef ENABLE_READ_WRITE_DEBUG
	Log_Debug("platform_read()\n");
	Log_Debug("reg: %0x\n", reg);
	Log_Debug("len: %d\n", len);
;
#endif

	// Set the register address to read
	int32_t retVal = I2CMaster_Write(i2cFd, lsm6dsOAddress, &reg, 1);
	if (retVal < 0) {
		Log_Debug("ERROR: platform_read(write step): errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

	// Read the data into the provided buffer
	retVal = I2CMaster_Read(i2cFd, lsm6dsOAddress, bufp, len);
	if (retVal < 0) {
		Log_Debug("ERROR: platform_read(read step): errno=%d (%s)\n", errno, strerror(errno));
		return -1;
	}

#ifdef ENABLE_READ_WRITE_DEBUG
	Log_Debug("Read returned: ");
	for (int i = 0; i < len; i++) {
		Log_Debug("%0x: ", bufp[i]);
	}
	Log_Debug("\n\n");
#endif 	   

	return 0;
}