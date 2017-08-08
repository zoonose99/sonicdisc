#include <stdint.h>
#include <Wire.h>
#include "SonicSensor.h"

const uint8_t NUM_OF_SENSORS = 8; // The number of sonic sensors on the disc
const uint8_t I2C_PACKET_SIZE = 9; // The size of each I2C transmission in bytes
const uint8_t SONIC_DISC_I2C_ADDRESS = 0x09; // The address to assume as an I2C slave
// The pin to trigger an interrupt signal on the master MCU
// to indicate that a measurement is ready to be transmitted.
// It is set HIGH when there are data to be fetched and LOW otherwise.
const uint8_t INT_PIN = 0; // Note that this is also the RX pin
// The pin connected to the on-bard LED for debugging
const uint8_t LED_PIN = 1; // Note that this is also the TX pin
// How often the measurements should take place (in milliseconds)
const unsigned long MEASUREMENT_INTERVAL = 10;
// The time (in milliseconds) that the last measurement took place
unsigned long previousMeasurement = 0;
// Sonic Disc's operational states
enum State {
    STANDBY, // MCU and sensors are on but no measurements are being made
    MEASURING // Sonic Disc is conducting measurements using the sensors
};

volatile State currentState = STANDBY;

// Values to be received via I2C from master
enum I2C_RECEIPT_CODE {
    STATE_TO_STANDBY = 0x0A,
    STATE_TO_MEASURING = 0x0B
};

// Error codes to be transmitted via I2c to the master
enum I2C_ERROR_CODE {
    NO_ERROR,
    IN_STANDBY,
    INCOMPLETE_MEASUREMENT
};

// The Sonic Disc pin mapping of sensors
// Sonic Sensors are defined as SonicSensor(trigger pin, echo pin)
SonicSensor sensors[NUM_OF_SENSORS] = {
    SonicSensor(A3, A2),  // Ultrasonic_0 on the Sonic Disc
    SonicSensor(A1, A0),  // Ultrasonic_1
    SonicSensor(13, 12),  // Ultrasonic_2
    SonicSensor(11, 10),  // Ultrasonic_3
    SonicSensor(8, 9),    // Ultrasonic_4
    SonicSensor(7, 6),    // Ultrasonic_5
    SonicSensor(4, 5),    // Ultrasonic_6
    SonicSensor(3, 2)     // Ultrasonic_7
};

/**
 * Hook for pin change interrupt of PCINT0 vector
 * Pins: D8 to D13
 */
ISR (PCINT0_vect) {

}

/**
 * Hook for pin change interrupt of PCINT1 vector
 * Pins: A0 to A5
 */
ISR(PCINT1_vect) {

}

/**
 * Hook for pin change interrupt of PCINT2 vector
 * Pins: D0 to D7
 */
ISR (PCINT2_vect) {

}

/**
 * Method to set up change interrupt for the specified pin
 * @param pin The Arduino-like name of the pin
 */
void setupChangeInterrupt(uint8_t pin) {
    pinMode(pin, INPUT);
    // Enable interrupt for pin
    *digitalPinToPCMSK(pin) |= bit(digitalPinToPCMSKbit(pin));
    // Clear any outstanding interrupt
    PCIFR |= bit(digitalPinToPCICRbit(pin));
    // Enable interrupt for the pin's group
    PCICR |= bit(digitalPinToPCICRbit(pin));
}

/**
 * Callback for incoming I2C requests by transmiting the collected measurements.
 * A packet of I2C_PACKET_SIZE bytes is sent upon each request. The first byte stands
 * for the error code, while the rest are the values estimated using
 * the ultrasonic sensor readings. The order of the readings is clockwise.
 * For example, a typical package looks like this:
 * |error|us_0|us_1|us_2|us_3|us_4|us_5|us_6|us_7|
 *
 * Error code values:   NO_ERROR                | No error
 *                      INCOMPLETE_MEASUREMENT  | Incomplete measurement
 *                      STANDBY                 | In standby state
 *
 * Sensor values:       0       | Error in measurement (e.g. ping timeout)
 *                      1       | TBD
 *                      2-255   | Valid measurement (in cm)
 */
void handleRequests() {
    uint8_t packet[I2C_PACKET_SIZE] = {0};
    // Send packet via I2C
    Wire.write(packet, I2C_PACKET_SIZE);
}

