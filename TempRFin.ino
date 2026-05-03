/*
	Program for indoor wireless temperature and humidity sensor.

	Feather MO with RFM69HCW radio module
	SHT40 sensor
	Phototransistor
	Display Sharp memory display 2.7"
	

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

//Pinout
#define BATTERY_VOLTAGE A7
#define ANALOG_ENABLE 12
#define LUMINOSITY A1
#define LUMINOSITY_ENABLE A2
#define SHARP_SS   A5

#include "display.h"

#define RF_CS 8
#define RF_RST 4
#define RF_IRQ 3

//RF
#define RF_FREQ 915.0
#define PACKET_SIZE 5
#define RF_INTERVAL 18000 //millisecond between emissions
#define TIMEOUT 2000 //reception timeout in millisecond

#define VOLTAGE_DIVIDER 66 // 3V3 * (15k+15k)/30k * 10 ; x10 to get results in deciVolts
#define ADC_RES 10 // 10bits

#define STATE_OFF 0
#define STATE_SYNC 1
#define STATE_ON 2

#define BATT 0
#define TEMP 1
#define HUMI 2
#define LUMI 3
#define M_ID 3
#define M_RSSI 4

//#define DEBUG

Adafruit_SHT4x sht4 = Adafruit_SHT4x();
RH_RF69 rf69(RF_CS, RF_IRQ); // Singleton instance of the radio driver
TimerCounter timer;

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

	display_setup();

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

}

void loop()
{
	static unsigned int state = STATE_SYNC;
	int data_in[4]; //battery voltage, temperature, humidity, luminosity
	int data_rf[5]; //battery voltage, temperature, humidity, message_id, rssi
	
	unsigned int waited_time;
	int sleep_time;

	if(state == STATE_OFF) //read luminosity, if dark sleep 30s, else state = SYNC
	{
		digitalWrite(LUMINOSITY_ENABLE, HIGH); //Enable luminosity sensor
		delay(10); //need few ms to settle
		int luminosity = analogRead(LUMINOSITY);
		digitalWrite(LUMINOSITY_ENABLE, LOW); //Disable luminosity sensor

		if(isDark(luminosity))
		{
			sleep_time = 30000;//30s
		}
		else
		{
			state = STATE_SYNC;
		}
	}
	
	else if(state == STATE_SYNC) //read sensors in, display with SYNC, then RF_Listen, display again
	{
		digitalWrite(LED_BUILTIN, HIGH);

		read_sensors_in(data_in);

		display_data(data_in[BATT], data_in[TEMP], data_in[HUMI], data_in[LUMI],
				0, 0, 0, -99, -1, true, 0);

		waited_time = RF_Listen(RF_INTERVAL + 1000);
		read_sensors_rf(data_rf, 0);

		state = STATE_ON;
		read_sensors_in(data_in);
		display_data(data_in[BATT], data_in[TEMP], data_in[HUMI], data_in[LUMI],
				data_rf[BATT], data_rf[TEMP], data_rf[HUMI], data_rf[M_RSSI], data_rf[M_ID], false, waited_time);
		sleep_time = compute_sleep_time(-1, 0); //reset variable in this function
		sleep_time = compute_sleep_time(0, 0); //reset variable in this function

		digitalWrite(LED_BUILTIN, LOW);
	}
	
	else if(state == STATE_ON) //Listen RF, read sensors in, display
	{
		digitalWrite(LED_BUILTIN, HIGH);

		//Outside
		waited_time = RF_Listen(TIMEOUT);
		read_sensors_rf(data_rf, waited_time);

		//Read sensors
		read_sensors_in(data_in);
		
		sleep_time = compute_sleep_time(data_rf[M_ID], waited_time);
	
		if(isDark(data_in[LUMI]))
		{
			state = STATE_OFF;
			sleep_time = 30000;
			display_sleep();
		}
		else
		{
			//Display information 75ms with software SPI, 35ms with hardware SPI 4MHz
			display_data(data_in[BATT], data_in[TEMP], data_in[HUMI], data_in[LUMI],
					data_rf[BATT], data_rf[TEMP], data_rf[HUMI], data_rf[M_RSSI], data_rf[M_ID], false, sleep_time);
		}
		
		digitalWrite(LED_BUILTIN, LOW);
		
		/*
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
		Serial.print(" RF_status "); Serial.print(RF_status);
		Serial.print(" ID "); Serial.print(message_id);
		Serial.print(" Slept "); Serial.print(sleep_time);
		Serial.print(" NewRF_status "); Serial.print(RF_status);
		Serial.print(" Waited "); Serial.print(waited_time); Serial.print(" Sleeping "); Serial.println(sleep_time);
		Serial.println();
		#endif
		*/	
	}	
	
	if(state != STATE_SYNC)
	{
		#ifndef DEBUG
		timer.wait(sleep_time);
		#else
		delay(1000);
		#endif
	}

}

