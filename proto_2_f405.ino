#define LED0 A14
#define LED1 A13

void setup() {
	pinMode(LED0, OUTPUT);
	pinMode(LED1, OUTPUT);
	//Serial.begin(115200);
}

void loop() {
	digitalWrite(LED0, HIGH);
	digitalWrite(LED1, LOW);
	delay(500);
	digitalWrite(LED0, LOW);
	digitalWrite(LED1, HIGH);
	delay(500);
	//Serial.println("hello there");
}
