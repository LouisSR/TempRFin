/*
	Program for indoor wireless temperature and humidity sensor.

	Feather MO with RFM69HCW radio module
	SHT40 sensor
	Phototransistor
	Display
	

	Read temperature and humidity
	Read battery voltage
	Read luminosity
	Read battery voltage
	Read temperature and humidity  and battery voltage from RF 900MHz
	Display information
	Sleep

*/

//#include "ArduinoLowPower.h" //bug on the duration of the sleep: https://github.com/arduino-libraries/ArduinoLowPower/issues/18
#include <System.h>
#include <TimerCounter.h>
using namespace SAMD21LPE;

#include <SPI.h>
#include <RH_RF69.h> //RadioHead - modified RH_RF69.cpp line 182: disable CLKOUT to minimize the current consumption (see 3.2.2)

#include "Adafruit_SHT4x.h"

#include <Adafruit_GFX.h>
#include <Adafruit_SharpMem.h>
#include <Fonts/FreeSans54pt7b.h> //https://github.com/WaylandM/XOD_ArduinoFreeFontFile
#include <Fonts/FreeSans30pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSans16pt7b.h> //https://github.com/WaylandM/XOD_ArduinoFreeFontFile
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

//Pinout
#define BATTERY_VOLTAGE A7
#define ANALOG_ENABLE 12
#define LUMINOSITY A2
#define LUMINOSITY_ENABLE A3 //17

#define RF_CS 8
#define RF_RST 4
#define RF_IRQ 3

//Display
#define SHARP_SS   A5
#define SHARP_WIDTH 168
#define SHARP_HEIGHT 144
#define BLACK 0
#define WHITE 1

//RF
#define RF_FREQ 915.0
#define PACKET_SIZE 5
#define RF_INTERVAL 11000 //millisecond between emissions
#define TIMEOUT 2000 //reception timeout in millisecond

#define VOLTAGE_DIVIDER 66 // 3V3 * (15k+15k)/30k * 10 ; x10 to get results in deciVolts
#define ADC_RES 10 // 10bits

#define DEBUG

Adafruit_SHT4x sht4 = Adafruit_SHT4x();
RH_RF69 rf69(RF_CS, RF_IRQ); // Singleton instance of the radio driver
Adafruit_SharpMem display(&SPI, SHARP_SS, SHARP_HEIGHT, SHARP_WIDTH, 4000000);
TimerCounter timer;
unsigned long toc1, toc2, toc3, toc4, toc5;


void setup()
{
	pinMode(LED_BUILTIN, OUTPUT);
	digitalWrite(LED_BUILTIN, HIGH);
	pinMode(RF_RST, OUTPUT);
	pinMode(ANALOG_ENABLE, OUTPUT);
	pinMode(LUMINOSITY_ENABLE, OUTPUT);

#ifndef DEBUG
// Reduce active consumption
	System::disableClock(GCM_TCC2_TC3);
	System::disableClock(GCM_SERCOM0_CORE);
	System::disableClock(GCM_SERCOM1_CORE);
	System::disableClock(GCM_SERCOM2_CORE);
	System::disableClock(GCM_SERCOM3_CORE); //i2c
	System::disableClock(GCM_SERCOM4_CORE); //SPI
	System::disableClock(GCM_SERCOM5_CORE); //sercoms -100uA
#endif

	display.begin();
	display.clearDisplay(); //TODO: change to welcome display
	display_welcome();

#ifndef DEBUG
	delay(5000); //time to be able to upload a new sketch before entering sleep mode

	// configure low power clock generator to run at 1 kHz
  const byte GCLKGEN_ID_1K = 6;
  System::setupClockGenOSCULP32K(GCLKGEN_ID_1K, 4); // 2^(4+1) = 32 -> 1 kHz
	timer.enable(4, GCLKGEN_ID_1K, 1024, TimerCounter::DIV1, TimerCounter::RES16, 1000U, true); // configure timer counter to run at 1 kHz
  System::setSleepMode(System::STANDBY); // select MCU sleep mode STANDBY for timer wait

// Reduce active consumption
	Serial.end();
	System::disableClock(GCM_USB); //-800uA
#endif

	sht4.begin();
	sht4.setPrecision(SHT4X_LOW_PRECISION);
	sht4.setHeater(SHT4X_NO_HEATER);

	rf69.init();
	rf69.setFrequency(RF_FREQ);
#ifndef DEBUG
	RF_Listen(RF_INTERVAL*2);
#endif
}

