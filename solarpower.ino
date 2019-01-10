/* Solar Power Monitor 
 * 2019 Michael McCool
 * See initial serial port output (or read it below) for usage instructions.
 * https://github.com/mmccool/solarpower */
#include <M5Stack.h>
#include <Adafruit_Sensor.h>
#include "DHT12.h"
#include <Wire.h> 
#include <Adafruit_INA219.h>
#include <Adafruit_BMP280.h>

// Configuration
const bool pretty = true; // pretty-print JSON data

const float battery_capacity_Wh = 1400;
int cycle = 5000;
const int cycle_inc = 10;
const int grain = 5;

// Sensors
Adafruit_BMP280 bme; 
DHT12 dht12; 
Adafruit_INA219 ina219_A(0x40); // solar panel input
Adafruit_INA219 ina219_B(0x41); // charger input
Adafruit_INA219 ina219_C(0x44); // output

void setup(void) 
{
  M5.begin();
  Wire.begin();
  // Set standard I2C Speed (fast does not work reliably with BMP280)
  Wire.setClock(100000); 
  
  M5.Lcd.begin();
  M5.Lcd.setBrightness(10);
  
  M5.Lcd.fillScreen( BLACK );
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(10);
  M5.Lcd.setTextColor( YELLOW ); 

  M5.Lcd.println("Solar");
  M5.Lcd.println("Power");
  M5.Lcd.println("Monitor");

  delay(3000);
 
  Serial.begin(115200);
  while (!Serial) {
    // pause until serial console opens
    delay(1);
  }
  M5.Lcd.fillScreen( BLACK );

  {
    if (!bme.begin(0x76)) {  
      Serial.println("Could not find a valid BMP280 sensor, check wiring!");
      //while (1);
    }
  }

  uint32_t currentFrequency;
    
  Serial.println("==================================================================");
  Serial.println("Solar Power Monitor");
  Serial.println("==================================================================");
  
  // Initialize the INA219s.
  // By default the initialization will use the largest range (32V, 2A).
  ina219_A.begin();
  ina219_B.begin();
  ina219_C.begin();
  
  // Set custom calibration ranges
  ina219_A.setCalibration_32V_2A();  // Panel max 21V
  ina219_B.setCalibration_32V_2A();  // Charger nominal 16V, but may be slightly higher
  ina219_C.setCalibration_16V_400mA(); // Battery max 12.6V

  Serial.println("Measuring voltages and currents with INA219s");
  Serial.println("Measuring temperature and humidity with DHT12");
  Serial.println("Measuring pressure with BMP280");
  Serial.println("------------------------------------------------------------------");

  Serial.println("Parser should search up to first semicolon character and discard");
  Serial.println("and discard it and everything before it.  After that, each record");
  Serial.println("is separated by another semicolon.  Note that CR/LF is NOT a");
  Serial.println("record separator, as when in pretty-printing mode long lines are");
  Serial.println("broken up for readability.  Records consist of a pseudo-identifier");
  Serial.println("in quotes, followed by a colon, followed by some JSON for the"); 
  Serial.println("content.  The identifier is an echo of the command when a command");
  Serial.println("is used to elicit a response, although note the system may also");
  Serial.println("return data changed as the result of spontaneous events as well.");
  Serial.println("Data returned also generally has a cycle index and a timestamp");
  Serial.println("based on the number of elapsed microseconds since the system was");
  Serial.println("last initialized.");
  Serial.println("------------------------------------------------------------------");
  Serial.println("COMMANDS");
  Serial.println("In the following, <n> should be replaced with an integer.");
  Serial.println("All commands should also be terminated with a semicolon.");
  Serial.println("a    - return all state, including modes and latest sensor readings");
  Serial.println("c    - return all the latest power readings");
  Serial.println("c<n> - return the power readings for channel n");
  Serial.println("e    - return current environmental readings");
  Serial.println("d    - return current display mode");
  Serial.println("d<n> - set display mode");
  Serial.println("s    - return estimated battery charge status");
  Serial.println("o    - return current observe mode");
  Serial.println("o<n> - set observe mode on (n=1) or off (n=0)");
  Serial.println("y    - return current cycle time (sampling period)");
  Serial.println("y<n> - set current cycle time (number of grains)");
  Serial.println("------------------------------------------------------------------");
  Serial.println("EVENTS");
  Serial.println("The following may be returned spontaneously:");
  Serial.println("A    - button A event (payload is a string giving press type)");
  Serial.println("B    - button B event (payload is a string giving press type)");
  Serial.println("C    - button C event (payload is a string giving press type)");
  Serial.println("Other records for modified data may follow events or be issued");
  Serial.println("autonomously if observe mode is on");

  // End of preamble/start of parseable data 
  Serial.print(  "===================================================================");
  Serial.println(";"); // first record separator
}

