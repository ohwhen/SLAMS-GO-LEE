/*

SID LEE

Released under Attribution-NonCommercial-ShareAlike 3.0 Unported
http://creativecommons.org/licenses/by-nc-sa/3.0/legalcode

 */

#include <SoftwareSerial.h>
#include <WiFlyHQ.h>
#include <Wire.h>
#include <Flash.h>
#include <QueueList.h>
#include <MsTimer2.h>

#include "config.h"
#include "MemoryFree.h"
#include "debug.h"
#include "i2cCommands.h"
#include "controller.h"

// Set up super high speed über analogRead
#ifndef cbi
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif

#define SINGLE_PLAYER 0
#define NO_LASER 0

// Pin to led mapping
FLASH_ARRAY(int, ledTable, 
	1,
	2,
	0,
	3,
	9,
	4,
	8,
	5,
	7,
	6,

	12,
	13,
	11,
	14,
	10,
	15,
	19,
	16,
	18,
	17
);


// PROGMEM strings
// ------------------------------------
FLASH_STRING(flash_startup, "startup");
FLASH_STRING(flash_start_game, "game");
FLASH_STRING(flash_ping, "go");


// Variables
// ------------------------------------

WiFly wifly;
SoftwareSerial wifi(WIFI_RX, WIFI_TX);

QueueList<String> i2cOutQueue;
QueueList<String> i2cInQueue;
String i2cInBuffer;
QueueList<String> offlineQueue;

Controller controller1(I2C_CODE_LEFT);
Controller controller2(I2C_CODE_RIGHT);

bool offlineMode = true;

bool gameIsActive;
volatile byte previousGoal;
volatile long previousGoalTime;
long registerWinTime;

byte winner = 0;
bool celebration;

long previousConnection = 0;
bool connected = false;
bool blinkGoalLeds = false;

volatile byte newGoalFromLaser = 0;

volatile int val1;
volatile int val2;

// Utils
// ------------------------------------

void i2c(byte command, byte target = 0, String data = "")
{
	String d = String(target, DEC);
	d += String(command, DEC);

	if (data != NULL && data.length() > 0)
	{
		info(F("Queueing I2C with data ("));
		info(data.length());
		info(F("): "));
		infoln(data);
		i2cOutQueue.push(d + "#" + data.substring(0, 32)); // max length is 32 chars
	}
	else
	{
		infoln(F("Queueing I2C without data"));
		i2cOutQueue.push(d + "#"); // max length is 16 chars	
	}
}

inline byte transmitI2C(byte target, String message)
{
	info(F("Transmitting I2C: "));
	info(message);
	info(F(" to " ));
	infoln(target);
	Wire.begin(); // Set I2C Master
	Wire.beginTransmission(target);
	byte n = message.length();
	for (byte i = 0; i < n; i++)
	{
		Wire.write(message[i]);
	}
	Wire.write('\0');
	byte code = Wire.endTransmission(true);

	Wire.begin(I2C_CODE_MASTER); // Set I2C Slave
	Wire.onReceive(i2cReceiveHandler);

	switch (code) {
		case 0:
			// success
			infoln(F("I2C sent"));
			break;
		case 1:
			errorln(F("I2C error 1: data too long to fit in transmit buffer"));
			break;
		case 2:
			errorln(F("I2C error 2: received NACK on transmit of address"));
			break;
		case 3:
			errorln(F("I2C error 3: received NACK on transmit of data"));
			break;
		case 4:
			errorln(F("I2C error 4: other error"));
			break;
	}

	return code;
}

void i2cReceiveHandler(int numBytes)
{
	while (Wire.available() > 0)
	{
		char c = Wire.read();
		if (c == 0)
		{
			if (i2cInBuffer.length() > 0)
			{
				i2cInQueue.push(i2cInBuffer);
			}
			i2cInBuffer = "";
		}
		else
		{
			i2cInBuffer += c;
		}
	}
}