void loop()
{
	toc1 = millis();
	unsigned int battery_voltage_in, luminosity;
	byte radiopacket[PACKET_SIZE]; 
	sensors_event_t humidity, temp;
	byte humidity_in;
	int temp_in;
	int rssi;
	int16_t temp_out;
	byte humidity_out;
	byte battery_voltage_out;
	int message_id;
	static unsigned int disconnection_count = 0;
	static unsigned int connection_count = 0;
	static int status = 0; //0: connexion lost, 1: first connexion, 2: connexion ok
	static int sleep_time = RF_INTERVAL;
	unsigned int waited_time;

	digitalWrite(LED_BUILTIN, HIGH);

	//Outside
	waited_time = RF_Listen(TIMEOUT);
	toc1 = millis();
	if (waited_time < TIMEOUT)
	{
		// Should be a message for us now   
		uint8_t buf[PACKET_SIZE];
		uint8_t len = PACKET_SIZE;
		if (rf69.recv(buf, &len)) 
		{
			if (!len) return;
			rssi = rf69.lastRssi();
			temp_out = word(buf[0], buf[1]);
			humidity_out = buf[2];
			battery_voltage_out = buf[3];
			message_id = buf[4];
			connection_count++;
		}
	}
	else
	{
		rssi = -99;
		battery_voltage_out = 0;
		humidity_out = 0;
		temp_out = 0;
		disconnection_count++;
		message_id = -1;
	}
	rf69.sleep();
	toc2 = millis();

	byte packet_loss_rate = (disconnection_count*100.0) / (connection_count+disconnection_count);
	if(connection_count>1000)//avoid overflow
	{
		disconnection_count / 8;
		connection_count / 8;
	}

	//Read sensors
	digitalWrite(LUMINOSITY_ENABLE, HIGH); //Enable luminosity sensor

	digitalWrite(ANALOG_ENABLE, HIGH); //Enable voltage divider
	battery_voltage_in = analogRead(BATTERY_VOLTAGE) * VOLTAGE_DIVIDER;
	battery_voltage_in = battery_voltage_in >> ADC_RES;
	digitalWrite(ANALOG_ENABLE, LOW); //Disable voltage divider
	
	sht4.getEvent(&humidity, &temp);// populate temp and humidity objects with fresh data
	humidity_in = humidity.relative_humidity;
	temp_in = temp.temperature*10;

	luminosity = analogRead(LUMINOSITY); //need few ms to settle
	digitalWrite(LUMINOSITY_ENABLE, LOW); //Enable luminosity sensor
	toc3 = millis();

	//Display information 75ms with software SPI, 35ms with hardware SPI 4MHz
	display_data(battery_voltage_in, temp_in, humidity_in,
			battery_voltage_out, temp_out, humidity_out, rssi, packet_loss_rate, luminosity, message_id, waited_time);
	

	//Sleep
	digitalWrite(LED_BUILTIN, LOW);
	toc4 = millis();

	#ifdef DEBUG
	//Serial.print(temp_out); Serial.print("° ");
	//Serial.print(humidity_out); Serial.print("% ");
	Serial.print("Batt_out "); Serial.println(battery_voltage_out); 
	//Serial.print("RSSI "); Serial.println(rssi); 
	Serial.print("Batt_in "); Serial.println(battery_voltage_in); 
	Serial.print("Luminosity "); Serial.println(luminosity); 

	//Serial.print("RF "); Serial.println(toc2-toc1); 
	//Serial.print("T "); Serial.println(toc3-toc2);
	//Serial.print("Display "); Serial.println(toc4-toc3);
	//Serial.print("Total "); Serial.println(toc4-toc1);
	Serial.print(" Status "); Serial.print(status);
	Serial.print(" ID "); Serial.print(message_id);
	Serial.print(" Slept "); Serial.print(sleep_time);
	#endif
	
	toc5 = millis();

	//update status
	if(message_id == -1) 
	{
		status = 0;
	}
	else
	{
		if(status == 0)
		{
			status = 1;
		}
		else
		{
			status = 2;
		}
	}

	//calculate sleep_time
	if(status == 0) //sleep short time
	{
		sleep_time = RF_INTERVAL-TIMEOUT-1000;
	}
	else if(status == 1) //sleep interval with big margin
	{
		sleep_time = RF_INTERVAL - 400; 
	}
	else //optimize waited_time
	{
		if(message_id == 0) //increase sleep time
		{
			//sleep_time = sleep_time + (waited_time/10*29) - 120; // Sleep for Interval - processing time since reception - margin
			sleep_time = sleep_time + waited_time - 10;
		}
		if(message_id < 3 ) //decrease sleep time
		{
			sleep_time = sleep_time - 10;
		}
		if(message_id >= 3 ) //decrease sleep time
		{
			sleep_time = sleep_time - 60;
		}
	}
	
#ifndef DEBUG
	timer.wait(sleep_time);
#else
	Serial.print(" NewStatus "); Serial.print(status);
	Serial.print(" Waited "); Serial.print(waited_time); Serial.print(" Sleeping "); Serial.println(sleep_time);
	Serial.println();
	//delay(sleep_time);
	delay(1000);
#endif
}

