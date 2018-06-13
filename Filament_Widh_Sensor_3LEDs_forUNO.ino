// Reference 1) Filament width sensor based on Thing #454584 by filpper / http://www.thingiverse.com/thing:454584
// Reference 2) Arduino Playground: Reading a photodiode array / http://playground.arduino.cc/Main/TSL1402R
// Reference 3) Arduino Code: Reading and Writing Data Structures to EEPROM / http://playground.arduino.cc/Code/EEPROMWriteAnything

#include <EEPROM.h>
#include <TimerOne.h>
#include "EEPROMAnything.h"

#define NPIXELS 128  // No. of pixels in array

// delay multiplier
#define MUL 64

// Define various ADC prescaler:
unsigned char PS_32 = (1 << ADPS2) | (1 << ADPS0);
unsigned char PS_128 = (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);

PROGMEM const byte PWMDAC1pin = 9; // PWM DAC, only pins 9 and 10 are allowed
PROGMEM const byte PWMDAC2pin = 10; // PWM DAC, only pins 9 and 10 are allowed
PROGMEM const byte period = 128; // for 10 bit DAC 

PROGMEM const byte CLKpin = 3;    // <-- Arduino pin delivering the clock pulses to pin 3 (CLK) of the TSL1401 
PROGMEM const byte SIpin = 4;     // <-- Arduino pin delivering the SI (serial-input) pulse to pin 2 of the TSL1401 
PROGMEM const byte AOpin = A1;    // <-- Arduino pin connected to pin 4 (analog output 1)of the TSL1401

int nAggr = 2;        // image averaging count per one scan. should not exceed 10
int timer = 5;        // value output averaging counter

byte leds[3] = {5, 6, 9};
byte brightness[3] = {128, 128, 128};
int noLED = 0;

unsigned short intArray[3 * NPIXELS]; // <-- the array where the readout of the photodiodes is stored, as integers

// default filament width
double lowpassValue[3] = {1.75, 1.75, 1.75};
double lowpassFactor = 0.5;
double MyValue[3];

int timerCounter = 0;
double timerValue[3] = {0, 0, 0};

PROGMEM const double calibFactorDefault[3] = {22.271, 15.748, 22.271};
double calibFactor[3] =  {22.271, 15.748, 22.271}; // (default pixel per mm = 15.748 for 400dpi sensor / at 45deg:  22.271 px per mm

boolean debugMessage = false;
boolean doCalibration = false;
boolean visualize = false;

void helpMessage()
{
   if(EEPROM.read(0) != 42 || EEPROM.read(20) != calculateCRC())
   {
     Serial.println(F("[WARNING] This sensor is not calibrated yet, applying default calibration factor"));
   }
   
   Serial.println(F("Filament width sensor based on Thing #454584 (http://www.thingiverse.com/thing:454584) by filpper"));
   Serial.println(F("Modified to use Arduino Pro Micro (from SparkFun electornics) by inornate (http://kuaa.net)"));
   Serial.println(F("==============================     Commands     ================================"));
   Serial.println(F(" [h] help (this screen) / [c] calibration with 2mm rod")); 
   Serial.println(F(" [d] turn on debug messages / [e] turn off debug messages"));    
   Serial.println(F(" [v] enable visualize / [f] disable visaulize"));
   Serial.println(F(" [0] [1] [2] : Select LED  /  [j] [k] decrease/increase the brightness"));
   Serial.println(F(" [R] : reset the default value"));
   Serial.println(F("==============================  Setting Values  ================================"));

   Serial.print(F("Number of value acquision per scan : "));
   Serial.println(nAggr);

   Serial.print(F("Voltage output period (unit: scans) : "));
   Serial.println(timer);

   Serial.print(F("calibration factor (unit: pixel per mm) : "));
   printDouble(calibFactor[0], 5);
   Serial.print(", ");
   printDouble(calibFactor[1], 5);
   Serial.print(", ");   
   printDouble(calibFactor[2], 5);   
   Serial.println();
   
   Serial.print(F("LED brightnesses: "));
   Serial.print(brightness[0]);
   Serial.print(", ");
   Serial.print(brightness[1]);
   Serial.print(", ");
   Serial.print(brightness[2]);
   Serial.println();

   Serial.println(F("Version : Feb 13 2015"));
}

