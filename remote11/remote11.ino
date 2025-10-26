// 29/11/22

/*-Power on: anim default
-Double-clic n'importe quand: lock/unlock. Si lock: anim default. Si unlock: laisser anim default mais permettre de changer.
-Locked: permettre seulement un double-clic pour unlock.
-Unlocked: choisir l'anim qu'on veut, pouce + un ou plusieurs doigts.

Fluburtur, [16/11/2022 16:41]
et quand unlock normalement ça revient sur la default mais des fois il joue une random aussi

Fluburtur, [16/11/2022 16:41]
je crois que le timer pour les random continue de tourner quand il est lock donc ça en met une en attente

Ftk Yote, [16/11/2022 16:41]
Ok, tu préfères quoi ? Laisser default, revenir sur celle d'avant, ou random ?

Fluburtur, [16/11/2022 16:41]
revenir sur default
 */

// Serial comm with main board UART 8N1@115200
// Serial frame: 2 bytes, AA BB
// AA: 1=Lock? 0=Unlock? 2=Brightness
// BB: Animation number / Brightness: 0:- 1:+ 2:Reset

// Input polarity setting. Depends on wiring.
// True: Use Arduino's internal pull-ups, common ground.
// False: Use external pull-downs, common +V.
#define BUTTON_PULLUP true

// Maximum amount of time (ms) for double click to get registred
#define MAX_DOUBLECLICK_TIME 500

// Amount of time for which the button must be held in order to reset the brightness
#define BRIGHTNESS_RESET_TIME 1000

// Minimum amount of time for a finger pair touch to be registered (in units of 100ms)
#define TOUCH_MIN_TIME 1

typedef struct {
  uint8_t buttons[3];
  uint8_t anim_n;
} comb_t;

// Finger combination list
// Each line: Finger A, finger B, finger C, animation number
// Set a finger to 0 if not used
const comb_t combinations[] = {
//  {12, 13, 0, 5},  // Example: thumb (implicit), fingers 12 and 13 touching: chose animation #5
//(finger-input): (1-13)(2-12)(3-11)(4-10)(5-6)(6-7)(7-8)(8-9)
  {A1, 0, 0, 1},
  {A0, 0, 0, 2},
  {11, 0, 0, 3},
  {10, 0, 0, 4},
  {8, 0, 0, 8},
  {7, 0, 0, 7},
  {6, 0, 0, 6},
  {5, 0, 0, 5},
  {A1, A0, 0, 9},
  {A0, 11, 0, 10},
  {11, 10, 0, 11},
  {5, 6, 0, 12},
  {6, 7, 0, 13},
  {7, 8, 0, 14}
};

uint8_t last_combination = 0xFF;  // Index of last finger combination match
uint8_t debounce_timer;
const uint8_t combination_count = sizeof(combinations) / sizeof(combinations[0]);

const uint8_t buttons[] =   {A1, A0, 11, 10, 5, 6, 7, 8};
//const uint8_t buttons[] = {13, 12, 11, 10, 6, 7, 8, 9};

const uint8_t brightnessButtons[] = {/*A0, 5*/};

uint8_t lastButton = 0xFF;  // ID of last pressed button
uint8_t buttonClickCount = 0;
bool defaultAnimationLocked = false;
uint32_t lastButtonPressed;
uint8_t lastBrightnessButton = 0xFF;
bool resetBrightness = false;
uint32_t lastBrightnessButtonPressed;

void SetupInputs(const uint8_t* inputs, const uint8_t len) {
  for (uint8_t i = 0; i < len; i++)
    pinMode(inputs[i], BUTTON_PULLUP ? INPUT_PULLUP : INPUT);
}

void SerialFrame(const uint8_t byte_a, const uint8_t byte_b) {
  uint8_t data[2];
  data[0] = byte_a;
  data[1] = byte_b;
  Serial.write(data, sizeof(data));
  /*Serial.print("SerialFrame ");
  Serial.print(byte_a, DEC);
  Serial.print("-");
  Serial.print(byte_b, DEC);
  Serial.println();*/
}

void setup() {
	Serial.begin(115200);
  SetupInputs(buttons, sizeof(buttons));
  //SetupInputs(brightnessButtons, sizeof(brightnessButtons));
}

