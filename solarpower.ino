/* Solar Power Monitor */
#include <M5Stack.h>
#include <Adafruit_Sensor.h>
#include "DHT12.h"
#include <Wire.h> 
#include <Adafruit_INA219.h>
#include <Adafruit_BMP280.h>

Adafruit_BMP280 bme; 
DHT12 dht12; 
Adafruit_INA219 ina219_A(0x40); // solar panel input
Adafruit_INA219 ina219_B(0x41); // charger input
Adafruit_INA219 ina219_C(0x44); // output

void setup(void) 
{
  M5.begin();
  Wire.begin();
  Wire.setClock(100000); // Std Mode
  
  M5.Lcd.begin();
  M5.Lcd.setBrightness(10);
  M5.Lcd.fillScreen( BLACK );
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.setTextColor( WHITE );  
  M5.Lcd.setTextSize(2);

  M5.Lcd.fillScreen( BLACK );
  M5.Lcd.setCursor(0, 0);
  M5.Lcd.println("Power/ENV Meter");

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
    }
    //while (1);
  }

  uint32_t currentFrequency;
    
  Serial.println("Hello!");
  
  // Initialize the INA219s.
  // By default the initialization will use the largest range (32V, 2A).
  ina219_A.begin();
  ina219_B.begin();
  ina219_C.begin();
  
  // Slightly lower 32V, 1A range (higher precision on amps):
  ina219_A.setCalibration_32V_1A();
  ina219_B.setCalibration_32V_1A();
  // Lower 16V, 400mA range (higher precision on volts and amps):
  ina219_C.setCalibration_16V_400mA();

  Serial.println("Measuring voltages and currents with 3x INA219");
  Serial.println("Measuring temperature and humidity with DHT12");
  Serial.println("Measuring pressure with BMP280");
}

#define PRINT(A)         {M5.Lcd.print(A);}
#define PRINTLN(A)       {M5.Lcd.println(A);}
#define PRINT_FLT(A,D)   {M5.Lcd.print(A,D);}

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

// Estimate percent-of-charge from voltage.
// Very rough! Just linear function between min and max voltages
// Also, this will be inaccurate when the battery is being charged,
// and it also does not take into account the voltage drop due to 
// loading.
float voltage_to_cs(float v) {
  float c = 3; // number of cells
  float L = 3.3*c; // per-cell voltage for 0% charge
  float H = 4.2*c; // per-cell voltage for 100% charge
  float p = (v - L)/(H - L);
  if (p < 0.0) p = 0.0;
  if (p > 1.0) p = 1.0;
  return p;
}