// Listen for a message for max timeout (in milliseconds)
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

// Read luminosity sensor. Return true if dark for a long time
bool isDark(int luminosity)
{
	static unsigned int dark = 0;

	if(luminosity < 100) dark++;
	else dark = 0;

	if(dark > 10) //200 ~ 1h
	{
		dark = 200;
		return true;
	}
	else
	{
		return false;
	}
}

//fill data from remote sensors: battery voltage, temperature, humidity, message_id, rssi
void read_sensors_rf(int* data, unsigned int waited_time)
{
	bool bad_data = true;
	int16_t temperature;
	int battery, humidity, rssi, message_id;
	if (waited_time < TIMEOUT)
	{
		// Should be a message for us now   
		uint8_t buf[PACKET_SIZE];
		uint8_t len = PACKET_SIZE;
		if (rf69.recv(buf, &len)) 
		{
			if (len)	
			{
				rssi = rf69.lastRssi();
				temperature = word(buf[0], buf[1]);
				humidity = buf[2];
				battery = buf[3];
				message_id = buf[4];
				bad_data = false;
			}		
		}
	}
	rf69.sleep();

	if(bad_data)
	{
		rssi = -99;
		battery = 0;
		humidity = 0;
		temperature = 0;
		message_id = -1;
	}
		
	data[BATT] = battery;
	data[TEMP] = temperature;
	data[HUMI] = humidity;
	data[M_ID] = message_id;
	data[M_RSSI] = rssi;
}

//fill data with sensors values: battery voltage, temperature, humidity, luminosity
void read_sensors_in(int* data)
{
	int battery, temperature, humidity, luminosity;
	sensors_event_t humi, temp;

	digitalWrite(LUMINOSITY_ENABLE, HIGH); //Enable luminosity sensor
	digitalWrite(ANALOG_ENABLE, HIGH); //Enable voltage divider
	battery = analogRead(BATTERY_VOLTAGE) * VOLTAGE_DIVIDER;
	battery = battery >> ADC_RES;
	digitalWrite(ANALOG_ENABLE, LOW); //Disable voltage divider
	
	sht4.getEvent(&humi, &temp);// populate temp and humidity objects with fresh data
	temperature = temp.temperature*10;
	humidity = humi.relative_humidity;

	luminosity = analogRead(LUMINOSITY); //need few ms to settle
	digitalWrite(LUMINOSITY_ENABLE, LOW); //Disable luminosity sensor

	data[BATT] = battery;
	data[TEMP] = temperature;
	data[HUMI] = humidity;
	data[LUMI] = luminosity;

}


int compute_sleep_time(int message_id, int waited_time)
{
	static int RF_status = 0; //0: connexion lost, 1: first connexion, 2: connexion ok
	static int sleep_time = RF_INTERVAL;

	//update RF_status
	if(message_id == -1) 
	{
		RF_status = 0;
	}
	else
	{
		if(RF_status == 0)
		{
			RF_status = 1;
		}
		else
		{
			RF_status = 2;
		}
	}

	//calculate sleep_time
	if(RF_status == 0) //sleep short time
	{
		sleep_time = RF_INTERVAL-TIMEOUT-1000;
	}
	else if(RF_status == 1) //sleep interval with big margin
	{
		sleep_time = RF_INTERVAL - 400; 
	}
	else //optimize waited_time
	{
		if(message_id == 0) //increase sleep time
		{
			sleep_time = sleep_time + waited_time - 10;
		}
		if(message_id == 1 ) //decrease sleep time
		{
			sleep_time = sleep_time - 20;
		}
		if(message_id >= 2 ) //decrease sleep time
		{
			sleep_time = sleep_time - 60;
		}
	}

	return sleep_time;
}