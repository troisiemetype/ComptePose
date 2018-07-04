/*
 * This Arduino sketch is for checking that soft and hardware is ok, and set initial values into eeprom.
 * for a timer to a photographic insolation system
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
	// Checking brightness against default value, that is 8.
	// If not equal to 8, set default values for eeprom.
	brightness = EEPROM.read(brightAdd);
	if(brightness != 8){
		// Write default values for brightness, bell type and belle length
		EEPROM.write(brightAdd, 8);
		EEPROM.write(bellTAdd, 1);
		EEPROM.write(bellLAdd, 5);
		// Set initial time to set to memory
		uint8_t seconds = 15;
		uint8_t minutes = 0;
		// For each of the available time memory, add 15 seconds to previous time, and store.
		for(uint8_t i = 0; i < 64; ++i){
			EEPROM.write((memAdd + 2 * i), minutes);
			EEPROM.write((memAdd + 2 * i + 1), seconds);
			if((seconds += 15) > 59){
				seconds = 0;
				++minutes;
			}
		}
	}
}

void loop(){

}