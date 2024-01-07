/* 
 * The sauce of the UAV's teststand2 controller
 * 
 * NYCU UAV Team 2023
 * 
 * 
 * Pin diagram:
 *                  #########
 *            3V3 +-#-------#-+ GND
 *             EN + #       # + IO23
 *           IO36 + #       # + IO22
 *           IO39 + #       # + IO01
 *           IO34 + #       # + IO03
 *           IO35 + ######### + IO21
 *  BAT_VOLT IO32 +           + GND
 *  RPM_DOUT IO33 +   ESP32   + IO19
 *  CUR_AOUT IO25 +  DevKitC  + IO18
 *   THR_SCK IO25 +           + IO05
 *    THR_DT IO27 +           + IO17
 *     LED_Y IO14 +           + IO16
 *     LED_G IO12 +           + IO04
 *            GND +           + IO00 
 *           IO13 +           + IO02
 *           IO09 +           + IO15 SAFETY_SWITCH
 *           IO10 +    ****   + IO08
 *           IO11 +    ****   + IO07
 *            5V0 +----****---+ IO06
 * 
 * This controller will talk to the host computer via json over serial.
 * There are two types of commands:
 * 1. sys_init: Initialize the system. This command will check if the system is ready to run.
 * 2. measure: Measure the thrust, rpm, current, and voltage of the motor. This command will also contain measurement parameters.
 * 
 * The controller will send back the result in json format.
 * 1. sys_init: The controller will send back a boolean value indicating if the system is ready to run.
 *              Format: {"response_type": "sys_init", "ok": true}
 * 2. measure: The controller will send back an array of measurements.
 *             Format: {"response_type": "measure", "ok": true, "data": [{"throttle": 0, "rpm": 0, "current": 0, "thrust": 0, "voltage": 0}, ...]}
 * 
 */

#include <Arduino.h>

#include <ArduinoJson.h>
#include <HX711.h>
#include <ESP32Servo.h>

//
// Constants
//

// Measuring params
#define MEASURE_AVERAGE_N   10   // Total number of measurements to take average of
#define MEASURE_DELAY_MS    10   // Delay between each measurements    
#define CURRENT_SCALE       63.573     // Convert ADC reading to Amp (with 25.1 mOhm shunt and 10x amp)
#define THRUST_SCALE        117105.75  // Convert the hx711 raw reading to kilogram
#define BAT_VOLTAGE_SCALE   8.7355     // Convert ADC reading to volt
#define MAX_MEASURE_STEPS   20

// PIN config
#define SAFETY_SWITCH_PIN   15
#define RPM_DOUT_PIN        33
#define THRUST_DT_PIN       27
#define THRUST_SCK_PIN      26
#define CURRENT_AOUT_PIN    25
#define LED_GREEN_PIN       12
#define LED_YELLOW_PIN      14
#define BAT_VOLTAGE_PIN     32
#define ESC_COMMAND_PIN     13

// Conversion
#define S_TO_MILLIS   1000.0

//
// Global variables
//

struct Measurements {
    float throttle = 0.0;
    int rpm =        0;
    float thrust =   0.0;
    float current =  0.0;
    float voltage =  0.0;
};

Measurements measurements[MAX_MEASURE_STEPS + 1];

float current_offset = 0.0;

unsigned long standby_ts = 0;
bool is_gled_on = true;

volatile bool system_paused = false;
volatile int rpm_count = 0;

HX711 scale;
Servo esc;

//
// ISRs
//

// Safty switch interrupt handling
void system_pause_isr() {
    system_paused = true;
}

// RPM measuring interrupt handling
void rpm_counting_isr() {
    rpm_count++;
}

//
// Functions
//

// Measure rpm
int measure_rpm() {
    unsigned long current_time = millis();
    unsigned long time_lapse_ms = 1000;
    rpm_count = 0;
    while (millis() - current_time < time_lapse_ms) {}
    return rpm_count * (60 * S_TO_MILLIS / time_lapse_ms);
}

// Measure current
float measure_current(float offset = 0.0) {
    float sum = 0.0;
    for (int i = 0; i < MEASURE_AVERAGE_N * 5; i++) {
        sum += static_cast<float>(analogRead(CURRENT_AOUT_PIN)) * (3.3 / 4095.0) * CURRENT_SCALE + offset;;
        delay(MEASURE_DELAY_MS);
    }
    return sum / static_cast<float>(MEASURE_AVERAGE_N * 5);
}

// Measure thrust
float measure_thrust() {
    float sum = 0.0;
    for (int i = 0; i < MEASURE_AVERAGE_N; i++) {
        sum += scale.get_units();
        delay(MEASURE_DELAY_MS);
    }
    return sum / static_cast<float>(MEASURE_AVERAGE_N);
}

