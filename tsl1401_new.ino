#include <EEPROM.h>
#include <TimerOne.h>

#define NPIXELS 128  // No. of pixels in array

// Define various ADC prescaler:
const unsigned char PS_32 = (1 << ADPS2) | (1 << ADPS0);
const unsigned char PS_128 = (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);

const byte PWMDAC1pin = 9; // PWM DAC, only pins 9 and 10 are allowed
const byte period = 128; // for 10 bit DAC 

const int LEDpin = 6;

const int CLKpin = 4;    // <-- Arduino pin delivering the clock pulses to pin 3 (CLK) of the TSL1401 
const int SIpin = 5;     // <-- Arduino pin delivering the SI (serial-input) pulse to pin 2 of the TSL1401 
const int AOpin = 1;    // <-- Arduino pin connected to pin 4 (analog output 1)of the TSL1401

unsigned int intArray[NPIXELS]; // <-- the array where the readout of the photodiodes is stored, as integers

int nAggr = 5;        // image averaging count per one scan. should not exceed 10
int timer = 5;        // value output averaging counter

// default filament width
double lowpassValue = 1.75;
double lowpassFactor = 0.5;

double calibFactor =  15.75; // (default pixel per mm = 15.748 for 400dpi sensor

boolean debugMessage = false;
boolean doCalibration = false;

int timerCounter = 0;
double timerValue = 0;


void helpMessage()
{
   if(EEPROM.read(0) != 42)
   {
     Serial.println("[WARNING] This sensor is not calibrated yet, applying default calibration factor");
   }
   
   Serial.println("Filament width sensor based on Thing #454584 (http://www.thingiverse.com/thing:454584) by filpper");
   Serial.println("Modified to use Arduino Pro Micro (from SparkFun electornics) by inornate (http://kuaa.net)");
   Serial.println("==============================     Commands     ================================");
   Serial.println(" [h] help (this screen) / [d] toggle debugging message / [c] calibration with 2mm rod"); 
   Serial.println("==============================  Setting Values  ================================");

   Serial.print("Number of value acquision per scan : ");
   Serial.println(nAggr);

   Serial.print("Voltage output period (unit: scans) : ");
   Serial.println(timer);

   Serial.print("calibration factor (unit: pixel per mm) : ");
   printDouble(calibFactor, 5);
   Serial.println();

   Serial.println("Version : Jan 13 2015");
}


void setup() 
{
  // To set up the ADC, first remove bits set by Arduino library, then choose 
  // a prescaler: PS_16, PS_32, PS_64 or PS_128:
  ADCSRA &= ~PS_128;  
  ADCSRA |= PS_32; // <-- Using PS_32 makes a single ADC conversion take ~30 us

  pinMode(PWMDAC1pin, OUTPUT);  
  Timer1.initialize(period); 

  // Next, assert default setting:
  analogReference(DEFAULT);

  // Set all IO pins low:
  for( int i=0; i< 14; i++ )
  {
    digitalWrite(i, LOW);  
  }

  pinMode(LEDpin, OUTPUT);

  initSensor();
  makeOutput(0);
  readCalibration();

  Serial.begin(115200);
}

void readCalibration()
{
  byte signiture = EEPROM.read(0);
  
  if(signiture == 42) // have calibration data on its EEPROM
  {
    double value = 0.0;
    byte* p = (byte*)(void*)&value;
    for (int i = 0; i < sizeof(value); i++)
       *p++ = EEPROM.read(1+i);
    
    calibFactor = value;
    
    Serial.print("Calibration data read: ");
    printDouble(calibFactor, 5);
    Serial.println();
  }
}

void writeCalibration(double calibValue) // write calibration factor
{
  EEPROM.write(0, 42);    // Signiture value, the answer of everything :D
  
  byte* p = (byte*)(void*)&calibValue;
  for (int i = 0; i < sizeof(calibValue); i++)
    EEPROM.write(1+i, *p++);
}