// ledNo: from 1 ~ 2
// value : 0 ~ 255
void setLED(int ledNo, byte val)
{
  int value = val;
  
  if(ledNo >= 3 || ledNo < 0)
    return;
  
  clearLED();

  if(ledNo == 2)
    value *= 4;
  
  analogWrite(leds[ledNo], value);
}

void clearLED()
{
  for(int i=0;i<3;i++)
  {
    analogWrite(leds[i], 0);
  }  
}

void setup() 
{ 
  Serial.begin(115200);

  // To set up the ADC, first remove bits set by Arduino library, then choose 
  // a prescaler: PS_16, PS_32, PS_64 or PS_128:
  ADCSRA &= ~PS_128;  
  ADCSRA |= PS_32; // <-- Using PS_32 makes a single ADC conversion take ~30 us

  pinMode(PWMDAC2pin, OUTPUT);  

  pinMode(leds[0], OUTPUT);  
  pinMode(leds[1], OUTPUT);
  pinMode(leds[2], OUTPUT);

  Timer1.initialize(period); 

  // Next, assert default setting:
  analogReference(DEFAULT);

  // Set all IO pins low:
  for( int i=0; i< 14; i++ )
  {
    digitalWrite(i, LOW);  
  }

  // Fast PWM mode
  TCCR0B = (TCCR0B & 0b11111000) | 0x01;
  TCCR1B = (TCCR1B & 0b11111000) | 0x01;

  initSensor();
  makeOutput(0);
  readCalibration();

  noLED = 0;  
  setLED(noLED, brightness[noLED]);
}

void readCalibration()
{
  byte signiture = EEPROM.read(0);
  byte crc = calculateCRC();
  byte readCRC = EEPROM.read(20);
  
  if(signiture == 42 && readCRC == crc) // have calibration data on its EEPROM
  {
    // calibFactors
    EEPROM_readAnything(1, calibFactor[0]);
    EEPROM_readAnything(5, calibFactor[1]);
    EEPROM_readAnything(9, calibFactor[2]);
    
    // brightnesses
    EEPROM_readAnything(13, brightness[0]);
    EEPROM_readAnything(14, brightness[1]);
    EEPROM_readAnything(15, brightness[2]);

    Serial.print(F("Calibration data read."));
    helpMessage();
    Serial.println();
  }
  else
  {    
     Serial.println(F("Calibration not has been done. Set to default values"));
     calibFactor[0] = calibFactorDefault[0];
     calibFactor[1] = calibFactorDefault[1];
     calibFactor[2] = calibFactorDefault[2];     
     
     brightness[0] = 128;
     brightness[1] = 128;
     brightness[2] = 128;
  }
}

void writeCalibration() // write calibration factor
{
  EEPROM.write(0, 42);    // Signiture value, the answer of everything :D

  // calibFactors
  EEPROM_writeAnything(1, calibFactor[0]);
  EEPROM_writeAnything(5, calibFactor[1]);
  EEPROM_writeAnything(9, calibFactor[2]);
  
  // brightnesses
  EEPROM_writeAnything(13, brightness[0]);
  EEPROM_writeAnything(14, brightness[1]);
  EEPROM_writeAnything(15, brightness[2]);

  byte crc = calculateCRC();
  
  EEPROM.write(20, crc);
}

byte calculateCRC()
{
  byte crcByte = 0;
  
  for(int i=0;i<20;i++)
  {
    crcByte ^= EEPROM.read(i);
  }
  
  return crcByte;
}


