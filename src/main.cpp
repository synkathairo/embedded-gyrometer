
#include "mbed.h"
#include "drivers/LCD_DISCO_F429ZI.h"
// #include <iomanip>
// #include <iostream>

// Define Regs & Configurations --> Gyroscope's settings
#define CTRL_REG1 0x20
#define CTRL_REG1_CONFIG 0b01'10'1'1'1'1
#define CTRL_REG4 0x23 // Second configure to set the DPS // page 33
#define CTRL_REG4_CONFIG 0b0'0'01'0'00'0

#define CTRL_REG3 0x22 // page 32
#define CTRL_REG3_CONFIG 0b0'0'0'0'1'000

#define OUT_X_L 0x28

#define SPI_FLAG 1
#define DATA_READY_FLAG 2

#define SCALING_FACTOR (17.5f * 0.017453292519943295769236907684886f / 1000.0f)

#define FILTER_COEFFICIENT 0.1f // Adjust this value as needed

#define SAMPLE_INTERVAL_MS 500 // 0.5 seconds in milliseconds
#define SAMPLE_COUNT 40        // Number of samples to store

#define RADIUS 1 // assume the radius (size of a person's leg is 1m, atleast mine is)

#define PERCENTILE 75 // given an array of angular vellocity data, find the PERCENTILEth percentile as a threshold
#define X 0
#define Y 1
#define Z 2

#define MAX_CHANGE_TOLERANCE 0.09f

LCD_DISCO_F429ZI lcd;  // Instantiate LCD object

InterruptIn button(PA_0); // Blue button
Timer pressTimer;          // Timer to measure press duration

volatile bool buttonPressed = false;
volatile int pressDuration = 0;
int height = 100;

// Structure to hold gyro data
struct GyroData {
    float gx, gy, gz;
};

// Circular buffer for storing gyro data
GyroData gyroBuffer[SAMPLE_COUNT];


// EventFlags object declaration
EventFlags flags;

// spi callback function
void spi_cb(int event) {
    flags.set(SPI_FLAG);
}


// data ready callback function
void data_cb() {
    flags.set(DATA_READY_FLAG);
}

int bufferIndex = 0;

// Function to add data to the buffer
void addDataToBuffer(float gx, float gy, float gz) {
    gyroBuffer[bufferIndex].gx = gx;
    gyroBuffer[bufferIndex].gy = gy;
    gyroBuffer[bufferIndex].gz = gz;
    bufferIndex = (bufferIndex + 1) % SAMPLE_COUNT;
}

float getVelocity(uint8_t axis){
    if(bufferIndex <= 1){
        return 0.0f;
    }   
    float current;
    float previous;
    if (axis = X){
        current = gyroBuffer[bufferIndex].gx;
        previous = gyroBuffer[bufferIndex - 1].gx;
    } else if (axis = Y){
        current = gyroBuffer[bufferIndex].gy;
        previous = gyroBuffer[bufferIndex - 1].gy;
    } else if (axis = Z){
        current = gyroBuffer[bufferIndex].gz;
        previous = gyroBuffer[bufferIndex - 1].gz;
    }
    float velocity = std::abs(current - previous) * RADIUS;
    return velocity;
  
}

float getDistance(float velocity){
    float distance = velocity * SAMPLE_INTERVAL_MS / 1000;
    return distance;

}

// Function to display a message on the LCD
void lcd_display_message(uint8_t **message, uint8_t numLines)  
{
    lcd.Clear(LCD_COLOR_WHITE);  // Clear the LCD screen
    // Loop through each line of the message
    for (uint8_t i = 0; i < numLines; i++) {
        // Display the line at the center of the LCD screen
        lcd.DisplayStringAt(0, LINE(5 + i), message[i], LEFT_MODE);
    }
}