void calibrate(bool quick = false)
{
	logln(F("Calibrating.."));
	int i;
	bool previousLaserState = digitalRead(LASER);

	float val = 0;
	float ambient = 0;
	float laser = 0;

	byte iterations = quick ? 5 : 15;
	int waitTime = quick ? 50 : 100;

	digitalWrite(LASER, HIGH); // Off

	// Calibrate lasers (how fucking cool is that!)
	logln(F("Calibrating lasers.."));

	logln(F("Controller 1"));
	digitalWrite(LASER, HIGH);

	// Left controller
	val = (float)analogRead(LASER_IN_RIGHT);
	for (i = 0; i < iterations; i++)
	{
		val += (float)(analogRead(LASER_IN_RIGHT) - val) * 0.1;
		delay(waitTime);
	}
	ambient = val;

	log(F("  ambient: "));
	logln(ambient);

	digitalWrite(LASER, LOW); // On
	delay(100);

	val = (float)analogRead(LASER_IN_LEFT);
	for (i = 0; i < iterations; i++)
	{
		val += (float)(analogRead(LASER_IN_LEFT) - val) * 0.1;
		delay(waitTime);
	}
	laser = (int)val;

	log(F("  laser: "));
	logln(laser);

	val = (laser - ambient) * GOAL_THRESHOLD1 + ambient;
	controller1.goalThreshold = (int)val;

	log(F("  goal threshold: "));
	logln(controller1.goalThreshold);

	logln(F("Controller 2"));
	digitalWrite(LASER, HIGH);
	delay(100);

	val = (float)analogRead(LASER_IN_LEFT);
	for (i = 0; i < iterations; i++)
	{
		val += (float)(analogRead(LASER_IN_RIGHT) - val) * 0.1;
		delay(waitTime);
	}
	ambient = val;

	log(F("  ambient: "));
	logln(ambient);

	digitalWrite(LASER, LOW); // On
	delay(100);

	val = (float)analogRead(LASER_IN_LEFT);
	for (i = 0; i < iterations; i++)
	{
		val += (float)(analogRead(LASER_IN_LEFT) - val) * 0.1;
		delay(waitTime);
	}
	laser = val;

	log(F("  laser: "));
	logln(laser);

	val = (laser - ambient) * GOAL_THRESHOLD2 + ambient;

	controller2.goalThreshold = (int)val;
	log(F("  goal threshold: "));
	logln(controller2.goalThreshold);

	digitalWrite(LASER, previousLaserState);
	delay(100);
}

void send(_FLASH_STRING data)
{
	checkConnection();
	data.print(wifly);
	wifly.println();
	previousConnection = millis();
}

void send(String data)
{
	info(F("send: "));
	infoln(data);
	checkConnection();
	wifly.println(data);
	previousConnection = millis();
}

void updateScores(byte off = 0)
{
	unsigned long val = 0;

	int j;
	int n;

	n = (off == I2C_CODE_RIGHT) ? controller2.previousScore : controller2.score;
	for (j = 0; j < n; j++)
	{
		val |= (unsigned long)1 << ledTable[j + 0];
	}

	n = (off == I2C_CODE_LEFT) ? controller1.previousScore : controller1.score;
	for (j = 0; j < n; j++)
	{
		val |= (unsigned long)1 << ledTable[j + 10];
	}
	
	shiftout(val);
}

void shiftout(unsigned long val)
{
	// Update score leds
	byte register1 = val & B11111111;
	byte register2 = (val >> 8) & B11111111;
	byte register3 = (val >> 16) & B11111111;

	// shift the bytes out:
	digitalWrite(LATCH, LOW);
	shiftOut(DATA, CLOCK, MSBFIRST, register3);
	shiftOut(DATA, CLOCK, MSBFIRST, register2);
	shiftOut(DATA, CLOCK, MSBFIRST, register1);
	digitalWrite(LATCH, HIGH);
}