void loop() 
{ 
  int i, j;
  short aggrArray[NPIXELS];

  // scan for each LEDs
  for(int led=0;led<3;led++)
  {
    setLED(led, brightness[led]);
    delay(5*MUL); // wait for LED setup time (lowpass filter, with 200 ohm and 10uF cap)
    
    for(i=0;i< NPIXELS;i++)
    {
      aggrArray[i] = 0;
    }

    for(i=0;i<nAggr;i++)
    {
      scanSensor(led);
      for(j=0;j<NPIXELS;j++)
      {
        aggrArray[j] += intArray[led*NPIXELS + j];
      }  
    }

    for(i=0;i< NPIXELS;i++)
    {
      intArray[led * NPIXELS + i] = int(aggrArray[i] / nAggr);
    }

    double nPixel = processImage(led);
    lowpassValue[led] = lowpassValue[led] * (1 - lowpassFactor) + nPixel * lowpassFactor;
  }
  
  clearLED();

  // TEMP: calculate mm for 
  double mmValue0 = lowpassValue[0] / calibFactor[0];
  double mmValue1 = lowpassValue[1] / calibFactor[1];
  double mmValue2 = lowpassValue[2] / calibFactor[2];

  // Command processor
  if(Serial.available() > 0)
  {
    char incomingByte = Serial.read();
    switch(incomingByte)
    {
    case 'd':    // Debugging message toggle
      debugMessage = true;
      visualize = false;
      break;
    case 'e':    // Debugging message cancel
      debugMessage = false;
      visualize = false;
      break;
    case 'c':    // Calibration
      doCalibration = true;
      break;
    case 'v':    // Visualize
      visualize = true;
      debugMessage = false;
      break;
    case 'f':    // Visualize cancel
      visualize = false;
      debugMessage = false;
      break;      
    case 'h':
      helpMessage();
      break;
    case '0':
      noLED = 0;
      break;
    case '1':
      noLED = 1;
      break;
    case '2':
      noLED = 2;
      break;      
    case 'j':    // brightness down
      brightness[noLED] = max(brightness[noLED]-1, 0);
      setLED(noLED, brightness[noLED]);
      break;
    case 'k':    // brightness up
      brightness[noLED] = min(brightness[noLED]+1, 255);   
      setLED(noLED, brightness[noLED]);      
      break;
    case 'R':    // reset the failsafe value
      EEPROM.write(0, 0);
      readCalibration();
      break;
    }
  }

  if(visualize)
  {
    Serial.write ((byte)0);            // sync byte = 0
        
    for(int led = 0; led < 3; led++)
    {
      for (int i = 0; i < NPIXELS; i++) {
        byte b = intArray[led * NPIXELS + i] / 4;
        
        if(b == 0)
          Serial.write((byte)1);
        else
          Serial.write((byte)b);
      }
    }
    
    Serial.write((byte)brightness[0]);
    Serial.write((byte)brightness[1]);  
    Serial.write((byte)brightness[2]);  
  }
  else if(debugMessage)
  {
    Serial.print(F("Raw:\t"));
    printDouble(lowpassValue[0], 4);
    Serial.print("\t");
    printDouble(lowpassValue[1], 4);
    Serial.print("\t");
    printDouble(lowpassValue[2], 4);
    MyValue[0]=lowpassValue[0];
    Serial.print("\t");
    printDouble(MyValue[0], 4);
    
    Serial.print(F("\t(mm)=\t"));
    printDouble(mmValue0, 3);
    Serial.print("\t");
    printDouble(mmValue1, 3);
    Serial.print("\t");
    printDouble(mmValue2, 3);

    Serial.println();
  }
  
  if(mmValue1 < 1.8 && mmValue1 > 2.2 && doCalibration)
  {
    Serial.println(F("Please insert 2mm rod for calibration"));
    doCalibration = false;
  }

  timerValue[0] += lowpassValue[0];  
  timerValue[1] += lowpassValue[1];
  timerValue[2] += lowpassValue[2];  
  timerCounter++;

  if(timerCounter == timer) // measurement have been done [timer] times
  {
    double averaged0 = timerValue[0] / timer;
    double averaged1 = timerValue[1] / timer;
    double averaged2 = timerValue[2] / timer;    
    
    double averagedMM0 =  averaged0 / calibFactor[0];
    double averagedMM1 =  averaged1 / calibFactor[1];
    double averagedMM2 =  averaged2 / calibFactor[2];
    
    double averagedMM = (averagedMM0 + averagedMM1 + averagedMM2) / 3;

    timerCounter = 0;
    timerValue[0] = 0;
    timerValue[1] = 0;
    timerValue[2] = 0;    
    
    if(doCalibration)
    {
      calibFactor[0] = averaged0 / 2.0;
      calibFactor[1] = averaged1 / 2.0;      
      calibFactor[2] = averaged2 / 2.0;      
      
      Serial.print(F("Calibration factor has been adjusted to "));
      printDouble(calibFactor[0], 5);
      Serial.print(", ");
      printDouble(calibFactor[1], 5);
      Serial.print(", ");
      printDouble(calibFactor[2], 5);
      Serial.println(F(" (pixel count per mm)"));
      doCalibration = false;
      writeCalibration();
    }

    unsigned int outVal = makeOutput(int(averagedMM * 1000));    // makeOutput get um for its parameter

    if(false && debugMessage)
    {
      Serial.print(F("Voltage output:\t"));
      printDouble(averagedMM, 3);
      Serial.print("\t(");
      Serial.print(outVal);
      Serial.println("/1023)");
    }
  }
}