void getMaxValuesFromBuffer(float &maxX, float &maxY, float &maxZ) {
    maxX = maxY = maxZ = 0.0f;
    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        maxX = std::max(maxX, gyroBuffer[i].gx);
        maxY = std::max(maxY, gyroBuffer[i].gy);
        maxZ = std::max(maxZ, gyroBuffer[i].gz);
    }
}

void onPress() {
    buttonPressed = true;
    pressTimer.reset();
    pressTimer.start();
}

void onRelease() {
    buttonPressed = false;
    pressTimer.stop();
    pressDuration = pressTimer.read_ms();
}

void updateDisplay(int height) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "input height: %d cm", height);
    // lcd.Clear(LCD_COLOR_WHITE);
    lcd.DisplayStringAt(0, LINE(1), (uint8_t *)buffer, CENTER_MODE);
}

int main() {
    lcd.Clear(LCD_COLOR_WHITE);

    button.fall(&onPress);
    button.rise(&onRelease);
    pressTimer.start();
    updateDisplay(height);

    while (true) {
        if (!buttonPressed && pressDuration > 0) {
            if (pressDuration < 500) {
                height += 5;
            } else if (pressDuration >= 500 && pressDuration < 1000) {
                height += 10;
            } else {
                break;  // Exit the loop if press duration is 2000 ms or more
            }
            pressDuration = 0;
            updateDisplay(height);
            
        }
        

        ThisThread::sleep_for(100ms);
    }

    //spi initialization
    SPI spi(PF_9, PF_8, PF_7, PC_1, use_gpio_ssel);
    uint8_t write_buf[32], read_buf[32];

    //interrupt initialization
    InterruptIn int2(PA_2, PullDown);
    int2.rise(&data_cb);
    
    //spi format and frequency
    spi.format(8, 3);
    spi.frequency(1'000'000);

    // Write to control registers --> spi transfer
    write_buf[0] = CTRL_REG1;
    write_buf[1] = CTRL_REG1_CONFIG;
    spi.transfer(write_buf, 2, read_buf, 2, spi_cb);
    flags.wait_all(SPI_FLAG);

    write_buf[0] = CTRL_REG4;
    write_buf[1] = CTRL_REG4_CONFIG;
    spi.transfer(write_buf, 2, read_buf, 2, spi_cb);
    flags.wait_all(SPI_FLAG);

    write_buf[0] = CTRL_REG3;
    write_buf[1] = CTRL_REG3_CONFIG;
    spi.transfer(write_buf, 2, read_buf, 2, spi_cb);
    flags.wait_all(SPI_FLAG);

    write_buf[1] = 0xFF;

    //(polling for\setting) data ready flag
    if (!(flags.get() & DATA_READY_FLAG) && (int2.read() == 1)) {
        flags.set(DATA_READY_FLAG);
    }

    Timer sampleTimer;
    sampleTimer.start();

    int16_t raw_gx;
    int16_t raw_gy;
    int16_t raw_gz;
    uint8_t axis;
    float gx;
    float gy; 
    float gz; 
    float abs_gx;
    float abs_gy;
    float abs_gz;
    float linear_velocity;
    float distance;
    float max_value;
    float prev_max_value;
    float maxX, maxY, maxZ;
    // char buffer[32]; // Buffer for string conversion

    while (1) {
        
        char lineBuffer[128]; // Increased buffer size for floating-point numbers
        flags.wait_all(DATA_READY_FLAG);
        write_buf[0] = OUT_X_L | 0x80 | 0x40;

        spi.transfer(write_buf, 7, read_buf, 7, spi_cb);
        flags.wait_all(SPI_FLAG);

        // Process raw data
        raw_gx = (((uint16_t)read_buf[2]) << 8) | ((uint16_t)read_buf[1]);
        raw_gy = (((uint16_t)read_buf[4]) << 8) | ((uint16_t)read_buf[3]);
        raw_gz = (((uint16_t)read_buf[6]) << 8) | ((uint16_t)read_buf[5]);

        gx = ((float)raw_gx) * SCALING_FACTOR;
        gy = ((float)raw_gy) * SCALING_FACTOR;
        gz = ((float)raw_gz) * SCALING_FACTOR;
        if (sampleTimer.read_ms() >= SAMPLE_INTERVAL_MS) {
            // Reset the timer
            sampleTimer.reset();
            abs_gx = std::abs(gx);
            abs_gy = std::abs(gy);
            abs_gz = std::abs(gz);
            
            addDataToBuffer(abs_gx, abs_gy, abs_gz);
            getMaxValuesFromBuffer(maxX, maxY, maxZ);
            bool shouldSwitchAxis = false;
            if (axis == X && maxY - maxX > MAX_CHANGE_TOLERANCE) {
                shouldSwitchAxis = true;
                axis = Y;
            } else if (axis == Y && maxZ - maxY > MAX_CHANGE_TOLERANCE) {
                shouldSwitchAxis = true;
                axis = Z;
            } else if (axis == Z && maxX - maxZ > MAX_CHANGE_TOLERANCE) {
                shouldSwitchAxis = true;
                axis = X;
            }
            if (shouldSwitchAxis) {
                // Logic to handle axis switching
                // For example, resetting distance and displaying a message
                distance = 0;
                bufferIndex = 0;
                printf("Axis %c, resetting...", (axis == X ? 'X' : (axis == Y ? 'Y' : 'Z')));
                snprintf(lineBuffer, sizeof(lineBuffer), "Axis %c, resetting...", (axis == X ? 'X' : (axis == Y ? 'Y' : 'Z')));
                lcd.Clear(LCD_COLOR_WHITE);
                lcd.DisplayStringAt(0, LINE(4), (uint8_t *)lineBuffer, CENTER_MODE);
                thread_sleep_for(2000);
            }
            linear_velocity = getVelocity(axis);
            // printf("linear velocity: %4.2f|g\n", linear_velocity);
            distance += getDistance(linear_velocity);
            // printf("distance: %4.2f|g\n", distance);

            // Clear the LCD screen before displaying new data
            lcd.Clear(LCD_COLOR_WHITE);

            snprintf(lineBuffer, sizeof(lineBuffer), "height: %d cm", height);
            lcd.DisplayStringAt(0, LINE(4), (uint8_t *)lineBuffer, CENTER_MODE);

            // Convert gx to string and display
            snprintf(lineBuffer, sizeof(lineBuffer), "gx: %2.2f rad/s", gx);
            lcd.DisplayStringAt(0, LINE(5), (uint8_t *)lineBuffer, CENTER_MODE);

            // Convert gy to string and display
            snprintf(lineBuffer, sizeof(lineBuffer), "gy: %2.2f rad/s", gy);
            lcd.DisplayStringAt(0, LINE(6), (uint8_t *)lineBuffer, CENTER_MODE);

            // Convert gz to string and display
            snprintf(lineBuffer, sizeof(lineBuffer), "gz: %2.2f rad/s", gz);
            lcd.DisplayStringAt(0, LINE(7), (uint8_t *)lineBuffer, CENTER_MODE);

            // Convert linear velocity to string and display
            snprintf(lineBuffer, sizeof(lineBuffer), "velocity: %2.2f m/s", linear_velocity);
            lcd.DisplayStringAt(0, LINE(8), (uint8_t *)lineBuffer, CENTER_MODE);

            // Convert distance to string and display
            snprintf(lineBuffer, sizeof(lineBuffer), "distance: %2.2f m", distance);
            lcd.DisplayStringAt(0, LINE(9), (uint8_t *)lineBuffer, CENTER_MODE);

            // Convert distance to string and display
            snprintf(lineBuffer, sizeof(lineBuffer), "time: %2.2fs", 0.5*bufferIndex);
            lcd.DisplayStringAt(0, LINE(10), (uint8_t *)lineBuffer, CENTER_MODE);


        }

        

        thread_sleep_for(100);
    }
}