// Channel configuration
const unsigned int N_CHANNELS = 3;
// Correction factors
const float cf[N_CHANNELS] = {
  7.0*1.13,
  7.0*1.8,
  7.0*1.1 
};
// Channel Names (JSON, Display)
const char* cn[N_CHANNELS][2] = {
  { "c0",  "Panel" },
  { "c1", "Charge" },
  { "c2", "Output" }
};

// Last data read or computed
float bus_V[N_CHANNELS];
float shunt_mV[N_CHANNELS];
float load_V[N_CHANNELS];
float current_A[N_CHANNELS];
float power_W[N_CHANNELS];

float temperature;
float humidity;
float pressure;

float cs;  // charge state; estimate % of capacity
float cp;  // charge state; estimated Wh

// Estimate percent-of-charge from voltage.
// Very rough! Just linear function between min and max voltages
// Also, this will be inaccurate when the battery is being charged,
// and it also does not take into account the voltage drop due to 
// loading.
float voltage_to_cs(float v) {
  const float L = 10.00; // voltage for 0% charge (about 3.3V per cell)
  const float H = 12.50; // voltage for 100% charge ("overcharge > 100% possible)
  float p = (v - L)/(H - L);
  if (p < 0.0) p = 0.0;
  return p;
}

// Read data from sensors, compute derived values,
// and store in above variables
void read_sensors () {
  // voltage, current, and power on channel A/0 (solar panel input)
  shunt_mV[0] =  ina219_A.getShuntVoltage_mV();
  bus_V[0] =     ina219_A.getBusVoltage_V();
  current_A[0] = cf[0]*ina219_A.getCurrent_mA()/1000.0;
  power_W[0] =   cf[0]*ina219_A.getPower_mW()/1000.0;
  load_V[0] =    bus_V[0] + shunt_mV[0]/1000.0;

  // voltage, current, and power on channel B/1 (backup charger) 
  shunt_mV[1] =  ina219_B.getShuntVoltage_mV();
  bus_V[1] =     ina219_B.getBusVoltage_V();
  current_A[1] = cf[1]*ina219_B.getCurrent_mA()/1000.0;
  power_W[1] =   cf[1]*ina219_B.getPower_mW()/1000.0;
  load_V[1] =    bus_V[1] + shunt_mV[1]/1000.0;
 
  // voltage, current, and power on channel C/2 (load)
  shunt_mV[2] =  ina219_C.getShuntVoltage_mV();
  bus_V[2] =     ina219_C.getBusVoltage_V();
  current_A[2] = cf[2]*ina219_C.getCurrent_mA()/1000.0;
  power_W[2] =   cf[2]*ina219_C.getPower_mW()/1000.0;
  load_V[2] =    bus_V[2] + shunt_mV[2]/1000.0;

  // environmental sensors
  temperature = dht12.readTemperature();
  humidity = dht12.readHumidity();
  pressure = bme.readPressure();

  // charge state estimates
  cs = voltage_to_cs(bus_V[2]); // load voltage is also battery voltage
  cp = battery_capacity_Wh*cs;
}

#define PRINT(A)         {M5.Lcd.print(A);}
#define PRINTLN(A)       {M5.Lcd.println(A);}
#define PRINT_FLT(A,D)   {M5.Lcd.print(A,D);}

// Print a float right-justified in a field of with w with d digits
// to the right of the decimal (if 0, no decimal point printed)
void print_flt(float f, int w, int d) {
  bool nf = (f < 0.0);
  float af = nf ? -f : f;
  int id = 1;
  float aaf = af;
  while (aaf >= 10.0) {
    aaf /= 10;
    id++;
  }
  int ed = w - id;
  if (d > 0) ed -= (d + 1);
  if (nf) ed -= 1;
  while (ed > 0) {
    PRINT(" ");
    ed--;
  }
  if (nf) PRINT("-");
  PRINT_FLT(af,d);
}

