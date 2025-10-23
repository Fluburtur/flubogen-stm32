// use internal pullup resistors of the arduino
// if this isn't true you'll need to connect external pulldown resistors
#define BUTTON_PULLUP false
//#define BUTTON_PULLUP true
// maximum amount of time (ms) for double click to get registred
#define MAX_DOUBLECLICK_TIME 500
// amount of time for which the button must be held in order to reset the brightness
#define BRIGHTNESS_RESET_TIME 1000

#if BUTTON_PULLUP
const uint8_t buttons[] = {A1, A0, 12, 11, 5, 6, 7, 8};
#else
const uint8_t buttons[] = {A0, 13, 12, 11, 5, 6, 7, 8};
#endif

const uint8_t brightnessButtons[] = {/*9, 10*/};

uint8_t lastButton = 0xFF,
		lastBrightnessButton = 0xFF,
		buttonClickCount = 0;
bool resetBrightness = false,
	 defaultAnimationLocked = false;
uint32_t lastButtonPressed,
		 lastBrightnessButtonPressed;

void setup() {
	Serial.begin(115200);
	for (uint8_t i = 0; i < sizeof(buttons); i++) {
#if BUTTON_PULLUP
		pinMode(buttons[i], INPUT_PULLUP);
#else
		pinMode(buttons[i], INPUT);
#endif
	}

	for (uint8_t i = 0; i < sizeof(brightnessButtons); i++) {
#if BUTTON_PULLUP
		pinMode(brightnessButtons[i], INPUT_PULLUP);
#else
		pinMode(brightnessButtons[i], INPUT);
#endif
	}

}


uint8_t data[2];
void loop() {
	for (uint8_t i = 0; i < sizeof(buttons); i++) {
		bool buttonPressed = BUTTON_PULLUP
			? !digitalRead(buttons[i])
			: digitalRead(buttons[i]);

		// button presed for the first time
		if (buttonPressed && buttons[i] != lastButton) {
			lastButton = buttons[i];
			lastButtonPressed = millis();
			buttonClickCount = 0;

			// don't read states of any other buttons
			break;
		} else if (buttons[i] == lastButton) {  // no other button was pressed

			if (millis() - lastButtonPressed > MAX_DOUBLECLICK_TIME) {
				if (buttonPressed) {
						// buttonClickCount is also used to indicated that the animation isn't already locked
					if (buttonClickCount == 0 && !defaultAnimationLocked) {
						// the button still wasn't released
						data[0] = 1;
						data[1] = i + 1;  // lock given animation
						Serial.write(data, sizeof(data));

						// increment buttonClickCount to indicate that we've already locked tha animation
						buttonClickCount++;
					}
				} else {
					// button was released
					if (!defaultAnimationLocked) {
						data[0] = 0;
						data[1] = i + 1;
						Serial.write(data, sizeof(data));
					}

					// reset last button
					lastButton = 0xFF;
				}
			} else if (!buttonPressed && buttonClickCount == 0) {
				// button was released for the first time
				buttonClickCount++;
			} else if (buttonPressed && buttonClickCount == 1) {
				// button was pressed second time within double-click period
				data[0] = defaultAnimationLocked ? 0 : 1;
				data[1] = 0;  // lock default animation
				Serial.write(data, sizeof(data));

				defaultAnimationLocked = !defaultAnimationLocked;

				// reset last button
				lastButton = 0xFF;

				// double-click debounce
				delay(150);

				break;
			}
		}
	}

	for (uint8_t i = 0; i < sizeof(brightnessButtons); i++) {
		bool buttonPressed = BUTTON_PULLUP
			? !digitalRead(brightnessButtons[i])
			: digitalRead(brightnessButtons[i]);

		if (buttonPressed && brightnessButtons[i] != lastBrightnessButton) {
			data[0] = 2;
			data[1] = i;
			Serial.write(data, sizeof(data));
			lastBrightnessButton = brightnessButtons[i];
			lastBrightnessButtonPressed = millis();
			resetBrightness = true;

			break;
		} else if (buttonPressed && resetBrightness && millis() - lastBrightnessButtonPressed > BRIGHTNESS_RESET_TIME) {
			data[0] = 2;
			data[1] = 2;
			Serial.write(data, sizeof(data));
			resetBrightness = false;
		} else if (brightnessButtons[i] == lastBrightnessButton && !buttonPressed) {
			lastBrightnessButton = 0xFF;
		}
	}

	delay(75);  // debounce
}
