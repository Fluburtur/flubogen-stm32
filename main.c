/*
 *
 * Cleanflight is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This file is part of Cleanflight.
 *
 * Cleanflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Cleanflight.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "platform.h"
#include "build/debug.h"
#include "drivers/serial.h"
#include "drivers/serial_uart.h"
#include "drivers/serial_usb_vcp.h"

#include "fc/config.h"

// LOL
#include "drivers/adc.h"
#include "drivers/time.h"
#include "drivers/timer.h"
#include "drivers/system.h"
#include "drivers/io.h"
#include "drivers/bus.h"
#include "drivers/light_led.h"
#include "drivers/light_ws2811strip.h"
#include "drivers/sdcard/sdcard.h"
// OSD driver (platform dependant)
#include "drivers/max7456.h"

#include "sensors/battery.h"

#include "io/serial.h"
#include "io/asyncfatfs/asyncfatfs.h"

#include "common/log.h"
#include "common/crc.h"

#define FILE_NAME "file.fur"
#define RANDOM_ANIMATION_PERIOD 10000  // in ms
#define INITIAL_BRIGHTNESS 50
#define MAX_BRIGHTNESS 150  // absolute maximum is 255
#define MIN_BRIGHTNESS 10
#define BRIGHTNESS_STEP (MAX_BRIGHTNESS*5)/100
// ADC values for automatic brightness adjustment (GL5528 photoresistor + 10k pullldown)
const uint16_t lightValues[] = {1300, 2300, 3100, 3500, 4095};
const uint8_t brightnessValues[] = {MIN_BRIGHTNESS, 20, INITIAL_BRIGHTNESS, 100, MAX_BRIGHTNESS};
#define MAX_GAMMA (double) 1.9
#define MIN_GAMMA (double) 1.2

enum {
	NO_ANIMATION,
	DEFAULT_ANIMATION,
	RANDOM_ANIMATION_1,
	RANDOM_ANIMATION_2,
	RANDOM_ANIMATION_3,
	BOOT_ANIMATION
} animationType_e;

const char *ANIMATION_NAME[] = {
	"NO ANIMATION",
	"DEFAULT ANIMATION",
	"RANDOM ANIMATION 1",
	"RANDOM ANIMATION 2",
	"RANDOM ANIMATION 3",
	"BOOT ANIMATION"
};

enum {
	SET_ANIMATION,
	LOCK_ANIMATION,
	CHANGE_BRIGHTNESS
} commandType_e;

enum {
	DECREASE_BRIGHTNESS,
	INCREASE_BRIGHTNESS,
	RESET_BRIGHTNESS
} brightnessChange_e;

static volatile uint8_t currentAnimation = BOOT_ANIMATION,
		brightness = INITIAL_BRIGHTNESS;
static volatile bool animationLocked = false;
uint8_t animationCount = 0,
		lastAnimation,
		lastLightInterval;
uint8_t gammaCorrection[256];
int16_t brightnessAdjustment = 0;
int32_t *animationOffset;
timeMs_t lastAnimationChange,
		 lastBrightnessChange;
timeUs_t lastBatteryUpdate = 0;
rgbColor24bpp_t matrix[WS2811_LED_STRIP_LENGTH];
serialPort_t *serial2;

bool hardwareInit(void);
void updateOSD(uint8_t animationNumber);
bool animationInit(void);
void drawAnimation(uint8_t animationIndex);
void checkBrightness(void);
void byteSwap(uint8_t *x);
uint8_t indexBitWidth(const uint16_t colorTableLength);
uint32_t byteLength(const uint32_t N, const uint8_t bitWidth);
uint8_t frameBitOffset(const uint8_t bitWidth, const uint32_t pixelsPerFame, const uint32_t frame);
void updateGammaCorrection(double brightness);
uint8_t lightInterval(uint16_t adcValue);
void initialiseAutoBrightness(void);
void changeAutoBrightness(void);
void afatfs_ftellSync(afatfsFilePtr_t file, uint32_t *position);

static afatfsFilePtr_t file = NULL;
static void fileOpenCallback(afatfsFilePtr_t openedFile) {
	file = openedFile;
}

static volatile bool receivingCommand = false,
				changeBrightness = false,
				resetBrightness = false;
static volatile uint8_t rxData;
static void serialRxCallback(uint16_t data, void *rxCallbackData)
{
	UNUSED(rxCallbackData);

	if (receivingCommand) {
		switch (rxData) {
			case SET_ANIMATION:
				if (data == 0)
					currentAnimation = DEFAULT_ANIMATION;
				else
					currentAnimation = BOOT_ANIMATION + data;
				animationLocked = false;
				break;

			case LOCK_ANIMATION:
				if (data == 0)
					currentAnimation = DEFAULT_ANIMATION;
				else
					currentAnimation = BOOT_ANIMATION + data;
				animationLocked = true;
				break;

			case CHANGE_BRIGHTNESS:
				switch (data) {
					case DECREASE_BRIGHTNESS:
						changeBrightness = true;
						if (brightnessAdjustment - BRIGHTNESS_STEP > -MAX_BRIGHTNESS)
							brightnessAdjustment -= BRIGHTNESS_STEP;
						else
							brightnessAdjustment = -MAX_BRIGHTNESS;
						break;

					case INCREASE_BRIGHTNESS:
						changeBrightness = true;
						if (brightnessAdjustment + BRIGHTNESS_STEP < MAX_BRIGHTNESS)
							brightnessAdjustment += BRIGHTNESS_STEP;
						else
							brightnessAdjustment = MAX_BRIGHTNESS;
						break;

					case RESET_BRIGHTNESS:
						resetBrightness = true;
						break;
				}
				break;
		}
		receivingCommand = false;
	} else {
		rxData = (uint8_t)data;
		receivingCommand = true;
	}
}

int main(void)
{
	if (!hardwareInit()) {
		//LOG_E(SYSTEM, "Harware initialisation failed");
		return 1;
	}

	if (!animationInit()) {
		//LOG_E(SYSTEM, "Animation initialisation failed");
		return 1;
	}

	initialiseAutoBrightness();
	//LOG_E(SYSTEM, "Brightness initialised to %d", brightness);
	updateGammaCorrection(brightness);

	// intialise random number generator
	srand((unsigned int) micros());

	updateOSD(currentAnimation);

	while (true) {
		// what is this delay here for???
		//delay(100);
		lastAnimation = currentAnimation;
		drawAnimation(currentAnimation);

		// animation can get changed in serialRxCallback
		if (lastAnimation == currentAnimation) {
			currentAnimation = DEFAULT_ANIMATION;

			if (millis() - lastAnimationChange > RANDOM_ANIMATION_PERIOD) {
				switch (rand() % 12) {
					case 8:
					case 9:
						currentAnimation = RANDOM_ANIMATION_1;
						break;

					case 10:
						currentAnimation = RANDOM_ANIMATION_2;
						break;

					case 11:
						currentAnimation = RANDOM_ANIMATION_3;
						break;
				}
				lastAnimationChange = millis();
			}
		}
	}
}

bool hardwareInit(void)
{
	systemInit();
	__enable_irq();
	IOInitGlobal();
	busInit();
	ledInit(false);
	usbVcpInitHardware();
	timerInit();
	//serialInit(true, SERIAL_PORT_NONE);
    logInit();
	ws2811LedStripInit();
	sdcardInsertionDetectInit();
	// 0 - AUTO, 1 - PAL, 2 - NTSC
	max7456Init(0);
	max7456ClearScreen();
	max7456RefreshAll();
	sdcard_init();
	afatfs_init();
	initEEPROM();
	ensureEEPROMContainsValidData();
	readEEPROM();

	drv_adc_config_t adc_params;
	memset(&adc_params, 0, sizeof(adc_params));
	// add RSSI to ADC init (used for automatic brightness adjustment with photoresistor)
	adc_params.adcFunctionChannel[ADC_RSSI] = RSSI_ADC_CHANNEL;
	// add ADCs for measuring voltage and current of the battery
	adc_params.adcFunctionChannel[ADC_BATTERY] = VBAT_ADC_CHANNEL;
	adc_params.adcFunctionChannel[ADC_CURRENT] = CURRENT_METER_ADC_CHANNEL;
	adcInit(&adc_params);

	// initialise battery measurements
	batteryInit();
	//setBatteryProfile(1);
	batteryUpdate(0);
	currentMeterUpdate(0);
	powerMeterUpdate(0);

	afatfsError_e sdError = afatfs_getLastError();
	afatfsFilesystemState_e fatState = afatfs_getFilesystemState();
	LED0_ON;
	//LOG_E(SYSTEM, "Initialising SD card");
	// try to inicialize the SD card
	while ((fatState != AFATFS_FILESYSTEM_STATE_READY) && (sdError == AFATFS_ERROR_NONE)) {
		afatfs_poll();
		sdError = afatfs_getLastError();
		fatState = afatfs_getFilesystemState();
	}
	LED0_OFF;
	if (sdError == AFATFS_ERROR_NONE) {
		//LOG_E(SYSTEM, "SD card initialised");
	} else {
		//LOG_E(SYSTEM, "SD card failed to initialise");
		//LOG_E(SYSTEM, "Last SD error: %d", sdError);
		return false;
	}

	serial2 = (serialPort_t *) uartOpen(USART2, serialRxCallback, NULL, baudRates[BAUD_115200], MODE_RXTX, 0);

	return true;
}

void updateOSD(uint8_t animationNumber)
{
	//LOG_E(SYSTEM, "ADC_CURRENT: %d", adcGetChannel(ADC_CURRENT));
	timeUs_t newBatteryUpdate = micros();
	timeUs_t delta = newBatteryUpdate - lastBatteryUpdate;
	// update battery measurements
	batteryUpdate(delta);
	currentMeterUpdate(delta);
	powerMeterUpdate(delta);
	lastBatteryUpdate = newBatteryUpdate;

	//LOG_E(SYSTEM, "VBAT ADC: %d", adcGetChannel(ADC_BATTERY));
	uint16_t voltage = getBatteryVoltage();
	uint32_t power = getPower();
	uint32_t mAh = getMAhDrawn();
	uint32_t time = millis()/1000;
	uint8_t seconds = time%60;
	time /= 60;
	uint8_t minutes = time%60;
	time /= 60;
	// for some reason the OSD can only display 28 characters on one line instead of the 30 that is specified in datasheet
	char lineBuffer[(MAX7456_CHARS_PER_LINE - 2) + 1];
	uint8_t bufLen = sprintf(lineBuffer, "%d.%02dV %ld.%01ldW %ld%c",
			voltage/100, voltage%100, power/100, (power%100)/10, mAh, 0x07
	);

	for (uint8_t i = bufLen; i < sizeof(lineBuffer); i++)
		lineBuffer[i] = ' ';
	lineBuffer[sizeof(lineBuffer) - 1] = '\0';

	sprintf(&lineBuffer[sizeof(lineBuffer) - 10], "%02ld:%02d:%02d%c",
			time, minutes, seconds, 0x70
	);

	max7456Write(1, MAX7456_LINES_NTSC - 1, lineBuffer, 0);
	// clear line buffer
	memset(&lineBuffer, 0, sizeof(lineBuffer));

	bool locked = animationLocked && animationNumber == currentAnimation;
	if (animationNumber > BOOT_ANIMATION) {
		bufLen = sprintf(lineBuffer, "CUSTOM ANIMATION %d%s",
				animationNumber - BOOT_ANIMATION, locked ? " LOCKED" : "");
	} else {
		bufLen = sprintf(lineBuffer, "%s%s", ANIMATION_NAME[animationNumber], locked ? " LOCKED" : "");
	}

	for (uint8_t i = bufLen; i < sizeof(lineBuffer); i++)
		lineBuffer[i] = ' ';
	lineBuffer[sizeof(lineBuffer) - 1] = '\0';

	max7456Write(1, MAX7456_LINES_NTSC - 2, lineBuffer, 0);
	max7456Update();
}

bool animationInit(void)
{
	bool fileOpened = afatfs_fopen(FILE_NAME, "r", fileOpenCallback);
	//LOG_E(SYSTEM, "File opened: %d", fileOpened);
	//LOG_E(SYSTEM, "File is null: %d", file == NULL);
	//LOG_E(SYSTEM, "File size: %ld", afatfs_fileSize(file));

	if (!fileOpened || file == NULL)
		return false;

	afatfs_freadSync(file, &animationCount, sizeof(uint8_t));
	//LOG_E(SYSTEM, "Number of animations: %d", animationCount);

	animationOffset = malloc(animationCount*sizeof(int32_t));
	if (animationOffset == NULL)
		return false;

	int32_t offset = 1;

	for (uint8_t i = 0; i < animationCount; i++) {
		animationOffset[i] = offset;
		afatfs_fseekSync(file, offset, AFATFS_SEEK_SET);
		//LOG_E(SYSTEM, "Animation %d has offset %ld", i, animationOffset[i]);

		uint8_t fps;
		afatfs_freadSync(file, &fps, sizeof(uint8_t));

		if (fps >> 7) {  // MSB of FPS indicates compressed animation
			uint16_t colorCount = 0;
			// number of colors is saved as 1 byte wide index of the last color
			afatfs_freadSync(file, (uint8_t *)&colorCount, sizeof(uint8_t));
			colorCount++;
			//LOG_E(SYSTEM, "Animation %d has %d colors", i, colorCount);
			afatfs_fseekSync(file, offset + 2 + 3*colorCount, AFATFS_SEEK_SET);

			// number of frames in the animation
			uint16_t frameCount;
			afatfs_freadSync(file, (uint8_t *)&frameCount, sizeof(uint16_t));
			// swap bytes because high byte is first
			byteSwap((uint8_t *)&frameCount);
			//LOG_E(SYSTEM, "Animation %d has %d frames", i, frameCount);

			offset += 4 + 3*colorCount;
			offset += byteLength(frameCount*WS2811_LED_STRIP_LENGTH, indexBitWidth(colorCount));
		} else {
			// number of frames in the animation
			uint16_t frameCount;
			afatfs_freadSync(file, (uint8_t *)&frameCount, sizeof(uint16_t));
			// swap bytes because high byte is first
			byteSwap((uint8_t *)&frameCount);
			offset += 3 + 3*frameCount*WS2811_LED_STRIP_LENGTH;
		}
	}

	return true;
}

void drawAnimation(uint8_t animationNumber)
{
	if (animationNumber == NO_ANIMATION) {
		rgbColor24bpp_t color;
		color.rgb.r = 0;
		color.rgb.g = 0;
		color.rgb.b = 0;
		setStripColorRgb(color);
		return;
	}

	if (animationNumber > animationCount)
		return;

	animationNumber--;
	afatfs_fseekSync(file, animationOffset[animationNumber], AFATFS_SEEK_SET);
	//LOG_E(SYSTEM, "Playing animation %d from offset %ld", animationNumber, animationOffset[animationNumber]);

	uint8_t animationFps;
	afatfs_freadSync(file, &animationFps, sizeof(uint8_t));

	if (animationFps >> 7) {  // MSB indicates compressed animation
		timeMs_t refreshDelay = 1000 / (animationFps - 128);  // delay between frames
		uint16_t colorCount = 0;
		afatfs_freadSync(file, (uint8_t *)&colorCount, sizeof(uint8_t));
		colorCount++;
		uint8_t bitWidth = indexBitWidth(colorCount);
		uint8_t indexMask = (1 << bitWidth) - 1;
		uint32_t frameLength = byteLength(WS2811_LED_STRIP_LENGTH, bitWidth);
		rgbColor24bpp_t colorTable[colorCount];

		uint8_t colorTableData[3*colorCount];
		afatfs_freadSync(file, colorTableData, 3*colorCount);
		//LOG_E(SYSTEM, "CRC of color table for animation %d is %d", animationNumber, crc16_ccitt_update(0, colorTableData, 3*colorCount));

		for (uint16_t i = 0; i < colorCount; i++) {
			for (uint8_t j = 0; j < 3; j++)
					colorTable[i].raw[j] = gammaCorrection[colorTableData[3*i + j]];
		}

		uint16_t frameCount;
		afatfs_freadSync(file, (uint8_t *)&frameCount, sizeof(uint16_t));
		byteSwap((uint8_t *)&frameCount);

		uint32_t animationFramesOffset;
		afatfs_ftellSync(file, &animationFramesOffset);

		for (uint16_t i = 0; i < frameCount;) {
			timeMs_t frameStart = millis();

			checkBrightness();
			updateOSD(animationNumber + 1);

			uint8_t colorIndexData[frameLength];
			afatfs_freadSync(file, colorIndexData, frameLength);
			//LOG_E(SYSTEM, "CRC of frame %d in animation %d is %d", i, animationNumber, crc16_ccitt_update(0, colorIndexData, frameLength));

			uint16_t ledIndex = 0;
			for (uint16_t j = 0; j < frameLength; j++) {
				for (uint8_t k = 0; k < 8/bitWidth; k++) {
					uint8_t colorIndex = 0;
					if (j == 0 && k == 0) {
						uint8_t bitOffset = frameBitOffset(bitWidth, WS2811_LED_STRIP_LENGTH, i);
						colorIndex = (colorIndexData[j] >> bitOffset) & indexMask;
						k = bitOffset/bitWidth;
					} else {
						colorIndex = (colorIndexData[j] >> (bitWidth * k)) & indexMask;
					}
					matrix[ledIndex] = colorTable[colorIndex];

					if (ledIndex < WS2811_LED_STRIP_LENGTH)
						ledIndex++;
					else
						break;
				}
			}

			//LOG_E(SYSTEM, "CRC of the frame buffer for frame %d in animation %d is %d", i, animationNumber, crc16_ccitt_update(0, (uint8_t *)&matrix, WS2811_LED_STRIP_LENGTH*3));
			// prepare file offset for next frame
			if (frameBitOffset(bitWidth, WS2811_LED_STRIP_LENGTH, i + 1) > 0) {
				uint32_t currentOffset = 0;
				afatfs_ftellSync(file, &currentOffset);
				afatfs_fseekSync(file, currentOffset - 1, AFATFS_SEEK_SET);
			}

			setStripColorsRgb(matrix);
			ws2811UpdateStrip();

			// seek to the start of frames if we have to repeat the animation
			if (i + 1 == frameCount && animationLocked && animationNumber + 1 == currentAnimation) {
				afatfs_fseekSync(file, animationFramesOffset, AFATFS_SEEK_SET);
				i = 0;
			} else {
				i++;  // advance frame
			}

			timeMs_t delta = millis() - frameStart;  // how long it took to draw the frame
			if (refreshDelay > delta) {
				delay(refreshDelay - delta);
			}
		}
	} else {
		timeMs_t refreshDelay = 1000 / animationFps;

		uint16_t frameCount;
		afatfs_freadSync(file, (uint8_t *)&frameCount, sizeof(uint8_t));
		byteSwap((uint8_t *)&frameCount);

		uint32_t animationFramesOffset;
		afatfs_ftellSync(file, &animationFramesOffset);

		for (uint16_t i = 0; i < frameCount;) {
			timeMs_t frameStart = millis();

			checkBrightness();
			updateOSD(animationNumber + 1);

			uint8_t colorData[3*WS2811_LED_STRIP_LENGTH];
			afatfs_freadSync(file, colorData, 3*WS2811_LED_STRIP_LENGTH);
			for (uint32_t j = 0; j < WS2811_LED_STRIP_LENGTH; j++) {
				for (uint8_t k = 0; k < 3; k++) {
					matrix[j].raw[k] = gammaCorrection[colorData[3*j + k]];
				}
			}

			setStripColorsRgb(matrix);
			ws2811UpdateStrip();

			// seek to the start of frames if we have to repeat the animation
			if (i + 1 == frameCount && animationLocked && animationNumber + 1 == currentAnimation) {
				afatfs_fseekSync(file, animationFramesOffset, AFATFS_SEEK_SET);
				i = 0;
			} else {
				i++;  // advance frame
			}

			timeMs_t delta = millis() - frameStart;
			if (refreshDelay > delta)
				delay(refreshDelay - delta);
		}
	}
}

void checkBrightness()
{
	//LOG_E(SYSTEM, "RSSI ADC: %d, brightness: %d", adcGetChannel(ADC_RSSI), brightness);
	changeAutoBrightness();

	if (resetBrightness) {
		initialiseAutoBrightness();
		brightnessAdjustment = 0;
		changeBrightness = true;
		resetBrightness = false;
	}

	if (changeBrightness) {
		//LOG_E(SYSTEM, "Updating brightness...");

		if (brightness + brightnessAdjustment > MAX_BRIGHTNESS)
			updateGammaCorrection(MAX_BRIGHTNESS);
		else if (brightness + brightnessAdjustment < MIN_BRIGHTNESS)
			updateGammaCorrection(MIN_BRIGHTNESS);
		else
			updateGammaCorrection(brightness + brightnessAdjustment);

		//LOG_E(SYSTEM, "Brightness changed to %d", brightness);
		changeBrightness = false;
	}
}

void byteSwap(uint8_t *x)
{
	x[0] ^= x[1];
	x[1] ^= x[0];
	x[0] ^= x[1];
}

uint8_t indexBitWidth(const uint16_t colorTableLength)
{
	if (colorTableLength < 3) {
		return 1;
	} else if (colorTableLength < 5) {
		return 2;
	} else if (colorTableLength < 17) {
		return 4;
	}

	return 8;
}

uint32_t byteLength(const uint32_t N, const uint8_t bitWidth)
{
	uint32_t m = N*bitWidth;
	uint32_t length = m/8;
	if (m & 7)
		length++;

	return length;
}

uint8_t frameBitOffset(const uint8_t bitWidth, const uint32_t pixelsPerFame, const uint32_t frame)
{
	return (bitWidth * pixelsPerFame * (frame&7))&7;
}

void updateGammaCorrection(double brightness)
{
	double gamma = MAX_GAMMA - (((double)255.0 - brightness)/(double)255.0) * (MAX_GAMMA - MIN_GAMMA);
	double a = brightness / pow(255, gamma);

	for (uint8_t i = 0;; i++) {
		gammaCorrection[i] = round(a * pow(i, gamma));

		if (i == 255)
			break;
	}
}

uint8_t lightInterval(uint16_t adcValue)
{
	for (uint8_t i = 0; i < sizeof(lightValues)/sizeof(uint16_t); i++) {
		if (adcValue < lightValues[i])
			return i;
	}

	return (uint8_t)(sizeof(lightValues)/sizeof(uint16_t)) - 1;
}

void initialiseAutoBrightness()
{
	uint16_t adcValue = adcGetChannel(ADC_RSSI);
	lastLightInterval = lightInterval(adcValue);
	brightness = brightnessValues[lastLightInterval];
}

void changeAutoBrightness()
{
	uint16_t adcValue = adcGetChannel(ADC_RSSI);
	uint8_t newLightInterval = lightInterval(adcValue);
	uint8_t newBrightness = brightnessValues[newLightInterval];

	if (newBrightness != brightness && newLightInterval != lastLightInterval) {
		brightness = newBrightness;
		lastLightInterval = newLightInterval;
		changeBrightness = true;
	}
}

void afatfs_ftellSync(afatfsFilePtr_t file, uint32_t *position)
{
	while(!afatfs_ftell(file, position))
			afatfs_poll();
}

