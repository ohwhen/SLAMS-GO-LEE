/*

SID LEE

Released under Attribution-NonCommercial-ShareAlike 3.0 Unported
http://creativecommons.org/licenses/by-nc-sa/3.0/legalcode

 */

#include <SoftwareSerial.h>
#include <LiquidCrystal.h>
#include <Wire.h>
#include <Flash.h>
#include <QueueList.h>
#include <Tone.h>

#include "config.h"
#include "graphics.h"
#include "MemoryFree.h"
#include "debug.h"
#include "i2cCommands.h"

#define I2C_ADDRESS I2C_CODE_RIGHT

FLASH_STRING(text_starting_up, "Warming up..");
FLASH_STRING(text_swipe1, "Swipe your alarm");
FLASH_STRING(text_swipe2, "tag here!");
FLASH_STRING(text_reset1, "Hold buttons to");
FLASH_STRING(text_reset2, "clear players");
FLASH_STRING(text_reseting, "Clearing players");
FLASH_STRING(text_reset, "Players reset!");
FLASH_STRING(text_temp, "golee.sidlee.com");
FLASH_STRING(text_started, "Game started!");
FLASH_STRING(text_connection_lost, "Connection lost");
FLASH_STRING(text_reconnecting, "Reconnecting..");
FLASH_STRING(text_reconnected, "Reconnected!");
FLASH_STRING(text_loading, "Loading..");
FLASH_STRING(text_loading_team, "Loading team..");
FLASH_STRING(text_goal, "GOOOOOAL!");
FLASH_STRING(text_win1, "You win!");
FLASH_STRING(text_win2, "Updating score..");
FLASH_STRING(text_loss1, "Damn..");
FLASH_STRING(text_loss2, "You lost");
FLASH_STRING(text_none, "");
FLASH_STRING(text_offline1, "GO LEE");
FLASH_STRING(text_offline2, "Offline mode");

FLASH_STRING(snd_start, "log:d=4,o=5,b=120:32c6,32c6,32p,32c7");
FLASH_STRING(snd_login, "L:d=4,o=5,b=120:32c6,32e6,32c7");
FLASH_STRING(snd_win, "win:d=4,o=5,b=120:549p,33g,1280g6,33p,3840c7,33p,960e6,33p,16c6,8p,33c6,549p,33c7");
FLASH_STRING(snd_goal, "goal:d=4,o=5,b=160:32g,32p,32c6,31p,32e6,34p,17c7");
FLASH_STRING(snd_add, "pos:d=4,o=5,b=120:32c6,32e6,32c7");
FLASH_STRING(snd_remove, "neg:d=4,o=5,b=120:32c6,32g6,32b4,549p,32c#6");

FLASH_ARRAY(int, notes, 
	NOTE_C4, NOTE_CS4, NOTE_D4, NOTE_DS4, NOTE_E4, NOTE_F4, NOTE_FS4, NOTE_G4, NOTE_GS4, NOTE_A4, NOTE_AS4, NOTE_B4,
	NOTE_C5, NOTE_CS5, NOTE_D5, NOTE_DS5, NOTE_E5, NOTE_F5, NOTE_FS5, NOTE_G5, NOTE_GS5, NOTE_A5, NOTE_AS5, NOTE_B5,
	NOTE_C6, NOTE_CS6, NOTE_D6, NOTE_DS6, NOTE_E6, NOTE_F6, NOTE_FS6, NOTE_G6, NOTE_GS6, NOTE_A6, NOTE_AS6, NOTE_B6,
	NOTE_C7, NOTE_CS7, NOTE_D7, NOTE_DS7, NOTE_E7, NOTE_F7, NOTE_FS7, NOTE_G7, NOTE_GS7, NOTE_A7, NOTE_AS7, NOTE_B7
);

class Player
{
public:
	String rfid;
	String name;
	String prevPoints;
	String points;
	bool code;
	bool detected;
	boolean active;
};

class Message
{
public:
	boolean active;
	char line1[17];
	char line2[17];
	long timeout;
};


// Variables
// ------------------------------------

SoftwareSerial rfidSerial(RFID_RX, RFID_TX);
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);
Tone music;

QueueList<String> i2cOutQueue;
QueueList<String> i2cInQueue;
String i2cInBuffer;