void printDouble(double val, byte precision){
  // prints val with number of decimal places determine by precision
  // precision is a number from 0 to 6 indicating the desired decimial places
  // example: printDouble( 3.1415, 2); // prints 3.14 (two decimal places)

  Serial.print (int(val));  //prints the int part
  if( precision > 0) {
    Serial.print("."); // print the decimal point
    unsigned long frac;
    unsigned long mult = 1;
    byte padding = precision -1;
    while(precision--)
      mult *=10;

    if(val >= 0)
      frac = (val - int(val)) * mult;
    else
      frac = (int(val)- val ) * mult;
    unsigned long frac1 = frac;
    while( frac1 /= 10 )
      padding--;
    while(  padding--)
      Serial.print("0");
    Serial.print(frac,DEC) ;
  }
}

void initSensor()
{
  // Initialize two Arduino pins as digital output:
  pinMode(CLKpin, OUTPUT); 
  pinMode(SIpin, OUTPUT);

  // Clock out any existing SI pulse through the ccd register:
  for(int i=0;i< NPIXELS+4;i++)
  {
    ClockPulse(); 
  }

  // Create a new SI pulse and clock out that same SI pulse through the sensor register:
  digitalWrite(SIpin, HIGH);
  delayMicroseconds(1*MUL);
  digitalWrite(CLKpin, HIGH);
  delayMicroseconds(1*MUL);
  digitalWrite(SIpin, LOW);
  delayMicroseconds(1*MUL);
  digitalWrite(CLKpin, LOW);

  for(int i=0;i< NPIXELS+4;i++)
  {
    ClockPulse(); 
  }
}

unsigned int makeOutput(int um) // input as micrometer (um)
{
  double d = (double)um / 1000;
  unsigned int value = d / 5 * 1023;

  Timer1.pwm(PWMDAC2pin, value); // output
  return value;
}

void scanSensor(int led)
{
  // Stop the ongoing integration of light quanta from each photodiode by clocking in a
  // SI pulse:
  // Create a new SI pulse and clock out that same SI pulse through the sensor register:
  digitalWrite(SIpin, HIGH);
  delayMicroseconds(1*MUL);
  digitalWrite(CLKpin, HIGH);
  delayMicroseconds(1*MUL);
  digitalWrite(SIpin, LOW);
  delayMicroseconds(1*MUL);
  digitalWrite(CLKpin, LOW);

  // Next, a new measuring cycle is starting once 18 clock pulses have passed. At  
  // that time, the photodiodes are once again active. We clock out the SI pulse through
  // the NPIXELS bit register in order to be ready to halt the ongoing measurement at our will
  // (by clocking in a new SI pulse):
  for(int i = 0; i < NPIXELS+4; i++)
  {
    if(i==18)
    {
      // Now the photodiodes goes active..
      // An external trigger can be placed here
    }
    ClockPulse(); 
  }    

  // The integration time of the current program / measurement cycle is ~2ms. If a larger time
  // of integration is wanted, uncomment the next line:
  delay(5*MUL);
  
  // Stop the ongoing integration of light quanta from each photodiode by clocking in a SI pulse 
  // into the sensors register:
  // Create a new SI pulse and clock out that same SI pulse through the sensor register:
  digitalWrite(SIpin, HIGH);
  delayMicroseconds(1*MUL);
  digitalWrite(CLKpin, HIGH);
  delayMicroseconds(1*MUL);
  digitalWrite(SIpin, LOW);
  delayMicroseconds(1*MUL);
  digitalWrite(CLKpin, LOW);

  // Next, read all 256 pixels in parallell. Store the result in the array. Each clock pulse 
  // causes a new pixel to expose its value on the two outputs:
  for(int i=0; i < NPIXELS; i++)
  {
    delayMicroseconds(20*MUL);// <-- We add a delay to stabilize the AO output from the sensor
    intArray[led * NPIXELS + i] = analogRead(AOpin);
    ClockPulse(); 
  }
}