// Listen for a message for max timeout (in milliseconds)
//TODO: reduce microcontroller current during waiting https://forums.adafruit.com/viewtopic.php?t=211308
//https://forums.adafruit.com/viewtopic.php?t=211308&start=15
//https://forum.arduino.cc/t/reducing-power-consumption/412345
// Return true if message is available
unsigned int RF_Listen(unsigned int timeout)
{
	unsigned int listen_time = 0;
	#define INTERVAL 10
	rf69.available(); //Enable RX
	while(listen_time < timeout) //wait for available message
	{
#ifndef DEBUG
		timer.wait(INTERVAL);
		listen_time = listen_time + 28; //28ms instead of 10ms, seen with oscilloscope ???
#else
		delay(INTERVAL);
		listen_time = listen_time + INTERVAL;
#endif
		
		if (rf69.available()) 
		{
			return listen_time;
		}
	}
	return listen_time;
}

void display_data(byte batt_in, int temp_in, byte humidity_in,
				byte batt_out, int temp_out, byte humidity_out, int rssi, byte packet_loss_rate, int luminosity, int message_id, int time)
{
	char buffer[14];
	bool connected = true; 
	humidity_in = min(humidity_in, 99);
	humidity_out = min(humidity_out, 99);

	if(message_id == -1) connected = false; //if not connected, do not display data from remote sensor

	display.clearDisplayBuffer();
	display.setRotation(3);
	display.setTextColor(BLACK);

	//Temperature + humidity
	#define MARGIN 8
	#define CHAR_HEIGHT 72 //height of 60pt char
	#define TEMP_IN_Y (CHAR_HEIGHT + MARGIN)
	#define TEMP_OUT_Y (SHARP_HEIGHT - MARGIN)

	display_temperature(6, TEMP_IN_Y, temp_in);
	humidity_in = 99;
	display_humidity(MARGIN, TEMP_OUT_Y, humidity_in);

	//Battery voltage
	#define BATT_IN_X 100
	#define BATT_IN_Y (SHARP_HEIGHT - 24)
	#define BATT_OUT_X BATT_IN_X
	#define BATT_OUT_Y (TEMP_OUT_Y + 8)

	display_drawBattery(BATT_IN_X, BATT_IN_Y, batt_in);

		//RSSI
	#define RSSI_X 115
	#define RSSI_Y (SHARP_HEIGHT - 30)
	
	display_drawRSSI(RSSI_X, RSSI_Y, rssi);

/*
	if(connected)
	{
		display_temperature(MARGIN, TEMP_OUT_Y, temp_out);
		display_humidity(HUMIDITY_X, TEMP_OUT_Y, humidity_out);
	}
	else
	{
		display.setFont(&FreeSans12pt7b);
		display.setCursor(40, TEMP_OUT_Y-10);
		display.print("no signal");
	}
	



	if(connected)
	{
		display_drawBattery(BATT_OUT_X, BATT_OUT_Y, batt_out);
	}
	
	//RSSI
	#define RSSI_X 130
	#define RSSI_Y (SHARP_HEIGHT - MARGIN)
	
	display_drawRSSI(RSSI_X, RSSI_Y, rssi);

	//Info
	#define INFO_X 104
	#define INFO_Y (BATT_IN_Y)

	rssi = max(rssi, -99);
	luminosity = min(luminosity, 999);
	display.setFont();
	display.setTextColor(BLACK);
	display.setCursor(INFO_X, INFO_Y);
	sprintf(buffer, "*%3d %ddB", luminosity, rssi);
	display.print(buffer);
	display.setCursor(INFO_X, INFO_Y+11);
	sprintf(buffer, "%3dms id%d", time, message_id);
	display.print(buffer);
*/
	display.refresh(); //13ms
}
#define CHAR_WIDTH 57 //width of 60pt char
#define DEGREE_X	20	// position of degree symbol
#define DEGREE_Y	66 // position of degree symbol

