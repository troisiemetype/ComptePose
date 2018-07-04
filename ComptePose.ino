/*
 * This Arduino sketch is for adding a timer to a photographic insolation system
 * Copyright (C) 2017  Pierre-Loup Martin
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Main program for v1 of the board, using atmega 328p.
 */

#include <EEPROM.h>

#include "Timer.h"
#include "SevenSegments.h"
#include "Encoder.h"
#include "PushButton.h"

/* NOTA
 * These values are for v1 and above, with atmega 328P.
 * Pin for optionnal switch 1: 			4 (PORT D 4)
 * Pin for switch 2: 					5 (PORT D 5)
 * Pin for switch 3 (encoder switch): 	6 (PORT D 6)
 *
 * Pins for encoder A: 					2 (PORT D 2) 
 * Pins for encoder B: 					3 (PORT D 3) 
 *
 * Pins for max7219 led driver:
 * 						Data:			14/A0 (PORT C 0)
 * 						Load:			15/A1 (PORT C 1)
 * 						Clk:			16/A2 (PORT C 2)
 *
 * Pin for output to relay:				8 (PORT B 0)
 *
 * Pin for piezo:						9 & 10 (PORT B 1 & 2) (hardware Timer 1 channel A & B, OC1A & OC1B)
 */

/*
 * This program is based on a state machine, with the following states:
 ** SETTING
 *	is used to set a time before to run an exposure.
 *
 ** RUN
 *	is when the exposure is beeing done, and the timer down counting to 0.
 *
 ** PAUSE
 *	is when the exposure is beeing done, but has been pause. Trace is kept of the time elapsed,
 *	and exposure can be resume.
 *
 ** MENU
 *	is for saving time to, or reading from memory, and set the diplay light intensity.
 *
 * The main loop reads main button (SW2), and call button management function if needed,
 * or dispatch to main subfunctions according to the current state.
 * See functions for more information on what happens
 */

// NOTA: default time precision: 10min = 10:03 in real life.
// TODO: see if we can fine tune it.

// Const for naming pins
const uint8_t SW1 = 4;			// This is optional: trace is on PCB, but button is not mounted.
const uint8_t SW2 = 5;
const uint8_t SW3 = 6;

const uint8_t ENCA = 2;
const uint8_t ENCB = 3;

const uint8_t DATA = 14;
const uint8_t LOAD = 15;
const uint8_t CLK = 16;

const uint8_t OUT = 8;
const uint8_t BELL1 = 9;
const uint8_t BELL2 = 10;

// EEPROM addresses
const uint16_t brightAdd = 0;
const uint16_t bellTAdd = 1;
const uint16_t bellLAdd = 2;
const uint16_t memAdd = 3;

// Max exposure values in eeprom
const uint8_t maxStore = 64;

const uint8_t maxType = 8;
const uint8_t maxTime = 90;

// these are the timers used, for lighting duration and run and pause blinking
// NOTA: two timers are used for blinking. One while running, the other while pausing.
// That way when we pause, we can blink immediately on a regular basis,
// and when exiting pause, running blinking stay synch with countdown.
Timer timerMain;
Timer timerDots;
Timer timerPause;
Timer timerBeep1;
Timer timerBeep2;
Timer timerBeep3;

// This are the classes that manage display
// ledDriver is usd for direct addressing
SevenSegments ledDriver;
// clock is used for time adressing, and setting minutes and seconds
SevenSegmentsClock clock;

// Class for encoder
Encoder encoder;

// And push button.
// Uncomment if you will to use the optionnal button placed left of the display
//PushButton sw1;
PushButton sw2;
PushButton sw3;

// State of the clock. Used for blinking when paused.
bool enable = LOW;

// Some vars to store the time set.
int8_t minutes, seconds = 0;
int16_t duration = 0;

// Brightness value
uint8_t brightness = 16;

// Bell enable
uint8_t bellType = 0;
uint8_t bellLength = 0;
uint16_t bellLoops = 0;


// Timer Top value
uint16_t counter = 0;

// Current mem
uint8_t currentMem = 1;

