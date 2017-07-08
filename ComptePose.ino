/*
 * This Arduino sketch is for adding a timer to a photographic insolation system
 * Copyright (C) 2016  Pierre-Loup Martin
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

#include <EEPROM.h>

#include "Timer.h"
#include "SevenSegments.h"
#include "Encoder.h"
#include "PushButton.h"

/* NOTA
 * Pin for optionnal switch 1: 			4 (PORT D 4)
 * Pin for switch 2: 					5 (PORT D 5)
 * Pin for switch 3 (encoder switch): 	6 (PORT D 6)
 *
 * Pins for encoder A: 					8 (PORT B 0) 
 * Pins for encoder B: 					9 (PORT B 1) 
 *
 * Pins for max7219 led driver:
 * 						Data:			14/A0 (PORT C 0)
 * 						Load:			15/A1 (PORT C 1)
 * 						Clk:			16/A2 (PORT C 2)
 *
 * Pin for output to relay:				3 (PORT D 3)
 *
 * Pin for piezo:						10 (PORT B 2) (hardware Timer 1 channel B, OC1B)
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

const uint8_t ENCA = 8;
const uint8_t ENCB = 9;

const uint8_t DATA = 14;
const uint8_t LOAD = 15;
const uint8_t CLK = 16;

const uint8_t OUT = 3;
const uint8_t BELL = 10;

// EEPROM addresses
const uint16_t brightAdd = 0;
const uint16_t bellAdd = 1;
const uint16_t memAdd = 2;

// Max exposure values in eeprom
const uint8_t maxStore = 64;

// these are the timers used, for lighting duration and run and pause blinking
// NOTA: two timers are used for blinking. One while running, the other while pausing.
// That way when we pause, we can blink immediately on a regular basis,
// and when exiting pause, running blinking stay synch with countdown.
Timer timerMain;
Timer timerDots;
Timer timerPause;
Timer timerBeep;

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
bool enableBell = true;

// Current mem
uint8_t currentMem = 1;

// These are the several machine state
enum state_t{
	SETTING = 0,
	RUN,
	PAUSE,
	MENU,
};

// And initial state
state_t state = SETTING;

// These are the states used for menu
enum menuState_t{
	MEM_RECALL = 0,
	MEM_STORE,
	BRIGHTNESS,
	SOUND,
};

// And the current state
uint8_t menuState = MEM_RECALL;


// Start compte pose.
void setup(){

	// init the timers
	timerMain.init();
	timerDots.init();
	timerPause.init();
	timerBeep.init();

	// Set delay for blink and beep timers
	timerDots.setDelay(500);
	timerPause.setDelay(500);
	timerBeep.setDelay(1000);

	// Read the values from eeprom
	brightness = EEPROM.read(brightAdd);
	enableBell = EEPROM.read(bellAdd);

	// Set the ledDriver and the clock
	ledDriver.begin(DATA, LOAD, CLK, 4);
	ledDriver.setIntensity(brightness - 1);

	clock.begin(&ledDriver);
	clock.setDots();

	// init the encoder
	// QUAD_STEP and reverse() may not be needed, following encoder used.
	encoder.begin(ENCA, ENCB, Encoder::QUAD_STEP);
	encoder.reverse();

	// Init the push buttons
//	sw1.begin(SW1, INPUT_PULLUP);
	sw2.begin(SW2, INPUT_PULLUP);
	sw3.begin(SW3, INPUT_PULLUP);

	// Set the command pin
	pinMode(OUT, OUTPUT);
	digitalWrite(OUT, HIGH);
}

// Main loop: read buttons, dispatch to state function
void loop(){
//	if(timerBeep.update()) tone(BELL, 1760, 500);
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
// TODO: add access to eeprom for reading and saving reference times
void setting(){
	if(encoder.update()){
		setTime(encoder.getStep());
	}
	clock.update();
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
//		timerBeep.start(5);
//		tone(11, 432, 5);
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
	// Stop the timer if long pressed
	if(sw2.isLongPressed()){
		stop();

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
	if(sw3.justPressed() && state == SETTING){
		state = MENU;
		ledDriver.clrAll();
		menuNaviguate();
	} else if(sw3.isLongPressed() && (state == RUN || state == PAUSE)){
		stop();
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

	state = SETTING;
//	if(enableBell) timerBeep.start(10);
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

// Display the main menu items
void menuNaviguate(){
	bool menuClicked = false;
	do{
		// Get en encoder update
		if(encoder.update()){
			int8_t step = encoder.getStep();
			if(step > 0 && menuState < BRIGHTNESS) menuState++;
			if(step < 0 && menuState > 0) menuState--;
		}

		// Loop trough menu items
		if(menuState > BRIGHTNESS) menuState = BRIGHTNESS;

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
			case SOUND:
				ledDriver.setText(F("BELL"));
//				if(menuClicked) setBell();
				break;
			default:
				break;
		}

		menuClicked = false;

	} while(state == MENU);
	ledDriver.clrAll();

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
			return;
		}

		if(sw3.justPressed()) return;

	}

	ledDriver.setIntensity(brightness - 1);

}

/*
// Turn the bell on of off
void setBell(){
	bool enable = enableBell;
	ledDriver.clrAll();
	for(;;){
		sw2.update();
		sw3.update();

		if(encoder.update()){
			if(encoder.getStep() > 0){
				enable = true;
				ledDriver.setChar(0, 'B');
				ledDriver.setDigit(3, 1);
			} else {
				enable = false;
				ledDriver.setChar(0, 'B');
				ledDriver.setDigit(3, 0);
			}
		}

		if(sw2.justPressed()){
			enableBell = enable;
			EEPROM.update(bellAdd, enableBell);
			return;
		}

		if(sw3.justPressed()) return;
	}
}
*/