/**
 * Callback for incoming I2C receptions of data from the master as illustrated below.
 *
 * Request              |   Action
 * STATE_TO_STANDBY         Set state to standby (stop measurements)
 * STATE_TO_MEASURING       Start measuring
 *
 * @param numOfBytes The number of bytes expected to be received
 */
void handleReceipts(int numOfBytes) {
    if (Wire.available()) {
        char masterCommand = Wire.read();
        switch (masterCommand) {
            case STATE_TO_STANDBY:
                currentState = STANDBY;
                break;
            case STATE_TO_MEASURING:
                currentState = MEASURING;
            default:
                break;
        }
    }
}

/**
 * Check to see if it is OK to start a new cycle of measurements
 * based on the current time and the measurement interval.
 * @param  currentTime The current time in milliseconds to be compared
 *                     with the last time that a measurement took place.
 * @return             True if is time to conduct a new measurement and
 *                     False otherwise
 */
bool isTimeToMeasure(unsigned long currentTime) {
    bool isGoodTimeToMeasure = false;
    if (currentTime - previousMeasurement >= MEASUREMENT_INTERVAL) {
        isGoodTimeToMeasure = true;
        previousMeasurement = currentTime;
    }
    return isGoodTimeToMeasure;
}

/**
 * Triggers all sensors at once, using port manipulation for less
 * computation cycles. This is done by sending a pulse with a width
 * of 10 microseconds.
 */
void triggerSensors() {
    // Set all ultrasonic trigger pins to HIGH at the same time
    // Port B handles D8 to D13
    PORTB |= B00101001; // Set pins 8, 11, 13 HIGH
    // Port C handles A0 to A5
    PORTC |= B00001010; // Set pins A1, A3 HIGH
    // Port D handles D0 to D7
    PORTD |= B10011000; // Set pins D3, D4, D7 HIGH

    // Keep the signal HIGH for 10 microseconds
    delayMicroseconds(10);

    // Set the trigger pins back to LOW
    PORTB &= B11010110; // Set pins 8, 11, 13 LOW
    PORTC &= B11110101; // Set pins A1, A3 LOW
    PORTD &= B01100111; // Set pins D3, D4, D7 LOW
}

/**
 * Run once on boot or after a reset
 */
void setup() {
    // Set up ultrasonic sensor pins
    for (int i = 0; i < NUM_OF_SENSORS; i++){
        // Set up change interrupts for all the echo pins
        setupChangeInterrupt(sensors[i].getEchoPin());
        // Set up all trigger pins as outputs
        pinMode(sensors[i].getTriggerPin(), OUTPUT);
    }
    // Set up the interrupt signal and led pins
    pinMode(INT_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    // Set up the I2C bus and join it as a slave with address 9
    Wire.begin(SONIC_DISC_I2C_ADDRESS);
    // Set up callback for I2C requests
    Wire.onRequest(handleRequests);
    // Set up callback for I2C receipts
    Wire.onReceive(handleReceipts);
}

/**
 * Run continuously after setup()
 */
void loop() {
    switch(currentState) {
        case STANDBY:
            break;
        case MEASURING:
            if (isTimeToMeasure(millis())) {
                // Disable interrupts while we prepare to calculate
                // the distances to be certain that each set of measurements
                // corresponds to the same point in time.
                noInterrupts(); // Begin critical section
                for (int i = 0; i < NUM_OF_SENSORS; i++) {
                    sensors[i].prepareToCalculate();
                }
                interrupts(); // End critical section

                // Now that we are certain that our measurements are consistent
                // time-wise, calculate the distance.
                for (int i = 0; i < NUM_OF_SENSORS; i++) {
                    // Calculate distance for each sensor.
                    // Will also timeout any pending measurements
                    sensors[i].calculateDistance();
                }
                // Signal that we have a new set of measurements
                digitalWrite(INT_PIN, HIGH);

                // Start a new measurement with critical section for consistency.
                noInterrupts(); // Begin critical section
                // First reset previous the echoes so the interrupts can update
                // them again.
                for (int i = 0; i < NUM_OF_SENSORS; i++) {
                    sensors[i].resetEcho();
                }
                // Trigger all sensors at once
                triggerSensors();
                interrupts(); // End critical section
            }
            break;
        default:
            break; // We should not get here
    }
}