// These are the several machine state
enum state_t{
	SETTING = 0,
	RUN,
	PAUSE,
	BEEPING,
	MENU,
};

// And initial state
state_t state = SETTING;

// These are the states used for menu
enum menuState_t{
	MEM_RECALL = 0,
	MEM_STORE,
	BRIGHTNESS,
	SOUND_TYPE,
	SOUND_LENGTH,
};

// And the current state
// uint8_t is used instead of menuState_t for compare in if structures
uint8_t menuState = MEM_RECALL;


// Start compte pose.
void setup(){

//	eepromInit();

	// init the timers
	timerMain.init();
	timerDots.init();
	timerPause.init();
	timerBeep1.init();
	timerBeep2.init();
	timerBeep3.init();

	// Set delay for blink and beep timers
	timerDots.setDelay(500);
	timerPause.setDelay(500);

	// Read the values from eeprom
	brightness = EEPROM.read(brightAdd);
	bellType = EEPROM.read(bellTAdd);
	bellLength = EEPROM.read(bellLAdd);
//	bellLength = 0;

	setBeepTimers(bellType);

	// Set the ledDriver and the clock
	ledDriver.begin(DATA, LOAD, CLK, 4);
	ledDriver.setIntensity(brightness - 1);

	clock.begin(&ledDriver);
	clock.setDots();

	// init the encoder
	// QUAD_STEP and reverse() may not be needed, following encoder used.
	encoder.begin(ENCA, ENCB, Encoder::QUAD_STEP);
//	encoder.reverse();

	// Init the push buttons
//	sw1.begin(SW1, INPUT_PULLUP);
	sw2.begin(SW2, INPUT_PULLUP);
	sw3.begin(SW3, INPUT_PULLUP);

	// Set the command pin
	pinMode(OUT, OUTPUT);
	digitalWrite(OUT, HIGH);

	// Setup the timer and pins for the ring.
	beepSetup();
}

// Main loop: read buttons, dispatch to state function
void loop(){
	if(sw2.update()) manageSW2();
	if(sw3.update()) manageSW3();
	switch(state){
		case SETTING:
			setting();
			break;
		case RUN:
			running();
			break;
		case PAUSE:
			pause();
			break;
		case BEEPING:
			beeping();
			break;
		case MENU:
			menuNaviguate();
			break;
		default:
			setting();
			break;
	}

}

// Manage SETTING state.
// Read the encoder and adapt time duration
void setting(){
	if(encoder.update()){
		setTime(encoder.getStep());
	}
	clock.update();
}

// Make the beep beeps.
void beeping(){
	if(timerBeep1.update()){
		timerBeep2.start();
		beepOn();
	}
	if(timerBeep2.update()){
		beepOff();
	}
	if(timerBeep3.update()){
		state = SETTING;
		timerBeep2.stop();
		timerBeep1.stop();
		beepOff();
		stop();

	}
}

// Manage RUN state
// Make the points blink, update display, and call stop function when delay elapsed
void running(){
	// Blink the two center dots
	if(timerDots.update()){
		enable = !enable;
		clock.setDots(enable);
	}

	// Update display for minutes and seconds
	clock.setMinutes(timerMain.getRMinutes());
	clock.setSeconds(timerMain.getRSecondsM());
	clock.update();

	// Manage delay elapsed
	if(timerMain.update()){
		if(bellLength > 0){
			timerBeep1.start(bellLoops);
			timerBeep2.start();
			timerBeep3.start();
			beepOn();
			state = BEEPING;
		} else {
			state = SETTING;
		}
		stop();
	}
}

// Pause state management
// Blink the whole display
void pause(){
	// Timer update has to be called to update end time, otherwise it wouldn't really pause
	timerMain.update();
	// Make the display blink
	if(timerPause.update()){
		enable = !enable;
		clock.enable(enable);
	}
	clock.update();
}