bool loading = false;
long previousLoadingUpdate = 0;
int loaderBallPosition = 0; // 0-7
bool loaderBallAdvancing = true;

byte state = 0xFF;

Player player;
Player team;
byte currentScore = 0;

Message message;

// RFID specific
String tag;
unsigned int nowReset = 0;
unsigned int nowLastRfid = 0;
boolean rfidEnabled = false;
boolean rfidTagSeen = false;
byte rfidTagCurrent[RFID_TAG_LENGTH];
byte rfidTagTemp[6];

// Buttons
byte buttons[] = { BUTTON_PLUS, BUTTON_MINUS, BUTTON_RESET };
byte buttonValues[3];
byte prevButtonValues[3];
long timeButtonPressed[3];
long timeButtonReleased[3];
long buttonHold[3];
boolean ignoreRelease[3];
boolean reseting;
long resetStart;

bool connected;
bool offline;
bool blinkScore;

char *currentSoundFx = (char *)malloc(100 * sizeof(char));
char *currentSoundFxStart;
boolean playingSoundFx;
byte default_dur = 4;
byte default_oct = 6;
int bpm = 63;
int num;
long wholenote;
long duration;
byte note;
byte scale;


// Utils
// ------------------------------------

void i2c(byte command, String data = NULL)
{
	String d = String(command);

	if (data != NULL && data.length() > 0)
	{
		info(F("Queueing I2C with data ("));
		info(data.length());
		info(F("): "));
		infoln(data);
		i2cOutQueue.push(d + data.substring(0, 31)); // max length is 32 chars

		log(F("Available memory: "));
		logln(freeMemory());
	}
	else
	{
		infoln(F("Queueing I2C without data"));
		i2cOutQueue.push(d); // max length is 16 chars	
	}
}