void loop() 
{  
  int i, j;
  int aggrArray[NPIXELS];

  for(i=0;i< NPIXELS;i++)
  {
    aggrArray[i] = 0;
  }

  digitalWrite(LEDpin, HIGH);
  delay(1);

  for(i=0;i<nAggr;i++)
  {
    // turn on LED

    scanSensor();
    // turn off LED during other jobs
    for(j=0;j<NPIXELS;j++)
    {
      aggrArray[j] += intArray[j];
    }  
  }

  digitalWrite(LEDpin, LOW);

  for(i=0;i< NPIXELS;i++)
  {
    intArray[i] = aggrArray[i] / nAggr;
  }

  double nPixel = processImage();
  lowpassValue = lowpassValue * (1 - lowpassFactor) + nPixel * lowpassFactor;
  double mmValue = lowpassValue / calibFactor;

  // Command processor
  if(Serial.available() > 0)
  {
    char incomingByte = Serial.read();
    switch(incomingByte)
    {
    case 'd':    // Debugging message toggle
      debugMessage = !debugMessage;
      break;
    case 'c':    // Calibration
      doCalibration = true;
      break;
    case 'h':
      helpMessage();
      break;
    }
  }

  if(debugMessage)
  {
    Serial.print("Raw:\t");
    printDouble(lowpassValue, 4);
    Serial.print("\t(mm)=\t");
    printDouble(mmValue, 3);
    Serial.println();
  }
  
  if(mmValue < 1.8 && mmValue > 2.2 && doCalibration)
  {
    Serial.println("Please insert 2mm rod for calibration");
    doCalibration = false;
  }

  timerValue += lowpassValue;  
  timerCounter++;

  if(timerCounter == timer) // measurement have been done [timer] times
  {
    double averaged = timerValue / timer;
    double averagedMM =  averaged / calibFactor;

    timerCounter = 0;
    timerValue = 0;
    
    if(doCalibration)
    {
      calibFactor = averaged / 2;
      Serial.print("Calibration factor has been adjusted to ");
      printDouble(calibFactor, 5);
      Serial.println(" (pixel count per mm)");
      doCalibration = false;
      writeCalibration(calibFactor);
    }

    unsigned int outVal = makeOutput(int(averagedMM * 1000));    // makeOutput get um for its parameter
    if(false && debugMessage)
    {
      Serial.print("Voltage output:\t");
      printDouble(averagedMM, 3);
      Serial.print("\t(");
      Serial.print(outVal);
      Serial.println("/1023)");
    }
  }
}

void printDouble( double val, byte precision){
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
  delayMicroseconds(1);
  digitalWrite(CLKpin, HIGH);
  delayMicroseconds(1);
  digitalWrite(SIpin, LOW);
  delayMicroseconds(1);
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

  Timer1.pwm(PWMDAC1pin, value); // output
  return value;
}

void scanSensor()
{
  // Stop the ongoing integration of light quanta from each photodiode by clocking in a
  // SI pulse:
  // Create a new SI pulse and clock out that same SI pulse through the sensor register:
  digitalWrite(SIpin, HIGH);
  delayMicroseconds(1);
  digitalWrite(CLKpin, HIGH);
  delayMicroseconds(1);
  digitalWrite(SIpin, LOW);
  delayMicroseconds(1);
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
  // delay(1);

  // Stop the ongoing integration of light quanta from each photodiode by clocking in a SI pulse 
  // into the sensors register:
  // Create a new SI pulse and clock out that same SI pulse through the sensor register:
  digitalWrite(SIpin, HIGH);
  delayMicroseconds(1);
  digitalWrite(CLKpin, HIGH);
  delayMicroseconds(1);
  digitalWrite(SIpin, LOW);
  delayMicroseconds(1);
  digitalWrite(CLKpin, LOW);

  // Next, read all 256 pixels in parallell. Store the result in the array. Each clock pulse 
  // causes a new pixel to expose its value on the two outputs:
  for(int i=0; i < NPIXELS; i++)
  {
    delayMicroseconds(20);// <-- We add a delay to stabilize the AO output from the sensor
    intArray[i] = analogRead(AOpin);
    ClockPulse(); 
  }
}

// This function generates an outgoing clock pulse from the Arduino digital pin 'CLKpin'. This clock
// pulse is fed into pin 3 of the linear sensor:
void ClockPulse()
{
  delayMicroseconds(1);
  digitalWrite(CLKpin, HIGH);
  digitalWrite(CLKpin, LOW);
}



double processImage()
{
  double x0, x1, x2, x3;
  double minstep, maxstep;  //tracks largest step changes in line scan
  int minsteploc, maxsteploc;  //tracks location of largest step change in linescan (pixel)
  int ct;
  int ad_image;

  double a1, b1, c1, a2, b2, c2, m1, m2; //sub pixel quadratic interpolation variables
  double widthsubpixel; 

  int filWidth = 0; // width of the filament in pixels

  int startPos = 0, endPos = 0;

  for (int i=3; i<NPIXELS; i++)
  {
    if (intArray[i-3] > 1000 && intArray[i-2] > 1000 && intArray[i-1] > 1000 && intArray[i] > 1000)
    {
      startPos = i;
      break;
    }
  }

  for (int i=NPIXELS-4; i>=0;i--)
  {
    if (intArray[i+3] > 1000 && intArray[i+2] > 1000 && intArray[i+1] > 1000 && intArray[i] > 1000)
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
    x0=intArray[i];
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

  if (filWidth>15)    //check for a measurement > 1mm  otherwise treat as noise
  {
    widthsubpixel=(double)filWidth+m2-m1; 
  }
  else
  {
    widthsubpixel=0;
  }

  return widthsubpixel;
}

