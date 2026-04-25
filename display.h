#include <Adafruit_GFX.h>
#include <Adafruit_SharpMem.h> //modified to avoid redraw unchanged lines - see refresh(bool* lines)
#include <Fonts/FreeSans54pt7b.h> //https://github.com/WaylandM/XOD_ArduinoFreeFontFile
#include <Fonts/FreeSans30pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSans16pt7b.h> //https://github.com/WaylandM/XOD_ArduinoFreeFontFile
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans9pt7b.h>

//Display
#define SHARP_WIDTH 400
#define SHARP_HEIGHT 240
#define BLACK 0
#define WHITE 1

//#define DEBUG_DISPLAY

Adafruit_SharpMem display(&SPI, SHARP_SS, SHARP_WIDTH, SHARP_HEIGHT, 4000000);

bool lines[SHARP_HEIGHT];

void display_data(int batt_in, int temp_in, int humidity_in, int batt_out, int temp_out, int humidity_out, int rssi, int packet_loss_rate, int luminosity, int message_id, int waited_time);
void display_temperature(unsigned int pos_x, unsigned int pos_y, int decitemp);
void display_humidity(unsigned int pos_x, unsigned int pos_y, unsigned int humidity);
void display_drawBattery(unsigned int pos_x, unsigned int pos_y, unsigned int decivolt);
void display_drawRSSI(unsigned int pos_x, unsigned int pos_y, int rssi);
void display_info(int pos_x, int pos_y, int rssi, int luminosity, int time, int message_id);
void display_welcome(void);
void display_setLines(bool* lines);
void display_setAllLines(bool* lines);
void display_drawTickMarks(void);

void display_setup()
{
	display.begin();
	display.clearDisplay();
	display_setAllLines(lines);
	display_welcome();
}

void display_data(int batt_in, int temp_in, int humidity_in, int batt_out, int temp_out, int humidity_out, int rssi, int packet_loss_rate, int luminosity, int message_id, int waited_time)
{
	bool connected = true; 
	humidity_in = min(humidity_in, 99);
	humidity_out = min(humidity_out, 99);

#ifndef DEBUG_DISPLAY
	if(lines[0]==true)
	{
		display.clearDisplay();
		display_setLines(lines); //will redraw only necessary lines
	}
#endif
	
	if(message_id == -1) connected = false; //if not connected, do not display data from remote sensor

	display.clearDisplayBuffer();
	display.setRotation(0);
	display.setTextColor(BLACK);

#ifdef DEBUG_DISPLAY
	display_drawTickMarks();
#endif

	//Temperature + humidity
	#define MARGIN 20
	#define CHAR_HEIGHT 72 //height of 60pt char
	#define TEMP_IN_X MARGIN
	#define TEMP_IN_Y (CHAR_HEIGHT + MARGIN)
	#define TEMP_OUT_X (SHARP_WIDTH/2 + MARGIN)
	#define TEMP_OUT_Y TEMP_IN_Y
	
	#define HUMIDITY_IN_X 60
	#define HUMIDITY_IN_Y 175
	#define HUMIDITY_OUT_X (SHARP_WIDTH/2 + HUMIDITY_IN_X)
	#define HUMIDITY_OUT_Y HUMIDITY_IN_Y

	display_temperature(TEMP_IN_X, TEMP_IN_Y, temp_in);
	display_humidity(HUMIDITY_IN_X, HUMIDITY_IN_Y, humidity_in);

	//Battery voltage + charging
	#define BATT_IN_X 40
	#define BATT_IN_Y (SHARP_HEIGHT - 42)
	#define BATT_OUT_X (SHARP_WIDTH/2 + BATT_IN_X)
	#define BATT_OUT_Y BATT_IN_Y

	display_drawBattery(BATT_IN_X, BATT_IN_Y, batt_in);

	//RSSI
	#define RSSI_X 345
	#define RSSI_Y (SHARP_HEIGHT - 17)

	display_drawRSSI(RSSI_X, RSSI_Y, rssi);

	if(connected)
	{
		display_temperature(TEMP_OUT_X, TEMP_OUT_Y, temp_out);
		display_humidity(HUMIDITY_OUT_X, HUMIDITY_OUT_Y, humidity_out);
	}
	else
	{
		display.setTextColor(BLACK);
		display.setFont(&FreeSans12pt7b);
		display.setCursor(255, TEMP_OUT_Y-20);
		display.print("no signal");
	}
	
	if(connected)
	{
		display_drawBattery(BATT_OUT_X, BATT_OUT_Y, batt_out);
	}

	//Info
	#define INFO_X 145
	#define INFO_Y 200

	display_info(INFO_X, INFO_Y, rssi, luminosity, waited_time, message_id);
	display.refresh(lines); 
}