// Measure voltage
float measure_voltage() {
    float sum = 0.0;
    for (int i = 0; i < MEASURE_AVERAGE_N; i ++) {
        sum += static_cast<float>(analogRead(BAT_VOLTAGE_PIN)) * (3.3 / 4095.0) * BAT_VOLTAGE_SCALE;
        delay(MEASURE_DELAY_MS);
    }
    return sum / static_cast<float>(MEASURE_AVERAGE_N);
}

// Set throttle
void set_throttle(float throttle) {
    int esc_value = map(static_cast<int>(throttle * 100.0), 0, 100, 0, 180);
    esc.write(esc_value);
}

// Handle all the measurements
void measure(int current_step, int total_steps, float throttle_scale = 1.0) {
    float throttle = static_cast<float>(current_step) / static_cast<float>(total_steps);
    set_throttle(throttle * throttle_scale);
    delay(1000);

    measurements[current_step].throttle = static_cast<int>(throttle * 100);
    measurements[current_step].rpm =      measure_rpm();
    measurements[current_step].current =  measure_current(current_offset);
    measurements[current_step].thrust =   measure_thrust();
    measurements[current_step].voltage =  measure_voltage();

    // Soft 100% throttle ramp-down
    if (throttle == 1.0) {
        set_throttle(0.75);
        delay(300);
        set_throttle(0.5);
        delay(300);
        set_throttle(0.25);
        delay(300);
    }
}

// Handle system initialization
bool sys_init() {
    const float max_rpm = 60.0;
    const float max_current = 5.0;
    const float min_voltage = 3.0;
    const float max_thrust = 1.0;

    if (measure_rpm() > max_rpm) {
        digitalWrite(LED_GREEN_PIN, LOW);
        is_gled_on = false;
        /* Pattern start */
        delay(100);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        delay(250);
        digitalWrite(LED_YELLOW_PIN, LOW);
        delay(100);
        /* Pattern end */
        digitalWrite(LED_GREEN_PIN, HIGH);
        is_gled_on = true;

        return false;
    }
    if (measure_current(current_offset) > max_current) {
        digitalWrite(LED_GREEN_PIN, LOW);
        is_gled_on = false;
        /* Pattern start */
        delay(100);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        delay(250);
        digitalWrite(LED_YELLOW_PIN, LOW);
        delay(100);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        delay(250);
        digitalWrite(LED_YELLOW_PIN, LOW);
        delay(100);
        /* Pattern end */
        digitalWrite(LED_GREEN_PIN, HIGH);
        is_gled_on = true;

        return false;
    }
    if (measure_voltage() < min_voltage) {
        digitalWrite(LED_GREEN_PIN, LOW);
        is_gled_on = false;
        /* Pattern start */
        delay(100);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        delay(250);
        digitalWrite(LED_YELLOW_PIN, LOW);
        delay(100);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        delay(250);
        digitalWrite(LED_YELLOW_PIN, LOW);
        delay(100);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        delay(250);
        digitalWrite(LED_YELLOW_PIN, LOW);
        delay(100);
        /* Pattern end */
        digitalWrite(LED_GREEN_PIN, HIGH);
        is_gled_on = true;

        return false;
    }
    if (measure_thrust() > max_thrust) {
        digitalWrite(LED_GREEN_PIN, LOW);
        is_gled_on = false;
        /* Pattern start */
        delay(100);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        delay(250);
        digitalWrite(LED_YELLOW_PIN, LOW);
        delay(100);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        delay(250);
        digitalWrite(LED_YELLOW_PIN, LOW);
        delay(100);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        delay(250);
        digitalWrite(LED_YELLOW_PIN, LOW);
        delay(100);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        delay(250);
        digitalWrite(LED_YELLOW_PIN, LOW);
        delay(100);
        /* Pattern end */
        digitalWrite(LED_GREEN_PIN, HIGH);
        is_gled_on = true;

        return false;
    }
    if (system_paused) {
        digitalWrite(LED_GREEN_PIN, LOW);
        is_gled_on = false;
        /* Pattern start */
        delay(100);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        delay(250);
        digitalWrite(LED_YELLOW_PIN, LOW);
        delay(100);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        delay(250);
        digitalWrite(LED_YELLOW_PIN, LOW);
        delay(100);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        delay(250);
        digitalWrite(LED_YELLOW_PIN, LOW);
        delay(100);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        delay(250);
        digitalWrite(LED_YELLOW_PIN, LOW);
        delay(100);
        digitalWrite(LED_YELLOW_PIN, HIGH);
        delay(250);
        digitalWrite(LED_YELLOW_PIN, LOW);
        delay(100);
        /* Pattern end */
        digitalWrite(LED_GREEN_PIN, HIGH);
        is_gled_on = true;

        return false;
    }

    // Calibrate one more time
    current_offset = -measure_current();
    scale.tare();

    return true;
}

//
// Setup
//