// Setip
// ------------------------------------
void setup()
{
	Serial.begin(57600);
	logln(F("GO LEE Master ready"));

	// über fast analog read!
	// sbi(ADCSRA,ADPS2) ;
	// cbi(ADCSRA,ADPS1) ;
	// cbi(ADCSRA,ADPS0) ;

	// Init pins
	pinMode(LATCH, OUTPUT);
	pinMode(CLOCK, OUTPUT);
	pinMode(DATA, OUTPUT);
	pinMode(GOAL_LEFT, OUTPUT);
	pinMode(GOAL_RIGHT, OUTPUT);
	pinMode(LASER, OUTPUT);
	pinMode(LASER_IN_LEFT, INPUT);
	pinMode(LASER_IN_RIGHT, INPUT);
	digitalWrite(LASER, HIGH); // Active low -> Turn laser off

	// Clear lights if on from previous round
	shiftout(0);

	log(F("Available memory: "));
	logln(freeMemory());

	i2cInBuffer = "";
	while (Wire.available() > 0) Wire.read(); // Clear I2C buffer

	MsTimer2::set(1, readLasers);

	randomSeed(analogRead(0));

	delay(2000);

	calibrate();

	i2c(I2C_OUT_STARTUP);
}

void initWifi()
{
	logln(F("Initializing network"));

	wifi.begin(9600);

	logln(F("Clearing buffer"));
	while (wifi.available() > 0) wifi.read(); // Clear wifi buffer

	infoln(F("Initializing wifly"));
	if (!wifly.begin(&wifi, &Serial))
	{
		error(F("Failed to initialize wifi"));
		delay(1000);
		initWifi();
		return;
	}

	infoln(F("Rebooting wifly to known state"));
	wifly.reboot();

	/* Join wifi network if not already associated */
	if (!wifly.isAssociated()) {
		/* Setup the WiFly to connect to a wifi network */
		infoln(F("Joining network"));
		wifly.setSSID(ssid);
		wifly.setPassphrase(password);
		wifly.enableDHCP();

		if (wifly.join()) {
			infoln(F("Joined wifi network"));
		} else {
			errorln(F("Failed to join wifi network"));
			return; // retry
		}
	} else {
		infoln(F("Already joined network"));
	}

	infoln(F("Setting wifi device id"));
	wifly.setDeviceID("GOLEE");
	infoln(F("Setting wifi protocol: TCP"));
	wifly.setIpProtocol(WIFLY_PROTOCOL_TCP);
	// info(F("Setting wifi channel: "));
	infoln(wifi_channel);
	wifly.setChannel(0); // auto

	logln(F("Wifi initialized"));

	if (wifly.isConnected()) {
		logln(F("Closing old connection"));
		wifly.close();
	}

	while (!connect())
	{
		errorln(F("Connection failed. Retrying"));
		delay(250);
	}

	send(flash_startup);
	wifly.gets(NULL, 0); // ignore response

	logln(F("Connection ready"));

	i2c(I2C_OUT_ENABLE);
}

boolean connect()
{
	logln(F("Connecting to server.."));

	if (wifly.open(server, port, true)) {
		logln(F("Connected!"));

		previousConnection = millis();
		connected = true;

		while (!offlineQueue.isEmpty())
		{
			send(offlineQueue.pop());
			wifly.gets(NULL, 0); // ignore response
			delay(100);
		}

		return true;
	} else {
		errorln(F("Failed to connect."));
		return false;
	}
}

void checkConnection()
{
	if (wifly.isConnected() == false)
	{
		reconnect();
	} else {
		if (wifly.available() < 0)
		{
			reconnect();
		}
	}
}

void reconnect()
{
	errorln(F("Reconnecting.."));
	connected = false;
	i2c(I2C_OUT_CONNECTION_LOST);
	MsTimer2::stop();
	if (!connect())
	{
		delay(250);
	}
	else
	{	
		logln(F("Reconnected!"));
		if (gameIsActive) MsTimer2::start();
		i2c(I2C_OUT_RECONNECTED);
	}
}