// Push button manager
// Attached to SW2, which is placed on right of rotary encoder
void manageSW2(){
	// Stop beeps if beeping.
	if(state == BEEPING){
		timerBeep1.stop();
		timerBeep2.stop();
		timerBeep3.stop();
		beepOff();
		state = SETTING;
		return;
	}

	// Stop the timer if long pressed
	if(sw2.isLongPressed()){
		stop();
		state = SETTING;

	// If simple pressed, dispatch following current state
	} else if(sw2.justPressed()){
		switch(state){
			// If state is setting, then we launch the timer with the delay set
			case SETTING:
				// Change state
				state = RUN;
				// Set the display again: when finishing a countdown, the set time is display again
				clock.setMinutes(minutes);
				clock.setSeconds(seconds);
				// Set timer delay
				timerMain.setMinutesSeconds(minutes, seconds);
				// Turn output high
				digitalWrite(OUT, LOW);
				// Start main timer and blink timer
				timerMain.start();
				timerDots.start(Timer::LOOP);
				break;
			// If state is running, then we pause the timer
			case RUN:
				// Pause both main and blink timer
				timerMain.pause();
				timerDots.pause();
				// Disable display, so user see instantly that timer is paused
				enable = LOW;
				clock.enable(enable);
				// Start blinking diplay
				timerPause.start(Timer::LOOP);
				// Turn output LOW.
				digitalWrite(OUT, HIGH);
				state = PAUSE;
				break;
			// If state is paused, we exit pause.
			case PAUSE:
				// Exit pause for main and running-blinking timers
				timerMain.pause();
				timerDots.pause();
				// Stop pause-blinking timer
				timerPause.stop();
				// Enable display (so we are sure that it doesn't stay shut after pause!)
				enable = HIGH;
				// Turn output high again
				digitalWrite(OUT, LOW);
				state = RUN;
				clock.enable(true);
				break;
			default:
				setting();
				break;
		}
	}
}

// Manage switch 3 (encoder button)
void manageSW3(){
	// Stop beeps if beeping.
	if(state == BEEPING){
		timerBeep1.stop();
		timerBeep2.stop();
		timerBeep3.stop();
		beepOff();
		state = SETTING;
		return;
	}

	if(sw3.justPressed() && state == SETTING){
		state = MENU;
		ledDriver.clrAll();
		menuNaviguate();
	} else if(sw3.isLongPressed() && (state == RUN || state == PAUSE)){
		stop();
		state = SETTING;
	}
}

// Manage countdown end or manual stop.
void stop(){
	//Turn output low.
	digitalWrite(OUT, HIGH);
	//Stop the main timer. Needed when stopped by user before countdown end.
	timerMain.stop();

	//Enable clock. Needed if we stop timer when pausing
	//Update display with time previously set
	clock.enable();
	clock.setDots(true);
	clock.setMinutes(minutes);
	clock.setSeconds(seconds);
}

// Timer setter. Called when encoder is moved.
void setTime(int8_t value){
	// Multiple choices for time increment: having increments of 1s is of no interest when counting minutes.
	// TODO: see if it could be an "encoder acceleration":
	// When encoder is turn longer that a preset delay, increment would increase
	if(duration >= 600){
		setSeconds(value * 30);
	} else if(duration >= 300){
		setSeconds(value * 15);
	} else if (duration >= 120){
		setSeconds(value * 10);
	} else if (duration >= 30){
		setSeconds(value * 5);
	} else {
		setSeconds(value);
	}

	// Compute the total seconds
	duration = 60 * minutes + seconds;
}

// set seconds
void setSeconds(int8_t value){
	seconds += value;
	//Manage over and under-run
	if(seconds >= 60){
		if(setMinutes(1)){
			seconds %= 60;
		} else {
			seconds = 59;
		}

	} else if(seconds < 0){
		if(setMinutes(-1)){
			seconds += 60;
		} else {
			seconds = 0;
		}
	}
	clock.setSeconds(seconds);
}

// Set minutes. Called by setSeconds(int8_t).
// Return a bool, to manage min and max timing
// Min is 00:00, Max is 99:59
bool setMinutes(int8_t value){
	minutes += value;
	if(minutes >= 99){
		minutes = 99;
		return false;
	} else if(minutes < 0){
		minutes = 0;
		return false;
	} else {
		clock.setMinutes(minutes);
		return true;
	}

}