void setup() {
    Serial.begin(115200);

    // Safty switch setup
    pinMode(SAFETY_SWITCH_PIN, INPUT);
    attachInterrupt(digitalPinToInterrupt(SAFETY_SWITCH_PIN), system_pause_isr, FALLING);
    
    // RPM measurement setup
    attachInterrupt(digitalPinToInterrupt(RPM_DOUT_PIN), rpm_counting_isr, RISING);

    // Current measurement setup
    pinMode(CURRENT_AOUT_PIN, INPUT);
    current_offset = -measure_current();

    // Thrust measurement setup
    scale.begin(THRUST_DT_PIN, THRUST_SCK_PIN);
    scale.set_scale(THRUST_SCALE);
    scale.tare();

    // Voltage measurement setup
    pinMode(BAT_VOLTAGE_PIN, INPUT);

    // ESC communication setup
    pinMode(ESC_COMMAND_PIN, OUTPUT);
    esc.attach(ESC_COMMAND_PIN, 1100, 1940);
    set_throttle(0.0);

    // LEDs setup
    pinMode(LED_GREEN_PIN, OUTPUT);
    pinMode(LED_YELLOW_PIN, OUTPUT);
    digitalWrite(LED_GREEN_PIN, HIGH);
    digitalWrite(LED_YELLOW_PIN, LOW);
    standby_ts = millis();
}

//
// Loop
//

void loop() {
    if (Serial.available() > 0) {
        StaticJsonDocument<512> command_doc;
        deserializeJson(command_doc, Serial);
        JsonObject command_obj = command_doc.as<JsonObject>();

        if (!command_obj.containsKey("command_type")) return;

        if (command_obj["command_type"] == "sys_init") {
            StaticJsonDocument<128> return_doc;
            bool is_sys_ok = sys_init();

            return_doc["response_type"] = "sys_init";
            return_doc["ok"] = is_sys_ok;
            serializeJson(return_doc, Serial);
            Serial.print("\n");

            digitalWrite(LED_GREEN_PIN, LOW);
            is_gled_on = false;
            /* Pattern start */
            delay(100);
            digitalWrite(LED_YELLOW_PIN, HIGH);
            delay(250);
            digitalWrite(LED_YELLOW_PIN, LOW);
            delay(100);
            digitalWrite(LED_YELLOW_PIN, HIGH);
            delay(250);
            digitalWrite(LED_YELLOW_PIN, LOW);
            delay(100);
            /* Pattern end */
            digitalWrite(LED_GREEN_PIN, HIGH);
            is_gled_on = true;
        }
        else if (command_obj["command_type"] == "measure") {
            StaticJsonDocument<3072> return_doc;
            int steps = command_obj["steps"];
            float throttle_scale = command_obj["throttle_scale"];
            for (int i = 0; i <= steps; i++) {
                if (!system_paused) {
                    measure(i, steps, throttle_scale);

                    digitalWrite(LED_GREEN_PIN, LOW);
                    is_gled_on = false;
                    /* Pattern start */
                    delay(100);
                    digitalWrite(LED_YELLOW_PIN, HIGH);
                    delay(250);
                    digitalWrite(LED_YELLOW_PIN, LOW);
                    delay(100);
                    /* Pattern end */
                    digitalWrite(LED_GREEN_PIN, HIGH);
                    is_gled_on = true;
                }
                else {
                    set_throttle(0.0);

                    delay(100);
                    digitalWrite(LED_GREEN_PIN, LOW);
                    digitalWrite(LED_YELLOW_PIN, HIGH);
                    is_gled_on = false;
                    
                    break;
                }
                set_throttle(0.0);
            }

            return_doc["response_type"] = "measure";
            return_doc["ok"] = !system_paused;
            JsonArray data_array = return_doc.createNestedArray("data");
            for (int i = 0; i <= steps; i++) {
                JsonObject data = data_array.createNestedObject();
                data["throttle"] = measurements[i].throttle;
                data["rpm"] =      measurements[i].rpm;
                data["current"] =  measurements[i].current;
                data["thrust"] =   measurements[i].thrust;
                data["voltage"] =  measurements[i].voltage;
            }
            serializeJson(return_doc, Serial);
            Serial.print("\n");

            if (system_paused) {
                while (true) {}
            }

            digitalWrite(LED_GREEN_PIN, LOW);
            is_gled_on = false;
            /* Pattern start */
            delay(100);
            digitalWrite(LED_YELLOW_PIN, HIGH);
            delay(250);
            digitalWrite(LED_YELLOW_PIN, LOW);
            delay(100);
            digitalWrite(LED_YELLOW_PIN, HIGH);
            delay(250);
            digitalWrite(LED_YELLOW_PIN, LOW);
            delay(100);
            /* Pattern end */
            digitalWrite(LED_GREEN_PIN, HIGH);
            is_gled_on = true;
        }
    }

    if (millis() - standby_ts > 1000) {
        digitalWrite(LED_GREEN_PIN, is_gled_on ? LOW : HIGH);
        is_gled_on = !is_gled_on;
        standby_ts = millis();
    }
}