#define CHAR_WIDTH 57 //width of 60pt char
#define DEGREE_X	20	// position of degree symbol
#define DEGREE_Y	65 // position of degree symbol

void display_temperature(unsigned int pos_x, unsigned int pos_y, int decitemp)
{
	int decimal_x; //adjust position of decimal depending on number of digits
	int sign_x; //adjust position of sign depending on number of digits

	if(decitemp < -99) //2 digits, negative
	{
		sign_x = pos_x - 10;
		decimal_x = 2*CHAR_WIDTH;
	}
	else if(decitemp < 0) //1 digit, negative
	{
		pos_x += 28;
		sign_x = pos_x - 30;
		decimal_x = CHAR_WIDTH;
	}
	else if(decitemp < 100) //1 digit, positive
	{
		pos_x += 28;
		decimal_x = CHAR_WIDTH;
	}
	else // 2 digits, positive
	{
		decimal_x = 2*CHAR_WIDTH;
	}
	
	//diplay "-" for negative numbers and adjust position of digits
	if(decitemp < 0)
	{
		display.fillRect(sign_x, pos_y-36, 20, 8, BLACK);
		decitemp = -decitemp;
	}
	display.setTextColor(BLACK);
	display.setFont(&FreeSans54pt7b);
	display.setCursor(pos_x, pos_y);
	display.print(decitemp/10);

	display.setFont(&FreeSans30pt7b);
	display.setCursor(pos_x+decimal_x, pos_y);
	display.print(".");
	display.print(decitemp%10);

	//draw "°" symbol
	display.fillCircle(pos_x+decimal_x+DEGREE_X, pos_y-DEGREE_Y, 9, BLACK);
	display.fillCircle(pos_x+decimal_x+DEGREE_X, pos_y-DEGREE_Y, 5, WHITE);
}

#define CHAR_WIDTH2 33

void display_humidity(unsigned int pos_x, unsigned int pos_y, unsigned int humidity)
{
	int shift;

	if(humidity<10)
	{
		pos_x = pos_x + CHAR_WIDTH2 - 10;
		shift = CHAR_WIDTH2;
	}
	else
	{
		//pos_x = pos_x;
		shift = 2*CHAR_WIDTH2;
	}
	display.setTextColor(BLACK);
	display.setFont(&FreeSans30pt7b);
	display.setCursor(pos_x, pos_y);
	display.print(humidity);
	display.setFont(&FreeSans12pt7b);
	display.setCursor(pos_x+shift, pos_y-25);
	display.print("%");
}

#define BATT_WIDTH 70
#define BATT_HEIGHT 26
#define BATT_MIN 33
#define BATT_MAX 40

void display_drawBattery(unsigned int pos_x, unsigned int pos_y, unsigned int decivolt)
{
	display.drawRoundRect(pos_x, pos_y, BATT_WIDTH, BATT_HEIGHT, 4, BLACK);
	display.drawRoundRect(pos_x+1, pos_y+1, BATT_WIDTH-2, BATT_HEIGHT-2, 3, BLACK);
	display.fillRect(pos_x+BATT_WIDTH, pos_y+6, 3, 12, BLACK);

	if(decivolt > BATT_MIN)
	{
		unsigned int batt_level;
		batt_level = map(decivolt, BATT_MIN, BATT_MAX, 6, BATT_WIDTH);
		batt_level = constrain(batt_level, 6, BATT_WIDTH-8);
		display.fillRoundRect(pos_x+4, pos_y+4, batt_level, BATT_HEIGHT-8, 2, BLACK);
		display.setFont();
		if(decivolt > 36)
		{
			display.setTextColor(WHITE);
			display.setCursor(pos_x+10, pos_y+9);
			display.print(decivolt/10);
			display.print("V");
			display.print(decivolt%10);
		}
		else
		{
			display.setTextColor(BLACK);
			display.setCursor(pos_x+BATT_WIDTH/2+10, pos_y+9);
			display.print(decivolt/10);
			display.print("V");
			display.print(decivolt%10);
		}
	}
	else
	{
		display.setFont();
		display.setTextColor(BLACK);
		display.setCursor(pos_x+BATT_WIDTH/2+10, pos_y+9);
		display.print(decivolt/10);
		display.print("V");
		display.print(decivolt%10);
	}
}