// Loop
// ------------------------------------
void loop()
{
	if (gameIsActive)
	{
		info(val1);
		info(F(" / "));
		info(controller1.goalThreshold);
		info(" -- ");
		info(val2);
		info(F(" / "));
		infoln(controller2.goalThreshold);
	}

	// Need to wait for previous loop to finish to transmit I2C data first
	if (celebration)
	{
		// Let's also make sure all messages were sent (should be 3)
		if (i2cOutQueue.isEmpty())
		{
			celebrate();
			celebration = false;
			winner = 0;
			registerWinTime = 0;
		}
	}

	if (!offlineMode)
	{
		checkConnection();
	}

	// if (!offlineMode && wifly.available() < 0)
	// {
	// 	errorln(F("Disconnected"));
	// 	connected = false;
	// 	i2c(I2C_OUT_CONNECTION_LOST);
	// 	MsTimer2::stop();
	// 	if (!connect())
	// 	{
	// 		delay(250);
	// 	}
	// 	else
	// 	{
	// 		if (gameIsActive) MsTimer2::start();
	// 		i2c(I2C_OUT_RECONNECTED);
	// 	}
	// }

	// Check for queued I2C output
	if (!i2cOutQueue.isEmpty())
	{
		String msg = i2cOutQueue.pop();

		info(F("Processing I2C out: "));
		infoln(msg);

		byte target = msg[0] - '0';
		byte result;
		
		if (target == 0)
		{
			transmitI2C(I2C_CODE_LEFT, msg.substring(1));
			transmitI2C(I2C_CODE_RIGHT, msg.substring(1));
		}
		else
		{
			transmitI2C(target, msg.substring(1));
		}

		Wire.begin(I2C_CODE_MASTER); // Set I2C Slave
		Wire.onReceive(i2cReceiveHandler);
	}

	if (!i2cInQueue.isEmpty())
	{
		// Format: {i2c address}{action}{data}
		String data = i2cInQueue.pop();
		byte i2cSource = data[0] - '0';
		byte action = data[1] - '0';

		Controller *controller = getController(i2cSource);

		log(F("I2C message from: "));
		log(i2cSource);
		log(F(", action: "));
		log(action);
		log(F(", data: "));
		logln(data.substring(2));

		int result;

		switch (action)
		{
			case I2C_IN_STARTED_UP:
				controller->state = CONTROLLER_STATE_STARTED_UP;

				infoln(F("Controller started"));

#if SINGLE_PLAYER == 1
				if (controller1.state == CONTROLLER_STATE_STARTED_UP)
#else
				if (controller1.state == CONTROLLER_STATE_STARTED_UP && controller2.state == CONTROLLER_STATE_STARTED_UP)
#endif			
				{
					initWifi();
				}
				break;
			case I2C_IN_OFFLINE:
				logln(F("Offline mode"));
				controller->state = CONTROLLER_STATE_STARTED_UP;
				offlineMode = true;
				i2c(I2C_OUT_OFFLINE);
				// calibrate();
				// startGame();
				break;
			case I2C_IN_ONLINE:
				offlineMode = false;
				break;
			case I2C_IN_RFID:
				if (connected)
				{
					if (data.substring(2).length() < 12)
					{
						error(F("Invalid RFID data: "));
						errorln(data.substring(2));
						break;
					}
					log(F("Got RFID code: "));
					logln(data.substring(2));
					controller->setRFID(data.substring(2));

					char buf[33];
					send("id:" + String(controller->sideOnServer()) + ":" + data.substring(2));
					delay(10);
					result = wifly.gets(buf, 33, 5000);

					if (result > 0)
					{
						log(F("Got server response: "));
						logln(buf);

						if (buf[0] == '?')
						{
							i2c(I2C_OUT_TEMP_PLAYER, i2cSource, String(buf).substring(1));
						}
						else
						{
							i2c(I2C_OUT_PLAYER, i2cSource, String(buf));
						}
					} else {
						log(F("Could not get rfid response from server. Result: "));
						logln(result);
						// i2c(I2C_OUT_PLAYER, i2cSource, String("Unknown response"));
					}

#if SINGLE_PLAYER == 1
					if (controller1.hasPlayer())
#else
					if (controller1.hasPlayer() && controller2.hasPlayer())
#endif
					{
						calibrate(true);
						startGame();
					}
				}
				break;
			case I2C_IN_ADD_POINT:
				if (gameIsActive && controller->score < MAX_SCORE && ((connected && controller->hasPlayer()) || offlineMode))
				{
					addPoint(i2cSource);
				}
				break;
			case I2C_IN_REMOVE_POINT:
				if (gameIsActive && controller->score > 0 && ((connected && controller->hasPlayer()) || offlineMode))
				{
					removePoint(i2cSource);
				}
				break;
			case I2C_IN_RESET:
				logln(F("Players reset"));
				send("reset:" + String(controller->sideOnServer()));
				wifly.gets(NULL, 0);
				controller->reset();
				newGoalFromLaser = 0;
				updateScores();
				stopGame();
				break;
		}
	}

	if (!celebration && winner > 0 && millis() > registerWinTime)
	{
		logln(F("Registering win"));

		stopGame();

		if (!offlineMode)
		{
			wifly.flushRx();
			char buf[33];
			send("end:" + String(getController(winner)->sideOnServer()));
			byte result = wifly.gets(buf, 33, 5000);

			if (result > 0)
			{
				log(F("Got end response: "));
				logln(buf);

				i2c(I2C_OUT_WIN, winner);
				i2c(I2C_OUT_LOSS, winner == I2C_CODE_LEFT ? I2C_CODE_RIGHT : I2C_CODE_LEFT);

				registerWinTime = 0;
				celebration = true;

				// {side}:{points},{side:points}
				char *input = buf;
				char *str;
				while ((str = strtok_r(input, ",", &input)) != NULL)
    			{
    				byte target = str[0] - '0';
					i2c(I2C_OUT_END, target == 1 ? I2C_CODE_LEFT : I2C_CODE_RIGHT, String(str).substring(2));
    			}
			} else {
				logln(F("Could not register win"));
				delay(1000);
			}
		}
		else
		{
			i2c(I2C_OUT_WIN, winner);
			i2c(I2C_OUT_LOSS, winner == I2C_CODE_LEFT ? I2C_CODE_RIGHT : I2C_CODE_LEFT);
			registerWinTime = 0;
			celebration = true;
		}
	}

	if (Serial.available()) {
		char c = Serial.read();

		if (c == 'm')
		{
			Serial.print(F("Available memory: "));
			Serial.println(freeMemory(), DEC);
		}
		else if (!offlineMode && c == 't')
		{
			Serial.println(F("Entering terminal mode"));
			while (1) {
				if (wifly.available() > 0) {
					Serial.write(wifly.read());
				}

				if (Serial.available()) { // Outgoing data
					wifly.write(Serial.read());
				}
			}
		}

		while (Serial.available() > 0) Serial.read();
	}

	if (!offlineMode && wifly.isConnected() && wifly.peek() == '!')
	{
		// New player registered
		char buf[33];
		int result = wifly.gets(buf, 33, 2500);
		i2c(I2C_OUT_PLAYER, 0, String(buf).substring(1));	
	}

	if (blinkGoalLeds)
	{
		Controller *controller = getController(previousGoal);

		int n = (controller->previousScore < controller->score ? 15 : 3);
		log(F("Blinking "));
		log(n);
		logln(F(" times"));
		for (byte i = 0; i < n; i++)
		{
			delay(50);
			updateScores(previousGoal); // off
			delay(50);
			updateScores(); // on
		}
		blinkGoalLeds = false;
	}

	if (newGoalFromLaser > 0)
	{
		addPoint(newGoalFromLaser);
		i2c(I2C_OUT_GOAL, newGoalFromLaser);
		newGoalFromLaser = 0; // will enable laser read again
	}
}