void display_temperature(unsigned int pos_x, unsigned int pos_y, int decitemp)
{
	unsigned int shift_x; 
	
	//diplay "-" for negative numbers and adjust position of digits
	if(decitemp < 0)
	{
		display.fillRect(pos_x, pos_y-36, 18, 8, BLACK);
		if(decitemp < -99)
		{
			pos_x = pos_x-2; //more compact
		}
		else
		{
			pos_x = pos_x + 24;
		}
	
		decitemp = -decitemp;
	}
	display.setFont(&FreeSans54pt7b);
	display.setCursor(pos_x, pos_y);
	display.print(decitemp/10);

	//adjust position of decimal depending on number of digits
	if(decitemp < 100)
	{
		shift_x = CHAR_WIDTH;
	}
	else
	{
		shift_x = 2*CHAR_WIDTH;
	}
	display.setFont(&FreeSans30pt7b);
	display.setCursor(pos_x+shift_x, pos_y);
	display.print(".");
	display.print(decitemp%10);

	//draw "°" symbol
	display.fillCircle(pos_x+shift_x+DEGREE_X, pos_y-DEGREE_Y, 9, BLACK);
	display.fillCircle(pos_x+shift_x+DEGREE_X, pos_y-DEGREE_Y, 5, WHITE);

}

void display_humidity(unsigned int pos_x, unsigned int pos_y, unsigned int humidity)
{
	display.setFont(&FreeSans30pt7b);
	display.setCursor(pos_x, pos_y);
	display.print(humidity);
	display.setFont(&FreeSans12pt7b);
	display.setCursor(pos_x+65, pos_y-25);
	display.print("%");
}

#define BATT_WIDTH 60
#define BATT_HEIGHT 20
#define BATT_MIN 33
#define BATT_MAX 40