// record index
int rec_index = 0;

// display mode
const unsigned int DP_DETAILED = 0;
const unsigned int DP_CHARGE_PERCENT = 1;
const unsigned int DP_BATTERY_VOLTAGE = 2;
const unsigned int DP_PANEL_POWER = 3;
const unsigned int DP_CYCLE_TIME = 4;
const unsigned int N_DP = 5;
unsigned int disp_mode = DP_DETAILED;

// observation mode
bool observe_mode = false;

// display detailed data on LCD
void disp_detailed() {
  if (0 == rec_index) return; // no data yet

  // Blank display, initialize options
  M5.Lcd.setTextSize(2);
  M5.Lcd.fillScreen( BLACK );
  M5.Lcd.setCursor(0, 0);
  int cw = 6; // column width

  // Power Flow Data
  M5.Lcd.setTextColor( YELLOW ); 
  PRINTLN("POWER");
  M5.Lcd.setTextColor( WHITE );
  PRINTLN("    Panel Charge Output");
  PRINT("B: "); 
    print_flt(bus_V[0],cw,2);     
    PRINT(" ");
    print_flt(bus_V[1],cw,2);     
    PRINT(" ");
    print_flt(bus_V[2],cw,2);
    PRINTLN(" V");
  PRINT("S: ");
    print_flt(shunt_mV[0],cw,2);   
    PRINT(" ");
    print_flt(shunt_mV[1],cw,2);   
    PRINT(" ");   
    print_flt(shunt_mV[2],cw,2);   
    PRINTLN(" mV");
  PRINT("L: ");
    print_flt(load_V[0],cw,2);    
    PRINT(" ");
    print_flt(load_V[1],cw,2);    
    PRINT(" ");   
    print_flt(load_V[2],cw,2);    
    PRINTLN(" V");
  PRINT("C: "); 
    print_flt(current_A[0],cw,2); 
    PRINT(" ");
    print_flt(current_A[1],cw,2); 
    PRINT(" ");    
    print_flt(current_A[2],cw,2); 
    PRINTLN(" A");
  PRINT("P: ");
    print_flt(power_W[0],cw,2);   
    PRINT(" ");
    print_flt(power_W[1],cw,2);   
    PRINT(" ");   
    print_flt(power_W[2],cw,2);   
    PRINTLN(" W");
  PRINTLN("");

  // Environmental Data
  M5.Lcd.setTextColor( GREEN ); 
  PRINTLN("ENVIRONMENT");
  M5.Lcd.setTextColor( WHITE );
  PRINT("T: "); 
    PRINT(temperature);  
    PRINTLN(" C");
  PRINT("H: "); 
    PRINT(humidity);     
    PRINTLN(" %");
  PRINT("P: "); 
    PRINT(pressure);     
    PRINTLN(" Pa");
  PRINTLN("");  

  // Charge Status (Estimated)
  M5.Lcd.setTextColor( RED ); 
  PRINT("STATUS: ");
  M5.Lcd.setTextColor( WHITE ); 
  print_flt(100*cs,3,0); PRINT(" %  ");
  print_flt(cp,4,0); PRINT(" Wh"); 
}

// display charge percent only on LCD
void disp_charge_percent() {
  if (0 == rec_index) return; // no data yet

  // Blank display, initialize options
  M5.Lcd.setTextSize(15);
  M5.Lcd.fillScreen( BLACK );
  M5.Lcd.setCursor(0, 0);

  M5.Lcd.setTextColor( WHITE ); 
  PRINTLN("CHARGE");
  print_flt(100*cs,3,0); PRINT("%");
}

// display battery voltage only on LCD
void disp_battery_voltage() {
  if (0 == rec_index) return; // no data yet

  // Blank display, initialize options
  M5.Lcd.setTextSize(15);
  M5.Lcd.fillScreen( BLACK );
  M5.Lcd.setCursor(0, 0);

  M5.Lcd.setTextColor( WHITE ); 
  PRINTLN("BATTERY");
  print_flt(bus_V[2],6,2); PRINT("V");
}