void loop() {
  uint8_t i, j, button, last_anim = 0;
  
  /*for (uint8_t i = 0; i < 8; i++) {
    Serial.print(digitalRead(buttons[i]), DEC);
  }
  Serial.println();*/

  uint8_t b, btn, match;
  uint8_t sendframe = 0, cmd, val;

  // Check each finger combination, save match, send serial frame if match steady for TOUCH_MIN_TIME samples.
  for (i = 0; i < combination_count; i++) { // 
    /*Serial.print("Testing combination ");
    Serial.print(i, DEC);
    Serial.println();*/
    
    match = 1;
    for (b = 0; b < 8; b++) {
      // See if button is used in sequence
      btn = buttons[b];
      if (btn) {
        for (j = 0; j < 3; j++) {
          if (btn == combinations[i].buttons[j]) {
            // Found
            if (digitalRead(btn) != (BUTTON_PULLUP ? LOW : HIGH)) {
              //Serial.print("Failed at 1");
              match = 0;  // Wrong state, can't be this combination
            }
            break;
          }
        }
        if (j == 3) {
          // Not found: must be low
          if (digitalRead(btn) != (BUTTON_PULLUP ? HIGH : LOW)) {
            /*Serial.print("Failed at 2:");
            Serial.print(btn, DEC);
            Serial.print(" should be low ");
            Serial.println();*/
            match = 0;
            break;    // Wrong state
          }
        }
      }
    }
    
    if (match) {
      // Match

      // DEBUG
      /*if (i != last_combination) {
        Serial.print("Matched index ");
        Serial.print(i, DEC);
        Serial.println();
      }*/
           
      if (i == last_combination) {
        if (debounce_timer == TOUCH_MIN_TIME) {
          if (!defaultAnimationLocked) {
            sendframe = 1;
            cmd = 1;
            val = combinations[i].anim_n;
            last_anim = combinations[i].anim_n;
          }
        }
        if (debounce_timer <= TOUCH_MIN_TIME)
          debounce_timer++; // Inc once more after TOUCH_MIN_TIME to avoid sending serial frames continuously if fingers keep touching
      } else
        debounce_timer = 0;
      
      last_combination = i;
      break;
    }
  }

  if (i == combination_count) { //combination_count)
    if (last_combination != 0xFF) {
      if (!defaultAnimationLocked) {
        sendframe = 1;
        cmd = 0;
        val = last_anim;
      }
    }
    last_combination = 0xFF;  // No match on last scan
  }
  
  // Double-click logic
	for (uint8_t i = 0; i < sizeof(buttons); i++) {
		bool buttonPressed = BUTTON_PULLUP ? !digitalRead(buttons[i]) : digitalRead(buttons[i]);

		if (buttonPressed && buttons[i] != lastButton) {
      // Button pressed for the first time
			lastButton = buttons[i];
			lastButtonPressed = millis(); // Overflow risk 50 days
			buttonClickCount = 0;

			// Don't read states of any other buttons
			break;
		} else if (buttons[i] == lastButton) {
		  // No other button was pressed during debounce time
			if (millis() - lastButtonPressed > MAX_DOUBLECLICK_TIME) {
				if (buttonPressed) {
          // Same button still pressed for > MAX_DOUBLECLICK_TIME
				  // buttonClickCount is also used to indicated that the animation isn't already locked
					/*if (buttonClickCount == 0 && !defaultAnimationLocked) {
						// The button still wasn't released
            SerialFrame(1, i + 1);  // lock given animation
						buttonClickCount++;     // Increment buttonClickCount to indicate that we've already locked the animation
					}*/
				} else {
					// Button was released
					/*if (!defaultAnimationLocked)
            SerialFrame(0, i + 1);*/

					lastButton = 0xFF;  // Reset last button
				}
			} else if (!buttonPressed && buttonClickCount == 0) {
				// Button was released for the first time
				buttonClickCount++;
			} else if (buttonPressed && buttonClickCount == 1) {
				// Button was pressed second time within double-click period
        
        defaultAnimationLocked = !defaultAnimationLocked;
        
        /*Serial.print("Toggle lock ");
        Serial.print(defaultAnimationLocked, DEC);
        Serial.println();*/
        
        sendframe = 1;
        cmd = defaultAnimationLocked ? 1 : 0;
        val = last_anim;
        
        lastButton = 0xFF;  // Reset last button
        
				break;
			}
		}
	}

	/*for (uint8_t i = 0; i < sizeof(brightnessButtons); i++) {
		bool buttonPressed = BUTTON_PULLUP ? !digitalRead(brightnessButtons[i]) : digitalRead(brightnessButtons[i]);

		if (buttonPressed && brightnessButtons[i] != lastBrightnessButton) {
      SerialFrame(2, i);
      
			lastBrightnessButton = brightnessButtons[i];
			lastBrightnessButtonPressed = millis();
			resetBrightness = true;

			break;
		} else if (buttonPressed && resetBrightness && millis() - lastBrightnessButtonPressed > BRIGHTNESS_RESET_TIME) {
      SerialFrame(2, 2);
			resetBrightness = false;
		} else if (brightnessButtons[i] == lastBrightnessButton && !buttonPressed) {
			lastBrightnessButton = 0xFF;
		}
	}*/

  if (sendframe)
    SerialFrame(cmd, val);
  
	delay(50);
}