void loop(void) 
{
  M5.Lcd.fillScreen( BLACK );
  M5.Lcd.setCursor(0, 0);

  const float cf_A = 7.0*1.13;
  float shunt_A =   ina219_A.getShuntVoltage_mV();
  float bus_A =     ina219_A.getBusVoltage_V();
  float current_A = cf_A*ina219_A.getCurrent_mA()/1000.0;
  float power_A =   cf_A*ina219_A.getPower_mW()/1000.0;
  float load_A =    bus_A + shunt_A/1000.0;

  const float cf_B = 7.0*1.8;
  float shunt_B =   ina219_B.getShuntVoltage_mV();
  float bus_B =     ina219_B.getBusVoltage_V();
  float current_B = cf_B*ina219_B.getCurrent_mA()/1000.0;
  float power_B =   cf_B*ina219_B.getPower_mW()/1000.0;
  float load_B =    bus_B + shunt_B/1000.0;

  const float cf_C = 7.0*1.1;
  float shunt_C =   ina219_C.getShuntVoltage_mV();
  float bus_C =     ina219_C.getBusVoltage_V();
  float current_C = cf_C*ina219_C.getCurrent_mA()/1000.0;
  float power_C =   cf_C*ina219_C.getPower_mW()/1000.0;
  float load_C =    bus_C + shunt_C/1000.0;

  // Display Detailed Data on LCD
  int cw = 6;
  M5.Lcd.setTextColor( YELLOW ); 
  PRINTLN("POWER");
  M5.Lcd.setTextColor( WHITE );
  PRINTLN("    Panel Charge Output");
  PRINT("B: "); 
    print_flt(bus_A,cw,2);     
    PRINT(" ");
    print_flt(bus_B,cw,2);     
    PRINT(" ");
    print_flt(bus_C,cw,2);
    PRINTLN(" V");
  PRINT("S: ");
    print_flt(shunt_A,cw,2);   
    PRINT(" ");
    print_flt(shunt_B,cw,2);   
    PRINT(" ");   
    print_flt(shunt_C,cw,2);   
    PRINTLN(" mV");
  PRINT("L: ");
    print_flt(load_A,cw,2);    
    PRINT(" ");
    print_flt(load_B,cw,2);    
    PRINT(" ");   
    print_flt(load_C,cw,2);    
    PRINTLN(" V");
  PRINT("C: "); 
    print_flt(current_A,cw,2); 
    PRINT(" ");
    print_flt(current_B,cw,2); 
    PRINT(" ");    
    print_flt(current_C,cw,2); 
    PRINTLN(" A");
  PRINT("P: ");
    print_flt(power_A,cw,2);   
    PRINT(" ");
    print_flt(power_B,cw,2);   
    PRINT(" ");   
    print_flt(power_C,cw,2);   
    PRINTLN(" W");
  PRINTLN("");

  float temperature = dht12.readTemperature();
  float humidity = dht12.readHumidity();
  float pressure = bme.readPressure();

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

  float cs = voltage_to_cs(bus_C);
  float cp = 1400*cs;

  M5.Lcd.setTextColor( RED ); 
  PRINT("STATUS: ");
  M5.Lcd.setTextColor( WHITE); 
  print_flt(100*cs,3,0); PRINT(" %  ");
  print_flt(cp,4,0); PRINT(" Wh");

  // Report Detailed Data via Serial Formatted as JSON
  Serial.println("{");
  Serial.println("  \"power\": {");
  Serial.println("    \"panel\": {"); 
  Serial.print(  "      \"bus_V\": ");       Serial.print(bus_A,2);       Serial.println(",");
  Serial.print(  "      \"shunt_mV\": ");    Serial.print(shunt_A,2);     Serial.println(",");
  Serial.print(  "      \"load_V\": ");      Serial.print(load_A,2);      Serial.println(",");
  Serial.print(  "      \"current_A\": ");   Serial.print(current_A,2);   Serial.println(",");
  Serial.print(  "      \"power_W\": ");     Serial.println(power_A,2);
  Serial.println("    },");
  Serial.println("    \"charge\": {"); 
  Serial.print(  "      \"bus_V\": ");       Serial.print(bus_B,2);       Serial.println(",");
  Serial.print(  "      \"shunt_mV\": ");    Serial.print(shunt_B,2);     Serial.println(",");
  Serial.print(  "      \"load_V\": ");      Serial.print(load_B,2);      Serial.println(",");
  Serial.print(  "      \"current_A\": ");   Serial.print(current_B,2);   Serial.println(",");
  Serial.print(  "      \"power_W\": ");     Serial.println(power_B,2);
  Serial.println("    },");
  Serial.println("    \"output\": {"); 
  Serial.print(  "      \"bus_V\": ");       Serial.print(bus_C,2);       Serial.println(",");
  Serial.print(  "      \"shunt_mV\": ");    Serial.print(shunt_C,2);     Serial.println(",");
  Serial.print(  "      \"load_V\": ");      Serial.print(load_C,2);      Serial.println(",");
  Serial.print(  "      \"current_A\": ");   Serial.print(current_C,2);   Serial.println(",");
  Serial.print(  "      \"power_W\": ");     Serial.println(power_C,2);
  Serial.println("    }");
  Serial.println("  },");
  Serial.println("  \"status\": {");
  Serial.print(  "    \"charge_pcnt\": ");   Serial.print(100*cs,2);      Serial.println(",");
  Serial.print(  "    \"charge_Wh\": ");     Serial.println(cp,2);     
  Serial.println("  },");
  Serial.println("  \"environment\": {");
  Serial.print(  "    \"temperature_C\": "); Serial.print(temperature,2); Serial.println(",");
  Serial.print(  "    \"humidity_pcnt\": "); Serial.print(humidity,2);    Serial.println(",");
  Serial.print(  "    \"pressure_Pa\": ");   Serial.println(pressure,2);  
  Serial.println("  }");
  Serial.println("};");  // use ";" as record separator

  delay(5000);
}
