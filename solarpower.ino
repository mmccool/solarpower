/* Solar Power Monitor */
#include <M5Stack.h>
#include <Adafruit_Sensor.h>
#include "DHT12.h"
#include <Wire.h> 
#include <Adafruit_INA219.h>
#include <Adafruit_BMP280.h>

// Configuration
const bool pretty = true; // pretty-print JSON data
const float battery_capacity_Wh = 1400;
const int cycle = 5000;
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
  Wire.setClock(100000); // Std I2C Speed (Fast does not work reliably with BMP280)
  
  M5.Lcd.begin();
  M5.Lcd.setBrightness(10);
  
  M5.Lcd.fillScreen( BLACK );
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextSize(2);
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
    
  Serial.println("Solar Power Monitor");
  
  // Initialize the INA219s.
  // By default the initialization will use the largest range (32V, 2A).
  ina219_A.begin();
  ina219_B.begin();
  ina219_C.begin();
  
  // Slightly lower 32V, 1A range (higher precision on amps):
  ina219_A.setCalibration_32V_1A();  // Panel max 21V
  ina219_B.setCalibration_32V_1A();  // Charger nominal 16V, but may be slightly higher
  // Lower 16V, 400mA range (higher precision on volts and amps):
  ina219_C.setCalibration_16V_400mA(); // Battery max 12.6V

  Serial.println("Measuring voltages and currents with INA219s");
  Serial.println("Measuring temperature and humidity with DHT12");
  Serial.println("Measuring pressure with BMP280");
}

// Channel configuration
const unsigned int N_CHANNELS = 3;
// correction factors
const float cf[N_CHANNELS] = {
  7.0*1.13,
  7.0*1.8,
  7.0*1.1 
};
// names (JSON, Display)
const char* cn[N_CHANNELS][2] = {
  { "panel",  "Panel" },
  { "charge", "Charge" },
  { "output", "Output" }
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
const unsigned int N_DP = 4;
unsigned int disp_mode = DP_DETAILED;

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
  pi(w);
  Serial.print("\"id\":");              
  ps();
  Serial.print(rec_index);
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
void send_power(int w = 0, bool rec = false) {
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
void send_charge(int w = 0, bool rec = false) {
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

  Serial.print(  "    \"charge_Wh\": ");     Serial.println(cp,2);     
  pi(w+2);
  Serial.print("\"charge_Wh\":"); 
  ps();
  Serial.print(cp,2); 

  pi(w);
  Serial.print("}");
}
void send_env(int w = 0, bool rec = false) {
  Serial.println("{");

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

void send_all() {
  // Report Detailed Data via Serial Formatted as JSON
  Serial.println("{");
  send_rec(2); pe(",");
  pi(2); Serial.print("\"power\":"); ps();
  send_power(2,false); pe(",");
  pi(2); Serial.print("\"environment\":"); ps();
  send_env(2,false); pe(",");
  pi(2); Serial.print("\"status\":"); ps();
  send_env(2,false); pe();
  pe("}"); 
}

static int cycle_count = cycle/grain + 1;
static int count = 0;

void loop(void) 
{
  M5.update();
  bool ui_update = false;
  
  // If A button released, rotate display modes
  if (M5.BtnA.wasReleased()) {
    Serial.println("BtnA.wasReleased;");
    disp_mode = (disp_mode + 1) % N_DP;
    ui_update = true;
  }  
  // If B button released, increase update interval
  if (M5.BtnB.wasReleased()) {
    Serial.println("BtnB.wasReleased;");
    cycle_count += 50;
    //Serial.print(cycle_count);    
    //Serial.println(";");
    ui_update = true;
  } 
  // If C button released, decrease update interval
  if (M5.BtnC.wasReleased()) {
    Serial.println("BtnC.wasReleased;");
    cycle_count -= 50;
    if (cycle_count <= 0) cycle_count = 1;
    //Serial.print(cycle_count);    
    //Serial.println(";");
    ui_update = true;
  } 

  if (0 == count) {
    // Update sensor readings
    read_sensors();
    rec_index++;
  }
  
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
       default:
         Serial.println("Unknown display mode!");
         break;
    };
  }

  // Read commands from serial input; as long as there are
  //  some, execute them until buffer is empty

  if (0 == count) {
    send_all();
    Serial.println(";");
  }

  delay(grain);
  count = (count + 1) % cycle_count;
}