void display_drawBattery(unsigned int pos_x, unsigned int pos_y, unsigned int decivolt)
{
	display.drawRoundRect(pos_x, pos_y, BATT_WIDTH, BATT_HEIGHT, 3, BLACK);
	display.drawRect(pos_x+BATT_WIDTH-1, pos_y+4, 3, 12, BLACK);

	if(decivolt > BATT_MIN)
	{
		unsigned int batt_level;
		batt_level = map(decivolt, BATT_MIN, BATT_MAX, 6, BATT_WIDTH);
		batt_level = constrain(batt_level, 6, BATT_WIDTH-6);
		display.fillRoundRect(pos_x+3, pos_y+3, batt_level, BATT_HEIGHT-6, 2, BLACK);
		display.setFont();
		if(decivolt > 36)
		{
			display.setTextColor(WHITE);
			display.setCursor(pos_x+6, pos_y+7);
			display.print(decivolt/10);
			display.print("V");
			display.print(decivolt%10);
		}
		else
		{
			display.setTextColor(BLACK);
			display.setCursor(pos_x+30, pos_y+7);
			display.print(decivolt/10);
			display.print("V");
			display.print(decivolt%10);
		}
	}
	else
	{
		display.setFont();
		display.setTextColor(BLACK);
		display.setCursor(pos_x+20, pos_y+7);
		display.print(decivolt/10);
		display.print("V");
		display.print(decivolt%10);
	}
}

#define RSSI_WIDTH 6
#define RSSI_HEIGHT 5
#define RSSI_WEAK -90
#define RSSI_FAIR -80
#define RSSI_STRONG -60
void display_drawRSSI(unsigned int pos_x, unsigned int pos_y, int rssi)
{
	if(rssi > -99)
	{
		//at least the small bar
		display.fillRect(pos_x, pos_y, RSSI_WIDTH, -2*RSSI_HEIGHT, BLACK);
		
		//second bar
		if(rssi > RSSI_WEAK)
		{
			display.fillRect(pos_x+RSSI_WIDTH+3, pos_y, RSSI_WIDTH, -3*RSSI_HEIGHT, BLACK);
		}
		else
		{
			display.drawRect(pos_x+RSSI_WIDTH+3, pos_y, RSSI_WIDTH, -3*RSSI_HEIGHT, BLACK);
		}

		//thrid bar
		if(rssi > RSSI_FAIR)
		{
			display.fillRect(pos_x+2*RSSI_WIDTH+2*3, pos_y, RSSI_WIDTH, -4*RSSI_HEIGHT, BLACK);
		}
		else
		{
			display.drawRect(pos_x+2*RSSI_WIDTH+2*3, pos_y, RSSI_WIDTH, -4*RSSI_HEIGHT, BLACK);
		}

		//fourth bar
		if(rssi > RSSI_STRONG)
		{
			display.fillRect(pos_x+3*RSSI_WIDTH+3*3, pos_y, RSSI_WIDTH, -5*RSSI_HEIGHT, BLACK);
		}
		else
		{
			display.drawRect(pos_x+3*RSSI_WIDTH+3*3, pos_y, RSSI_WIDTH, -5*RSSI_HEIGHT, BLACK);
		}
	}
	else //4 empty bars
	{
		display.drawRect(pos_x, pos_y, RSSI_WIDTH, -2*RSSI_HEIGHT, BLACK);
		display.drawRect(pos_x+RSSI_WIDTH+3, pos_y, RSSI_WIDTH, -3*RSSI_HEIGHT, BLACK);
		display.drawRect(pos_x+2*RSSI_WIDTH+2*3, pos_y, RSSI_WIDTH, -4*RSSI_HEIGHT, BLACK);
		display.drawRect(pos_x+3*RSSI_WIDTH+3*3, pos_y, RSSI_WIDTH, -5*RSSI_HEIGHT, BLACK);
	}
}

void display_welcome(void)
{
	display.clearDisplayBuffer();
	display.setRotation(3);
	display.setTextColor(BLACK);

	display.setFont(&FreeSans24pt7b);
	display.setCursor(17, 55);
	display.print("Happy");
	display.setCursor(2, 115);
	display.print("birthday");

	display.refresh();
}