// Set the three timers used by the beep.
void setBeepTimers(int8_t type){
	uint16_t delay1 = 0;
	uint16_t delay2 = 0;
	uint16_t delay3 = 0;

	switch(type){
		case 0:
			delay1 = 100;
			delay2 = 100;
			delay3 = 0;
			break;
		case 1:
			delay1 = 1000;
			delay2 = 500;
			delay3 = 0;
			break;
		case 2:
			delay1 = 500;
			delay2 = 250;
			delay3 = 0;
			break;
		case 3:
			delay1 = 100;
			delay2 = 50;
			delay3 = 0;
			break;
		case 4:
			delay1 = 1000;
			delay2 = 900;
			delay3 = 0;
			break;
		case 5:
			delay1 = 500;
			delay2 = 450;
			delay3 = 0;
			break;
		case 6:
			delay1 = 2000;
			delay2 = 1500;
			delay3 = 0;
			break;
		case 7:
			delay1 = 1000;
			delay2 = 50;
			delay3 = 0;
			break;
		case 8:
			delay1 = 500;
			delay2 = 40;
			delay3 = 0;
			break;
		default:
			delay1 = 1000;
			delay2 = 500;
			delay3 = 0;
			break;
	}

	timerBeep1.setDelay(delay1);
	timerBeep2.setDelay(delay2);

	bellLoops = ((uint32_t)(bellLength * 1000) / delay1);

	timerBeep3.setDelay(bellLoops * delay1);
}

// Display the main menu items
void menuNaviguate(){
	bool menuClicked = false;
	do{
		// Get en encoder update
		if(encoder.update()){
			int8_t step = encoder.getStep();
			if(step > 0 && menuState < SOUND_LENGTH) menuState++;
			if(step < 0 && menuState > 0) menuState--;
		}

		// Get a push from the right button
		if(sw2.update()){
			if(sw2.justPressed()) menuClicked = true;
		}

		// Get a push from the encoder button
		if(sw3.update()){
			if(sw3.justPressed()) state = SETTING;
		}

		// Display the right menu item
		ledDriver.clrAll();
		switch (menuState){
			case MEM_STORE:
				ledDriver.setText(F("-MEM"));
				if(menuClicked) storeTime();
				break;
			case MEM_RECALL:
				ledDriver.setText(F("MEM-"));
				if(menuClicked) recallTime();
				break;
			case BRIGHTNESS:
				ledDriver.setText(F("LUM"));
				if(menuClicked) setBrightness();
				break;
			case SOUND_TYPE:
				ledDriver.setText(F("BL T"));
				if(menuClicked) setBellType();
				break;
			case SOUND_LENGTH:
				ledDriver.setText(F("BL L"));
				if(menuClicked) setBellLength();
				break;
			default:
				break;
		}

		menuClicked = false;

	} while(state == MENU);
	ledDriver.clrAll();
	clock.setDots();

}

// Store a time in eeprom
void storeTime(){
	uint8_t mem = currentMem;
	ledDriver.clrAll();
	for(;;){
		sw2.update();
		sw3.update();
		if(encoder.update()){
			mem += encoder.getStep();
			if(mem < 1) mem = 1;
			if(mem > maxStore) mem = maxStore;
		}

		ledDriver.setText(F("-M"));
		ledDriver.setDigit(3, mem % 10);
		ledDriver.setDigit(2, mem / 10);

		if(sw2.justPressed()){
			currentMem = mem;
			EEPROM.update((memAdd + 2 * mem), minutes);
			EEPROM.update((memAdd + 2 * mem + 1), seconds);
			return;
		}

		if(sw3.justPressed()) return;

	}
}

