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

#include "Timer.h"
#include "SevenSegmentsDisplay.h"
#include "Encoder.h"
#include "PushButton.h"

/* NOTA:
 * Pin for button: 10
 * Pins for encoder: 2 & 3
 * Pins for shift register: 4 (Data) & 5 (clock)
 * Pins for display anode: 6, 7, 8 and 9
 */


Timer timerBlink;
Timer timer;

FourDigits display;

Encoder encoder;

PushButton button;

bool ledState = LOW;

int8_t minutes, seconds = 0;
int16_t duration = 0;

enum state_t{
	SETTING = 0,
	RUN,
	PAUSE,
};

state_t state = SETTING;

const uint8_t output = 13;

void setup(){

	timerBlink.init();
	timer.init();

	timerBlink.setDelay(500);

	display.init(6, 7, 8, 9);

	encoder.begin(2, 3);
	encoder.reverse();

	button.begin(10, INPUT_PULLUP);

	pinMode(output, OUTPUT);
	digitalWrite(output, LOW);

	Serial.begin(115200);

}

void loop(){
	if(button.update()) manageButton();
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
		default:
			setting();
			break;
	}

}

void setting(){
	if(encoder.update()){
		setTime(encoder.getStep());
	}
	display.update();
}

void running(){
	if(timerBlink.update()){
		ledState = !ledState;
		display.setPoint(ledState);
	}

	display.setMinutes(timer.getRMinutes());
	display.setSeconds(timer.getRSecondsM());
	display.update();

	if(timer.update()){
		stop();
	}
}

void pause(){
	timer.update();
	if(timerBlink.update()){
		ledState = !ledState;
		display.enable(ledState);
	}
	display.update();
}

void setTime(int8_t value){
	if(duration >= 300){
		setSeconds(value * 15);
	} else if (duration >= 120){
		setSeconds(value * 5);
	} else {
		setSeconds(value);
	}

	duration = 60 * minutes + seconds;
}

void setSeconds(int8_t value){
	seconds += value;
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
	display.setSeconds(seconds);
}

bool setMinutes(int8_t value){
	minutes += value;
	if(minutes >= 99){
		minutes = 99;
		return false;
	} else if(minutes < 0){
		minutes = 0;
		return false;
	} else {
		display.setMinutes(minutes);
		return true;
	}

}

void manageButton(){
	if(button.isLongPressed()){
		stop();

	} else if(button.justPressed()){
		switch(state){
			case SETTING:
				state = RUN;
				display.setMinutes(minutes);
				display.setSeconds(seconds);
				timer.setMinutesSeconds(minutes, seconds);
				digitalWrite(output, HIGH);
				timer.start();
				timerBlink.start(Timer::LOOP);
				break;
			case RUN:
				timer.pause();
				digitalWrite(output, LOW);
				state = PAUSE;
				break;
			case PAUSE:
				timer.pause();
				digitalWrite(output, HIGH);
				state = RUN;
				display.enable(true);
				break;
			default:
				setting();
				break;
		}
	}

}

void stop(){
	timer.stop();

	display.enable();
	display.setPoint(true);
	display.setMinutes(minutes);
	display.setSeconds(seconds);

	digitalWrite(output, LOW);

	state = SETTING;

}