void i2cReceiveHandler(int numBytes)
{
	while (Wire.available())
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

inline byte transmitI2C(String message)
{
	info(F("Transmiting I2C: "));
	infoln(message);
	log(F("Available memory: "));
	logln(freeMemory());
	Wire.begin(); // Set I2C Master
	Wire.beginTransmission(I2C_CODE_MASTER);
	Wire.write(I2C_ADDRESS + '0');
	byte n = message.length();
	for (byte i = 0; i < n; i++)
	{
		Wire.write(message[i]);
	}
	Wire.write('\0');
	byte code = Wire.endTransmission(true);

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

	Wire.begin(I2C_ADDRESS); // Set I2C Slave
	Wire.onReceive(i2cReceiveHandler);

	return code;
}

void updateScreen()
{
	if (state == STATE_STARTED_UP)
	{
		// Waiting for motherboard to boot up
		lcd.clear();
		text_starting_up.print(lcd);
	}
	else if (loading)
	{
		// Separate loading screen update
		return;
	}
	else if (!connected)
	{
		lcd.clear();
		lcd.setCursor(0, 0);
		text_connection_lost.print(lcd);
		lcd.setCursor(0, 1);
		text_reconnecting.print(lcd);
	}
	else if (message.active)
	{
		lcd.clear();
		lcd.setCursor(0, 0);
		lcd.print(message.line1);
		lcd.setCursor(0, 1);	
		lcd.print(message.line2);

		infoln(F("Message:"));
		info(F("  "));
		infoln(message.line1);
		info(F("  "));
		infoln(message.line2);
	}
	else if (offline)
	{
		lcd.clear();
		lcd.setCursor(0, 0);
		text_offline1.print(lcd);
		lcd.setCursor(0, 1);
		text_offline2.print(lcd);
	}
	else if (!player.active && !team.active)
	{
		// No players
		lcd.clear();
		lcd.setCursor(0, 0);
		text_swipe1.print(lcd);
		lcd.setCursor(0, 1);
		text_swipe2.print(lcd);
	}
	else
	{
		if (team.active)
		{
			lcd.clear();
			if (!team.detected)
			{
				text_loading_team.print(lcd);
			}
			else
			{
				if (team.name.length() > 11)
				{
					byte index = 11;
					if (team.name.indexOf(" ") > 0) {
						while (team.name[index] != ' ' && index > 0) index--;
						lcd.print(team.name.substring(0, index));
						index++; // skip space
						lcd.setCursor(0, 1);
						lcd.print(team.name.substring(index));
					} else {
						lcd.print(team.name.substring(0, 11)); // 15 - 4 - 1 space
						lcd.setCursor(0, 1);
						lcd.print(team.name.substring(11));
					}
				}
				else
				{
					lcd.print(team.name);
				}
				lcd.setCursor(16 - team.points.length(), 0);
				lcd.print(team.points);
			}
		}
		else if (player.active)
		{
			if (player.detected)
			{
				lcd.clear();
				if (player.name.length() >= 16 - 5)
				{
					lcd.print(player.name.substring(0, 16 - 6) + "..");
				}
				else
				{
					lcd.print(player.name);
				}

				lcd.setCursor(16 - player.points.length(), 0);
				lcd.print(player.points);
			}
			else
			{
				lcd.clear();
				text_loading.print(lcd);
			}
		}
	}
}

void playSoundFx(_FLASH_STRING song)
{
	currentSoundFx = currentSoundFxStart; // reset pointer position

	song.copy(currentSoundFx, song.length(), 0);
	currentSoundFx[song.length()] = 0; // add null terminator

	// song.copy(currentSoundFx, song.length());

	// format: d=N,o=N,b=NNN:
	// find the start (skip name, etc)


	while(*currentSoundFx != ':') currentSoundFx++;    // ignore name
	currentSoundFx++;                     // skip ':'

	// get default duration
	if (*currentSoundFx == 'd')
	{
		currentSoundFx++; currentSoundFx++;              // skip "d="
		num = 0;
		while(isdigit(*currentSoundFx))
		{
			num = (num * 10) + (*currentSoundFx++ - '0');
		}
		if (num > 0) default_dur = num;
		currentSoundFx++;                   // skip comma
	}

	// get default octave
	if (*currentSoundFx == 'o')
	{
		currentSoundFx++; currentSoundFx++;              // skip "o="
		num = *currentSoundFx++ - '0';
		if (num >= 3 && num <=7) default_oct = num;
		currentSoundFx++;                   // skip comma
	}

	// get BPM
	if (*currentSoundFx == 'b')
	{
		currentSoundFx++; currentSoundFx++;              // skip "b="
		num = 0;
		while(isdigit(*currentSoundFx))
		{
			num = (num * 10) + (*currentSoundFx++ - '0');
		}
		bpm = num;
		currentSoundFx++;                   // skip colon
	}

	// BPM usually expresses the number of quarter notes per minute
	wholenote = (60 * 1000L / bpm) * 4;  // this is the time for whole note (in milliseconds)

	infoln(F("Sound initialized"));

	playingSoundFx = true;
	// currentSoundFx = p;
}

void setMessage(_FLASH_STRING line1, _FLASH_STRING line2 = "", int timeout = 2000)
{
	message.active = true;
	line1.copy(message.line1, 16);
	line2.copy(message.line2, 16);
	message.timeout = millis() + timeout;
	updateScreen();
}

void clearMessage()
{
	setMessage(text_none, text_none, false);
	message.active = false;
	
	if (blinkScore)
	{
		logln(F("Blinking score"));
		String points = player.points;
		String prevPoints = player.prevPoints;
		for (int i = 0; i < 3; i++)
		{
			player.points = prevPoints;
			logln(F("Drawing points"));
			updateScreen();
			delay(200);

			player.points = "";
			logln(F("Drawing empty"));
			updateScreen();
			delay(200);
		}
		delay(200);
		blinkScore = false;
		player.points = points;
	}

	updateScreen();
}

void resetPlayers(boolean showMessage = true, boolean i2cOutput = true)
{
	reseting = false;
	player.active = false;
	player.name = "";
	player.rfid = "";
	team.active = false;
	team.name = "";
	team.rfid = "";
	currentScore = 0;

	if (showMessage)
	{
		setMessage(text_reset, text_none, 2500);
	}
	else
	{
		updateScreen();
	}

	if (i2cOutput) i2c(I2C_OUT_RESET);
}


// Setup
// ------------------------------------

void setup()
{
	Serial.begin(57600);
	logln(F("GO LEE Controller ready"));

	log(F("Available memory: "));
	logln(freeMemory());

	pinMode(LED, OUTPUT);
	pinMode(BUTTON_PLUS, INPUT); digitalWrite(BUTTON_PLUS, HIGH);
	pinMode(BUTTON_MINUS, INPUT); digitalWrite(BUTTON_MINUS, HIGH);
	pinMode(BUTTON_RESET, INPUT); digitalWrite(BUTTON_RESET, HIGH);
	pinMode(SPEAKER, OUTPUT);

	lcd.begin(16, 2);
	lcd.print(F("GO LEE"));

	Wire.begin(I2C_ADDRESS);
	Wire.onReceive(i2cReceiveHandler);

	currentSoundFxStart = currentSoundFx; // store pointer position so we can resume from here

	music.begin(SPEAKER);
	rfidSerial.begin(9600);

	randomSeed(analogRead(0));
}


// Loop
// ------------------------------------

void loop()
{
	if (!i2cOutQueue.isEmpty())
	{
		String msg = i2cOutQueue.pop();

		byte result;
		
		result = transmitI2C(msg);
		if (result != 0)
		{
			error(F("I2C failed, error "));
			errorln(result);
		}

		Wire.begin(I2C_ADDRESS); // Set I2C Slave
		Wire.onReceive(i2cReceiveHandler);
	}

	if (!i2cInQueue.isEmpty())
	{
		// Format: {i2c address}{action}{data}
		String data = i2cInQueue.pop();
		int index = data.indexOf("#");
		String commandStr = data.substring(0, index);
		char commandChar[commandStr.length() + 1];
		commandStr.toCharArray(commandChar, (commandStr.length() + 1));
		byte command = atoi(commandChar);
		data = data.substring(index + 1);

		log(F("I2C message ("));
		log(data.length());
		log(F("): command "));
		log(command);
		log(F(", data: "));
		logln(data);

		if (command == I2C_IN_STARTUP)
		{
			loading = true;
			connected = false;
			resetPlayers(false, false);

			for (int i = 0; i < LOADER_FRAMES; i++)
			{
				lcd.createChar(i, loader[i]);
			}

			// lcd.createChar(LOADER_FRAMES + 1, x);

			offline = !digitalRead(BUTTON_PLUS) && !digitalRead(BUTTON_MINUS);
			state = STATE_STARTED_UP;
			updateScreen(); // Warming up..

			// Active high
			for (int i = 0; i < 3; i++)
			{
				prevButtonValues[i] = HIGH;
			}

			if (offline)
			{
				i2c(I2C_OUT_OFFLINE);
			}
			else
			{
				i2c(I2C_OUT_STARTED_UP);
			}
		}
		else if (command == I2C_IN_OFFLINE)
		{
			logln(F("Offline mode enabled"));
			offline = true;
			state = STATE_ENABLED;
			loading = false;
			updateScreen();
		}
		else if (command == I2C_IN_ENABLE)
		{
			logln(F("Enabled"));
			state = STATE_ENABLED;
			loading = false;
			connected = true;

			updateScreen();

			i2c(I2C_OUT_ONLINE);

			playSoundFx(snd_start);
		}
		else if (command == I2C_IN_PLAYER || command == I2C_IN_TEMP_PLAYER)
		{
			int rfidIndex = data.indexOf(":"); if (rfidIndex < 0) rfidIndex = data.length();
			int nameIndex = data.indexOf("|"); if (nameIndex < 0) nameIndex = data.length();
			String rfid = data.substring(0, rfidIndex);
			String name = data.substring(rfidIndex + 1, nameIndex);
			String points = data.substring(nameIndex + 1);

			logln(F("Got tag details"));
			log(F("  "));
			logln(rfid);
			log(F("  "));
			logln(name);
			log(F("  "));
			logln(points);

			if (command == I2C_IN_TEMP_PLAYER)
			{
				setMessage(text_none, text_temp, 2500);

				String codeText = "Code: " + name;
				codeText.toCharArray(message.line1, 17);
				message.line1[11] = '\0';

				name = "(" + name + ")";
			}

			if (rfid == player.rfid)
			{
				player.name = name;
				player.points = points;
				player.detected = true;
				log(F("Player details received: name: "));
				log(player.name);
				log(F(", points: "));
				logln(player.points);
			}
			else if (rfid == team.rfid)
			{
				team.name = name;
				team.points = points;
				team.detected = true;
				log(F("Team details received: name: "));
				log(team.name);
				log(F(", points: "));
				logln(team.points);
			}

			playSoundFx(snd_login);

			updateScreen();
		}
		else if (command == I2C_IN_GOAL)
		{
			currentScore++;
			setMessage(text_goal, text_none, 1500);
			playSoundFx(snd_goal);
		}
		else if (command == I2C_IN_WIN)
		{
			setMessage(text_win1, text_win2, 2500);
			playSoundFx(snd_win);
		}
		else if (command == I2C_IN_LOSS)
		{
			setMessage(text_loss1, text_loss2, 2500);
		}
		else if (command == I2C_IN_GAME_STARTED)
		{
			currentScore = 0;
			// setMessage(text_started, text_none, 1000);
		}
		else if (command == I2C_IN_CONNECTION_LOST)
		{
			errorln(F("Connection lost"));
			connected = false;
			updateScreen();
		}
		else if (command == I2C_IN_RECONNECTED)
		{
			logln(F("Reconnected"));
			connected = true;
			setMessage(text_reconnected, text_none, 1000);
			updateScreen();
		}
		else if (command == I2C_IN_END)
		{
			logln(F("Got updated score"));
			player.prevPoints = player.points;
			player.points = data;

			log(player.prevPoints);
			log(F(" -> "));
			logln(player.points);
			blinkScore = true;
		}
	}

	if (state == STATE_ENABLED)
	{
		// Monitor RFID
		// This is quite ugly from a library, but it works..
		updateID12(false);
		clearTag(rfidTagTemp, 6);
		byte action = 0;
		unsigned int now = millis();
		if (!offline && rfidSerial.available()) 
		{
			// wait for the next STX byte
			while (rfidSerial.available() && action != 0x02)
			{
				action = rfidSerial.read();
			}
      
			// STX byte found -> RFID tag available
			if (action == 0x02)
			{
				if (readID12(rfidTagTemp))
				{
					nowLastRfid = millis();
					rfidTagSeen = true;

					updateCurrentRfidTag(rfidTagTemp);

					if (
						(tag == player.rfid && !player.detected) ||
						(tag == team.rfid && !team.detected)
					)
					{
						logln(F("Same as before but we don't have the name, retrying.."));
						i2c(I2C_OUT_RFID, tag); // retry
					}
					else if (tag != player.rfid && tag != team.rfid)
					{
						log(F("Got tag: "));
						logln(tag);

						if (!player.active)
						{
							infoln(F("Setting player"));

							player.active = true;
							player.rfid = tag;
							player.name = "";
							player.detected = false;
							i2c(I2C_OUT_RFID, player.rfid);
							clearMessage();
						}
						else if (!team.active)
						{
							infoln(F("Setting team"));

							team.active = true;
							team.rfid = tag;
							team.name = "";
							team.detected = false;
							i2c(I2C_OUT_RFID, team.rfid);
							clearMessage();
						}
						else
						{
							logln(F("Two players in game already"));
							// Two players already
							setMessage(text_reset1, text_reset2, 1000);
						}
					}
          		}
			}
	    }
		else if (rfidEnabled && rfidTagSeen == true && (now - nowLastRfid) >= RFID_REMOVED_TIME)
		{
			rfidTagSeen = false;
		}

		// Buttons
		for (int i = 0; i < 2; i++)
		{
			buttonValues[i] = digitalRead(buttons[i]);
		
			if (buttonValues[i] == LOW && prevButtonValues[i] == HIGH && (millis() - timeButtonReleased[i]) > BUTTON_DEBOUNCE_DELAY)
			{
				timeButtonPressed[i] = millis();
				if ((i == 0 || i == 1) && (!offline || (player.active || team.active)))
				{
					int otherButton = !i;
					if (timeButtonPressed[otherButton] >= millis() - BUTTON_HOLD_TIME)
					{
						logln(F("Reset started"));
						reseting = true;
						resetStart = millis();
						lcd.clear();
						lcd.setCursor(0, 0);
						text_reseting.print(lcd);
					}
				}
			}

			//if (buttonVal == HIGH && buttonLast == LOW && (millis() - btnDnTime) > long(debounce))
			if (timeButtonReleased[i] < millis() - BUTTON_DEBOUNCE_DELAY && buttonValues[i] == HIGH && prevButtonValues[i] == LOW && (millis() - timeButtonPressed[i]) > BUTTON_DEBOUNCE_DELAY)
			{
				if (!ignoreRelease[i] || (offline || (player.active | team.active)))
				{
					if (millis() > timeButtonReleased[i] + BUTTON_MIN_TIME)
					{
						if (i == 0 && currentScore < 10)
						{
							currentScore++;
							infoln(F("Increasing score"));
							// Plus
							i2c(I2C_OUT_ADD_POINT); // Increase score
							playSoundFx(snd_add);
						}
						if (i == 1 && currentScore > 0)
						{
							currentScore--;
							infoln(F("Decreasing score"));
							i2c(I2C_OUT_REMOVE_POINT); // Decrease score
							playSoundFx(snd_remove);
						}
					}
				}
				ignoreRelease[i] = false;
				timeButtonReleased[i] = millis();
				timeButtonPressed[i] = 0;
				buttonHold[i] = false;
				boolean wasReseting = reseting;
				reseting = false;
				if (wasReseting) updateScreen(); // Update with "swipe" message
			}
			else if (!buttonHold[i] && buttonValues[i] == LOW && (millis() - timeButtonPressed[i]) > BUTTON_HOLD_TIME)
			{
				ignoreRelease[i] = true;
				buttonHold[i] = true;
				if (i == 0 || i == 1) // Plus or minus
				{
					int otherButton = !i;
					if (!offline && buttonHold[otherButton])
					{
						// Both buttons pressed
						resetPlayers();
					}
				}
			}

			prevButtonValues[i] = buttonValues[i];
		}
	}

	if (reseting)
	{
		float p = (float)(millis() - resetStart) / (float)BUTTON_HOLD_TIME * 100.0f;
		float a = 16.0f / 100.0f * p;
		lcd.setCursor(0, 1);
		for (int i = 1; i < a; i++)
		{
			lcd.print("x");
		}
	}

	if (loading && millis() >= previousLoadingUpdate + LOADER_SPEED)
	{
		previousLoadingUpdate = millis();
		lcd.setCursor(0, 1);
		lcd.write(loaderBallPosition < 2 ? 1 : 0);

		for (byte i = 0; i < 14; i++)
		{
			// lcd.setCursor(i, 1);
			if (loaderBallPosition >= i * 6 && loaderBallPosition < (i + 1) * 6)
			{
				lcd.write(loaderBallPosition - i * 6 + 2);
			}
			else
			{
				lcd.write(" ");
			}
		}
		lcd.write(loaderBallPosition > 82 ? 0 : 1);

		loaderBallPosition += loaderBallAdvancing ? 1 : -1;
		if (loaderBallPosition < 1 || loaderBallPosition > 83) loaderBallAdvancing = !loaderBallAdvancing;
	}

	if (Serial.available()) {
		char c = Serial.read();

		if (c == 'm')
		{
			Serial.print(F("Available memory: "));
			Serial.println(freeMemory(), DEC);
		}

		while (Serial.available()) Serial.read();
	}

	if (message.active && millis() > message.timeout)
	{
		clearMessage();
	}

	// sound fx
	if (playingSoundFx)
	{
		// first, get note duration, if available
		num = 0;
		while(isdigit(*currentSoundFx))
		{
			num = (num * 10) + (*currentSoundFx++ - '0');
		}

		if (num) duration = wholenote / num;
		else duration = wholenote / default_dur;  // we will need to check if we are a dotted note after

		// now get the note
		note = 0;

		switch(*currentSoundFx)
		{
			case 'c':
				note = 1;
				break;
			case 'd':
				note = 3;
				break;
			case 'e':
				note = 5;
				break;
			case 'f':
				note = 6;
				break;
			case 'g':
				note = 8;
				break;
			case 'a':
				note = 10;
				break;
			case 'b':
				note = 12;
				break;
			case 'p':
				default:
				note = 0;
		}
		currentSoundFx++;

		// now, get optional '#' sharp
		if (*currentSoundFx == '#')
		{
			note++;
			currentSoundFx++;
		}

		// now, get optional '.' dotted note
		if (*currentSoundFx == '.')
		{
			duration += duration/2;
			currentSoundFx++;
		}

		// now, get scale
		if (isdigit(*currentSoundFx))
		{
			scale = *currentSoundFx - '0';
			currentSoundFx++;
		}
		else
		{
			scale = default_oct;
		}

		if (*currentSoundFx == ',')
			currentSoundFx++;       // skip comma for next note (or we may be at the end)

		// now play the note

		if (note)
		{
			music.play(notes[(scale - 4) * 12 + note]);
			delay(duration);
			music.stop();
		}
		else
		{
			delay(duration);
		}

		if (!*currentSoundFx || *currentSoundFx == 0)
		{
			playingSoundFx = false;
			infoln(F("Sound FX completed"));
		}
	}
}

void updateCurrentRfidTag(byte *tagNew)
{
  // only print changed value     
  if (!equals(tagNew, rfidTagCurrent))
  {
    saveTag(tagNew, rfidTagCurrent);
    
    byte i = 0;
    
    tag = "";
    
    // STX
    Serial.print(0x02);
    tag += 0x02;
    
    for (i=0; i<5; i++) 
    {
      if (rfidTagCurrent[i] < 16)
      {
        Serial.print(F("0"));
        tag += "0";
      }
      Serial.print(rfidTagCurrent[i], HEX);
      tag += String(rfidTagCurrent[i], HEX);
//      if (i < 4) tag += "-";
    }
    
    // ETX
    Serial.print(0x03);
    tag += 0x03;

    Serial.println();
    
    tag.toUpperCase();
  }
}

/**
 * read data from rfid reader
 * @return rfid tag number
 *
 * Based on code by BARRAGAN, HC Gilje, djmatic, Martijn
 * http://www.arduino.cc/playground/Code/ID12 
 */
boolean readID12(byte *code)
{
  boolean result = false;
  byte val = 0;
  byte bytesIn = 0;
  byte tempbyte = 0;
  byte checksum = 0;
  
  // read 10 digit code + 2 digit checksum
  while (bytesIn < RFID_TAG_INPUT) 
  {                        
    if ( rfidSerial.available() > 0) 
    { 
      val = rfidSerial.read();

      // if CR, LF, ETX or STX before the 10 digit reading -> stop reading
      if ((val == 0x0D)||(val == 0x0A)||(val == 0x03)||(val == 0x02)) break;
      
      // Do Ascii/Hex conversion:
      if ((val >= '0') && (val <= '9')) 
        val = val - '0';
      else if ((val >= 'A') && (val <= 'F'))
        val = 10 + val - 'A';


      // Every two hex-digits, add byte to code:
      if (bytesIn & 1 == 1) 
      {
        // make some space for this hex-digit by
        // shifting the previous hex-digit with 4 bits to the left:
        code[bytesIn >> 1] = (val | (tempbyte << 4));
        
        // If we're at the checksum byte, Calculate the checksum... (XOR)
        if (bytesIn >> 1 != RFID_TAG_LENGTH) checksum ^= code[bytesIn >> 1]; 
      } 
      else 
      {
        // Store the first hex digit first...
        tempbyte = val;                           
      }

      // ready to read next digit
      bytesIn++;                                
    } 
  }

  // read complete
  if (bytesIn == RFID_TAG_INPUT) 
  { 
    // valid tag
    if (code[5] == checksum) result = true; 
  }

  // reset id-12
  updateID12(true);


  return result;
}

/**
 * update reset state of the rfid reader
 */
void updateID12(boolean reset_)
{
  // reset is disabled
  if (RESET_ENABLED == 0 && rfidEnabled == true) return;

  // don't reset, just check if the id-12 should be enabled again 
  if (reset_ == false)
  {
    // current time
    unsigned int now = millis();

    // id-12 is disabled and ( reset period is over or initial id-12 startup )
    if (rfidEnabled == false && ((now - nowReset) >= RESET_TIME || nowReset == 0)) 
    { 
      rfidEnabled = true;
    }
  }
  // reset rfid reader
  else
  {
    nowReset = millis();
    rfidEnabled = false;  
  }
}

/**
 * clear rfid tags
 */
void clearTag(byte *arr, byte len)
{
  byte i;
  for (i=0; i < len ;i++) arr[i] = 0;
}

/**
 * save rfid tag
 */
void saveTag(byte *tagIn, byte *tagOut)
{
  byte i;
  for (i=0; i < RFID_TAG_LENGTH ;i++) tagOut[i] = tagIn[i];
}


/**
 * compare 2 rfid tags
 */
boolean equals(byte *tag1, byte *tag2)
{
  boolean result = false;
  byte j;
  
  for (j=0; j < RFID_TAG_LENGTH ;j++) 
  {
    if (tag1[j] != tag2[j]) break;
    else if (j == RFID_TAG_LENGTH-1) result = true;
  }    
  return result;
}