#define RSSI_WIDTH 6
#define RSSI_HEIGHT 6
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

#define INFO_CHAR_HEIGHT 12

void display_info(int pos_x, int pos_y, int rssi, int luminosity, int time, int message_id)
{
	char buffer[14];
	rssi = max(rssi, -99);
	luminosity = min(luminosity, 999);
	display.setFont();
	display.setTextColor(BLACK);

	display.setCursor(pos_x, pos_y);
	sprintf(buffer, "*%3d  %ddB", luminosity, rssi);
	display.print(buffer);

	display.setCursor(pos_x, pos_y + INFO_CHAR_HEIGHT);
	sprintf(buffer, "%3dms  id%d", time, message_id);
	display.print(buffer);
}

void display_welcome(void)
{
	display.clearDisplayBuffer();
	display.setRotation(0);
	display.setTextColor(BLACK);

	display.setFont(&FreeSans24pt7b);
	display.setCursor(30, 80);
	display.print("Happy birthday!");

	display.setFont(&FreeSans24pt7b);
	display.setCursor(20, 200);
	display.print("La Hulotte - 2026");

	display.refresh();
}

#define INTERVAL1 (MARGIN-1)
#define INTERVAL2 95
#define INTERVAL3 135
#define INTERVAL4 177
#define INTERVAL5 193
#define INTERVAL6 (SHARP_HEIGHT - 16)
//To avoid sending lines that are not modified. 
// True to redraw the line
// False to skip the line
void display_setLines(bool* lines)
{
	for(int i=0; i<SHARP_HEIGHT ; i++)
	{
		if(i < INTERVAL1) lines[i] = false;
		else if(i <= INTERVAL2) lines[i] = true;
		else if(i < INTERVAL3) lines[i] = false;
		else if(i <= INTERVAL4) lines[i] = true;
		else if(i < INTERVAL5) lines[i] = false;
		else if(i <= INTERVAL6) lines[i] = true;
		else lines[i] = false;
	}

}

//Redraw all the lines
void display_setAllLines(bool* lines)
{
	for(int i=0; i<SHARP_HEIGHT ; i++)
	{
		lines[i] = true;
	}
}

void display_drawTickMarks(void)
{
	for(int i = 1; i<SHARP_HEIGHT/10; i++ )
	{
		if(i%5 == 0)
		{
			display.drawFastHLine(SHARP_WIDTH/2-4, i*10, 8, BLACK);
		}
		else
		{
			display.drawFastHLine(SHARP_WIDTH/2-2, i*10, 4, BLACK);
		}
		
	}
	for(int i = 1; i<SHARP_WIDTH/10; i++ )
	{
		if(i%5 == 0)
		{
			display.drawFastVLine(i*10,SHARP_HEIGHT/2-4 , 8, BLACK);
		}
		else
		{
			display.drawFastVLine(i*10,SHARP_HEIGHT/2-2 , 4, BLACK);
		}
	}

	display.drawFastHLine(0, INTERVAL1-1, SHARP_WIDTH, BLACK);
	display.drawFastHLine(0, INTERVAL2-1, SHARP_WIDTH, BLACK);
	display.drawFastHLine(0, INTERVAL3-1, SHARP_WIDTH, BLACK);
	display.drawFastHLine(0, INTERVAL4-1, SHARP_WIDTH, BLACK);
	display.drawFastHLine(0, INTERVAL5-1, SHARP_WIDTH, BLACK);
	display.drawFastHLine(0, INTERVAL6-1, SHARP_WIDTH, BLACK);
	
}