// display panel power only on LCD
void disp_panel_power() {
  if (0 == rec_index) return; // no data yet

  // Blank display, initialize options
  M5.Lcd.setTextSize(15);
  M5.Lcd.fillScreen( BLACK );
  M5.Lcd.setCursor(0, 0);

  M5.Lcd.setTextColor( WHITE ); 
  PRINTLN("PANEL");
  print_flt(power_W[0],6,2); PRINT("W");
}

// display cycle time only on LCD
void disp_cycle_time() {
  if (0 == rec_index) return; // no data yet

  // Blank display, initialize options
  M5.Lcd.setTextSize(15);
  M5.Lcd.fillScreen( BLACK );
  M5.Lcd.setCursor(0, 0);

  M5.Lcd.setTextColor( WHITE ); 
  PRINTLN("CYCLE");
  print_flt(cycle/1000.0,6,2); PRINT("s");
}

void pi(int w) {
  if (pretty) while (w > 0) {
    Serial.print(" ");
    w--;
  }
}

void ps() {
  pi(1);
}

void pe(const char* text = "") {
  if (pretty) {
    Serial.println(text);
  } else {
    Serial.print(text);
  }
}

void send_rec(int w = 0) {
  // cycle index
  pi(w);
  Serial.print("\"index\":");              
  ps();
  Serial.print(rec_index);
  pe(",");

  // timestamp
  pi(w);
  Serial.print("\"timestamp\":");              
  ps();
  Serial.print(micros());
}
void send_channel(unsigned int ch, bool rec = true, int w = 0) {
  if (ch >= N_CHANNELS) return;

  Serial.print("{"); 
  pe();

  if (rec) {
    send_rec(w+2);    
    pe(","); 
  }

  pi(w+2); 
  Serial.print("\"bus_V\":"); 
  ps(); 
  Serial.print(bus_V[ch],2); 
  pe(","); 

  pi(w+2); 
  Serial.print("\"shunt_mV\":"); 
  ps(); 
  Serial.print(shunt_mV[ch],2); 
  pe(","); 

  pi(w+2); 
  Serial.print("\"load_V\":"); 
  ps(); 
  Serial.print(load_V[ch],2); 
  pe(","); 

  pi(w+2); 
  Serial.print("\"current_A\":"); 
  ps(); 
  Serial.print(current_A[ch],2); 
  pe(","); 

  pi(w+2); 
  Serial.print("\"power_W\":"); 
  ps(); 
  Serial.print(power_W[ch],2); 
  Serial.print(","); 
  pe();

  pi(w);
  Serial.print("}");
}
void send_power(int w = 0, bool rec = true) {
  Serial.print("{");
  pe();

  if (rec) {
    send_rec(w+2);    
    Serial.print(",");
    pe();
  }

  pi(w+2); 
  Serial.print("\"");
  Serial.print(cn[0][0]);
  Serial.print("\":");
  ps();
  send_channel(0,false,w+2);
  pe(",");

  pi(w+2);
  Serial.print("\"");
  Serial.print(cn[1][0]);
  Serial.print("\":");
  ps();
  send_channel(1,false,w+2);
  pe(",");

  pi(w+2);
  Serial.print("\"");
  Serial.print(cn[2][0]);
  Serial.print("\":");
  ps();
  send_channel(2,false,w+2);
  pe();

  pi(w);
  Serial.print("}");
}
void send_env(int w = 0, bool rec = true) {
  Serial.print("{");
  pe();

  if (rec) {
    send_rec(w+2);    
    pe(","); 
  }
  
  pi(w+2);
  Serial.print("\"temperature_C\":"); 
  ps();
  Serial.print(temperature,2); 
  pe(",");

  pi(w+2);
  Serial.print("\"humidity_pcnt\":");
  ps();
  Serial.print(humidity,2);
  pe(",");

  pi(w+2);
  Serial.print("\"pressure_Pa\": ");
  ps();
  Serial.print(pressure,2);  
  pe();

  pi(w);
  Serial.print("}");
}
void send_status(int w = 0, bool rec = true) {
  Serial.print("{");
  pe();

  if (rec) {
    send_rec(w+2);    
    Serial.print(",");
    pe();
  }

  pi(w+2);
  Serial.print("\"charge_pcnt\":"); 
  ps();
  Serial.print(100*cs,2); 
  pe(",");
    
  pi(w+2);
  Serial.print("\"charge_Wh\":"); 
  ps();
  Serial.print(cp,2);
  pe();

  pi(w);
  Serial.print("}");
}
void send_disp_mode(int w = 0, bool rec = true) {
  Serial.print("{");
  pe();

  if (rec) {
    send_rec(w+2);    
    Serial.print(",");
    pe();
  }

  pi(w+2);
  Serial.print("\"disp_mode\":"); 
  ps();
  Serial.print(disp_mode); 
  pe();
    
  pi(w);
  Serial.print("}");
}
void send_observe_mode(int w = 0, bool rec = true) {
  Serial.print("{");
  pe();

  if (rec) {
    send_rec(w+2);    
    Serial.print(",");
    pe();
  }

  pi(w+2);
  Serial.print("\"observe_mode\":"); 
  ps();
  Serial.print(observe_mode ? 1 : 0); 
  pe();
    
  pi(w);
  Serial.print("}");
}
static int cycle_count = cycle/grain;
static int count = 0;
void send_period(int w = 0, bool rec = true) {
  Serial.print("{");
  pe();

  if (rec) {
    send_rec(w+2);    
    Serial.print(",");
    pe();
  }

  pi(w+2);
  Serial.print("\"grain\":"); 
  ps();
  Serial.print(grain); 
  pe(",");

  pi(w+2);
  Serial.print("\"cycle\":"); 
  ps();
  Serial.print(cycle); 
  pe(",");

  pi(w+2);
  Serial.print("\"count\":"); 
  ps();
  Serial.print(count); 
  pe(",");

  pi(w+2);
  Serial.print("\"cycle_count\":"); 
  ps();
  Serial.print(cycle_count); 
  pe();
    
  pi(w);
  Serial.print("}");
}