// This function generates an outgoing clock pulse from the Arduino digital pin 'CLKpin'. This clock
// pulse is fed into pin 3 of the linear sensor:
void ClockPulse()
{
  delayMicroseconds(1*MUL);
  digitalWrite(CLKpin, HIGH);
  digitalWrite(CLKpin, LOW);
}

double processImage(int led)
{
  double x0, x1, x2, x3;
  double minstep, maxstep;  //tracks largest step changes in line scan
  int minsteploc, maxsteploc;  //tracks location of largest step change in linescan (pixel)
  int ct;
  int ad_image;

  double a1, b1, c1, a2, b2, c2, m1, m2; //sub pixel quadratic interpolation variables
  double widthsubpixel; 
  
  int offset = led * NPIXELS;

  int filWidth = 0; // width of the filament in pixels

  int startPos = 0, endPos = 0;

  for (int i=3; i<NPIXELS; i++)
  {
    
    if (intArray[offset+i-3] > 1000 && intArray[offset+i-2] > 1000 && intArray[offset+i-1] > 1000 && intArray[offset+i] > 1000)
    {
      startPos = i;
      break;
    }
  }

  for (int i=NPIXELS-4; i>=0;i--)
  {
    if (intArray[offset+i+3] > 1000 && intArray[offset+i+2] > 1000 && intArray[offset+i+1] > 1000 && intArray[offset+i] > 1000)
    {
      endPos = i;
      break;
    }
  }

  minstep = maxstep = 0;
  minsteploc = maxsteploc = 255;
  //clear the sub-pixel buffers
  x0 = x1 = x2 = x3 = 0;
  a1 = b1 = c1 = a2 = b2 = c2 = m1 = m2 = 0;
  widthsubpixel = 0;
  ct = startPos-2;  //index to count samples  need to load buffer for 2 steps to subtract x2-x1

  for (int i=startPos; i<endPos; i++)
  {
    x3=x2;  
    x2=x1;
    x1=x0;
    x0=intArray[offset+i];
    ct = ct + 1;

    if (ct > startPos+1 && ct < endPos-2)
    {
      if (x1+10<x2)
      {
        if (minstep<x2-x1)
        {
          minstep=x2-x1;
          minsteploc=ct;
          c1=x1-x0;
          b1=x2-x1;
          a1=x3-x2;
        }
      } 
      else if(x1 > x2+10)
      {
        if (maxstep<x1-x2)
        {
          maxstep=x1-x2;
          maxsteploc=ct;
          c2=x1-x0;
          b2=x2-x1;
          a2=x3-x2;
        }
      }
    }
  }

  if (minstep>16 && maxstep>16)  //check for significant threshold
  {
    filWidth=maxsteploc-minsteploc;
  } 
  else
    filWidth=0;
  if (filWidth>103)  //check for width overflow or out of range (15.7pixels per mm, 65535/635=103)
    filWidth=0;

  m1=((a1-c1) / (a1+c1-(b1*2)))/2;
  m2=((a2-c2) / (a2+c2-(b2*2)))/2;

  if (filWidth>10)    //check for a measurement > 1mm  otherwise treat as noise
  {
    widthsubpixel=(double)filWidth+m2-m1; 
  }
  else
  {
    widthsubpixel=0;
  }
  return widthsu
}