// Get a stored time from eeprom
void recallTime(){
	uint8_t mem = currentMem;
	ledDriver.clrAll();
	for(;;){
		sw2.update();
		sw3.update();
		if(encoder.update()){
			mem += encoder.getStep();
			if(mem < 1) mem = 1;
			if(mem > maxStore) mem = maxStore;
		}

		ledDriver.setText(F("M-"));
		ledDriver.setDigit(3, mem % 10);
		ledDriver.setDigit(2, mem / 10);

		if(sw2.justPressed()){
			currentMem = mem;
			currentMem = mem;
			minutes = EEPROM.read((memAdd + 2 * mem));
			seconds = EEPROM.read((memAdd + 2 * mem + 1));
			clock.setSeconds(seconds);
			clock.setMinutes(minutes);
			state = SETTING;
			return;
		}

		if(sw3.justPressed()) return;

	}
}

// Set display brightness
void setBrightness(){
	uint8_t tempBrightness = brightness;
	ledDriver.clrAll();
	for(;;){
		sw2.update();
		sw3.update();
		if(encoder.update()){
			tempBrightness += encoder.getStep();
			if(tempBrightness < 1) tempBrightness = 1;
			if(tempBrightness > 16) tempBrightness  =16;
		}

		ledDriver.setChar(0, 'L');
		ledDriver.setDigit(3, tempBrightness % 10);
		ledDriver.setDigit(2, tempBrightness / 10);
		ledDriver.setIntensity(tempBrightness - 1);

		if(sw2.justPressed()){
			brightness = tempBrightness;
			EEPROM.update(brightAdd, brightness);
			break;
		}

		if(sw3.justPressed()) break;

	}

	ledDriver.setIntensity(brightness - 1);

}


// Set the bell type
void setBellType(){
	uint8_t type = bellType;
	ledDriver.clrAll();
	ledDriver.setText(F("BT"));
	ledDriver.setDigit(3, type);

	for(;;){
		sw2.update();
		sw3.update();

		if(encoder.update()){
			if(encoder.getStep() > 0){
				if(++type > maxType) type = maxType;
			} else {
				if(type > 0) --type;
			}
			ledDriver.setText(F("BT"));
			ledDriver.setDigit(3, type);
		}

		if(sw2.justPressed()){
			bellType = type;
			EEPROM.update(bellTAdd, type);
			setBeepTimers(bellType);
			return;
		}

		if(sw3.justPressed()) return;
	}
}

// Turn the bell on of off
void setBellLength(){
	uint8_t length = bellLength;
	ledDriver.clrAll();
	ledDriver.setText(F("BL"));
	ledDriver.setDigit(3, length % 10);
	ledDriver.setDigit(2, length / 10);

	for(;;){
		sw2.update();
		sw3.update();

		if(encoder.update()){
			if(encoder.getStep() > 0){
				if(++length > maxTime) length = maxTime;
			} else {
				if(length > 0) --length;
			}
			ledDriver.setText(F("BL"));
			ledDriver.setDigit(3, length % 10);
			ledDriver.setDigit(2, length / 10);
		}

		if(sw2.justPressed()){
			bellLength = length;
			EEPROM.update(bellLAdd, length);
			setBeepTimers(bellType);
			return;
		}

		if(sw3.justPressed()) return;
	}
}

void beepSetup(){
	pinMode(BELL1, OUTPUT);
	pinMode(BELL2, OUTPUT);

	digitalWrite(BELL1, LOW);
	digitalWrite(BELL2, HIGH);

	counter = 8000;

	TCCR1A = 0b00000000;
	TCCR1B = 0b00001000;
	TIMSK1 = 0b00000110;
	OCR1A = counter;
	OCR1B = counter;
}


ISR(TIMER1_COMPA_vect){
	PINB = 0b00000010;
}

ISR(TIMER1_COMPB_vect){
	PINB = 0b00000100;
}

void beepOn(){
	TCCR1B |= 1;
}

void beepOff(){
	TCCR1B &= ~1;
}

// init the EEPROM values. Only used on first run.
void eepromInit(){
	// Write a 8 value for luminosity
	EEPROM.write(brightAdd, 8);
	EEPROM.write(bellTAdd, 1);
	EEPROM.write(bellLAdd, 5);
	// Write default 0:00 time value in each EEPROM area.
	for(uint8_t i = 0; i < maxStore; ++i){
		EEPROM.write(memAdd + i, 0);
		EEPROM.write(memAdd + i + 1, 0);
	}
}