void send_all() {
  // Report Detailed Data via Serial Formatted as JSON
  Serial.print("{");
  pe();
  
  send_rec(2); 
  pe(",");
  
  pi(2); 
  Serial.print("\"c\":"); 
  ps();
  send_power(2,false); 
  pe(",");
  
  pi(2); 
  Serial.print("\"e\":"); 
  ps();
  send_env(2,false); 
  pe(",");
  
  pi(2); 
  Serial.print("\"s\":"); 
  ps();
  send_status(2,false); 
  pe(",");

  pi(2); 
  Serial.print("\"d\":"); 
  ps();
  send_disp_mode(2,false); 
  pe(",");

  pi(2); 
  Serial.print("\"o\":"); 
  ps();
  send_observe_mode(2,false); 
  pe(",");

  pi(2); 
  Serial.print("\"y\":"); 
  ps();
  send_period(2,false); 
  pe();
  
  Serial.print("}");
}

void loop(void) 
{
  M5.update();
  bool ui_update = false;
  
  // If A button released, rotate display modes
  if (M5.BtnA.wasReleased()) {
    Serial.print("\"A\":");
    ps();
    Serial.println("\"released\";");

    disp_mode = (disp_mode + 1) % N_DP;

    Serial.print("\"d\":");
    ps();
    send_disp_mode();
    Serial.println(";");

    ui_update = true;
  }  
  // If B button released, increase update interval
  if (M5.BtnB.wasReleased()) {
    Serial.print("\"B\":");
    ps();
    Serial.println("\"released\";");

    switch (disp_mode) {
      case DP_CYCLE_TIME: {
        cycle_count += cycle_inc;
        cycle = grain * cycle_count;

        Serial.print("\"y\":");
        ps();
        send_period();
        Serial.println(";");

        ui_update = true;
      } break;
      default: {
      } break;
    }
  } 
  // If C button released, decrease update interval
  if (M5.BtnC.wasReleased()) {
    Serial.print("\"C\":");
    ps();
    Serial.println("\"released\";");

    switch (disp_mode) {
      case DP_CYCLE_TIME: {
        cycle_count -= cycle_inc;
        if (cycle_count < 0) cycle_count = 0;
        cycle = grain * cycle_count;

        Serial.print("\"y\":");
        ps();
        send_period();
        Serial.println(";");

        ui_update = true;
      } break;
      default: {
      } break;
    }
  } 

  if (0 == count) {
    // Update sensor readings
    read_sensors();
    rec_index++;
  }

  // Read command from serial input
  //  Reads and executes just one command per cycle to
  //  avoid starvation of sensor readings
  const int length = 100;
  char buffer[length+2];
  if (Serial.available()) {
    // parse a command; ";" is used as a termination character
    int k = Serial.readBytesUntil(';', buffer, length);
    if (k <= length) {
      buffer[k] = '\0';  // make sure buffer is null-terminated
      // find first non-whitespace starting character
      int i = 0;
      while (i < k && isWhitespace(buffer[i])) i++;
      // process command, if there is one
      if (i < k && isPrintable(buffer[i])) {
        Serial.print("\"");
        Serial.print(buffer);
        Serial.print("\":");
        ps();
        char cmd = buffer[i];
        switch (cmd) {
          case 'a': {
            if (i + 1 == k) {
              send_all();
            } else {
              Serial.print("\"unknown\"");
            }
            Serial.println(";");
          } break;
          case 'c': {
            if (i+1 == k) {
              // all channels
              send_power(); 
            } else {
              // get channel number
              unsigned int ch = (unsigned int)(buffer[i+1] - '0');
              // if a valid channel, print out data
              if (ch < N_CHANNELS) {
                send_channel(ch); 
              } else {
                Serial.print("\"unknown\"");
              }
            }
            Serial.println(";");
          } break;
          case 'd': {
            if (isDigit(buffer[i+1])) {
              unsigned int new_disp_mode = (unsigned int)(buffer[i+1] - '0');
              if (new_disp_mode < N_DP) {
                disp_mode = new_disp_mode;
                send_disp_mode();
              } else {
                Serial.print("\"unknown\"");
              }
            } else {
              send_disp_mode();
            }
            Serial.println(";");
          } break;
          case 'o': {
            if (isDigit(buffer[i+1])) {
              unsigned int new_observe_mode = (unsigned int)(buffer[i+1] - '0');
              if (new_observe_mode <= 1) {
                observe_mode = (1 == new_observe_mode);
                send_observe_mode();
              } else {
                Serial.print("\"unknown\"");
              }
            } else {
              send_observe_mode();
            }
            Serial.println(";");
          } break;
          case 'e': {
            if (i + 1 == k) {
              send_env(); 
            } else {
              Serial.print("\"unknown\"");
            }
            Serial.println(";");
          } break;
          case 's': {
            if (i + 1 == k) {
              send_status(); 
            } else {
              Serial.print("\"unknown\"");
            }
            Serial.println(";");
          } break;
          case 'y': {
            if (isDigit(buffer[i+1])) {
              unsigned int new_cycle_count = (unsigned int)(buffer[i+1] - '0');
              int j = 2;
              while (i+j < k && isDigit(buffer[i+j])) {
                new_cycle_count = 10*new_cycle_count
                                + (unsigned int)(buffer[i+j] - '0');
                j++;
              }
              cycle_count = new_cycle_count;
              cycle = grain * cycle_count;
            }
            send_period();
            Serial.println(";");
          } break;
          default: {
            Serial.println("\"unknown\";");
          } break;
        }
      }
    }
  }

  // Send all data automatically each cycle in "observe" mode
  if (0 == count && observe_mode && 'a' != buffer[0]) {
    Serial.print("\"a:\"");
    ps();
    send_all();
    Serial.println(";");
  }

  // Update UI if necessary
  if (0 == count || ui_update) {
    // Display data using selected mode
    switch (disp_mode) {
       case DP_DETAILED:
         disp_detailed();
         break;
       case DP_CHARGE_PERCENT:
         disp_charge_percent();
         break;
       case DP_BATTERY_VOLTAGE:
         disp_battery_voltage();
         break;
       case DP_PANEL_POWER:
         disp_panel_power();
         break;
       case DP_CYCLE_TIME:
         disp_cycle_time();
         break;
       default:
         // Unknown display mode! Reset to 0
         disp_mode = 0;
         Serial.println("\"d\":");
         ps();
         send_disp_mode();
         Serial.println(";");
         break;
    };
  }

  delay(grain);
  count = (count + 1) % (cycle_count + 1);
}