Controller *getController(byte code)
{
	if (code == controller1.i2cCode) return &controller1;
	if (code == controller2.i2cCode) return &controller2;
	return NULL;
}

void celebrate()
{
	logln(F("Celebration!"));
	long start = millis();

	while (millis() < start + CELEBRATION_TIME)
	{
		unsigned long val = 0;

		byte led = random(10);
		if (winner == I2C_CODE_LEFT) led += 10;
		val |= (unsigned long)1 << ledTable[led];

		shiftout(val);

		delay(25);
	}
	shiftout(0);
	
	if (!offlineMode)
	{
		while (Wire.available() > 0) Wire.read(); // clear any controller actions during celebration
		// wifly.flushRx();
	}

	calibrate(true);
	startGame();
}

void startGame()
{
	logln(F("Game started!"));
	gameIsActive = true;
	winner = 0;
	previousGoal = 0;
	newGoalFromLaser = 0;
	controller1.score = controller1.previousScore = 0;
	controller2.score = controller2.previousScore = 0;

	i2c(I2C_OUT_GAME_STARTED);

	digitalWrite(LASER, LOW); // On

	updateScores();

	if (!offlineMode)
	{
		delay(100);
		send(flash_start_game);
		wifly.gets(NULL, 0);
	}

	MsTimer2::start();
}

void stopGame()
{
	logln(F("Game stopped"));
	gameIsActive = false;
	previousGoal = 0;
	digitalWrite(LASER, HIGH); // Off
	MsTimer2::stop();
}

void addPoint(byte target)
{
	log(F("Add point: "));
	logln(target);
	Controller *controller = getController(target);
	if (controller->score < MAX_SCORE)
	{
		controller->previousScore = controller->score;
		controller->score++;
		previousGoal = target;
		
		blinkGoalLeds = true;

		if (controller->score >= MAX_SCORE)
		{
			// Don't set winner if other player has already reached max points
			Controller *otherController = getController(target == I2C_CODE_LEFT ? I2C_CODE_RIGHT : I2C_CODE_LEFT);
			if (otherController->score < MAX_SCORE)
			{
				winner = target;
				log(winner);
				logln(F(" won"));

				registerWinTime = millis() + GAME_END_TIME;
			}
		}

		if (!offlineMode)
		{
			send("addgoal:" + String(controller->sideOnServer()));
			wifly.gets(NULL, 0);
		}
	}
}

void removePoint(byte target)
{
	Controller *controller = getController(target);
	if (controller->score > 0)
	{
		controller->previousScore = controller->score;
		controller->score--;
		previousGoal = target;
		
		blinkGoalLeds = true;
		
		// Abort potential win
		winner = 0;
		registerWinTime = 0;

		if (!offlineMode)
		{
			send("remgoal:" + String(controller->sideOnServer()));
			wifly.gets(NULL, 0);
		}
	}
}

void readLasers()
{
#if NO_LASER == 1
	return;
#endif

	if (!gameIsActive || newGoalFromLaser > 0) return;
	
	// Monitor lasers
	if (millis() < previousGoalTime + MIN_GOAL_DELAY) return;

	cli(); // disable interrupts

	byte goal = 0;
	static float val;

	if (controller1.score < MAX_SCORE)
	{
		val1 = analogRead(LASER_IN_RIGHT);
		if (val1 < controller1.goalThreshold)
		{
			goal = I2C_CODE_LEFT;
		}
	}

	if (controller2.score < MAX_SCORE)
	{
		val2 = analogRead(LASER_IN_LEFT);
		if (val2 < controller2.goalThreshold)
		{
			goal = I2C_CODE_RIGHT;
		}
	}

	if (goal > 0)
	{
		previousGoalTime = millis();
		newGoalFromLaser = goal;
	}
	sei(); // enable interrupts
}