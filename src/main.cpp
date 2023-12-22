
#include "drivers/LCD_DISCO_F429ZI.h"
#include "mbed.h"

// Define Regs & Configurations --> Gyroscope's settings
#define CTRL_REG1 0x20
#define CTRL_REG1_CONFIG 0b01'10'1'1'1'1
#define CTRL_REG4 0x23  // Second configure to set the DPS // page 33
#define CTRL_REG4_CONFIG 0b0'0'01'0'00'0

#define CTRL_REG3 0x22  // page 32
#define CTRL_REG3_CONFIG 0b0'0'0'0'1'000

#define OUT_X_L 0x28

#define SPI_FLAG 1
#define DATA_READY_FLAG 2

#define SCALING_FACTOR (17.5f * 0.017453292519943295769236907684886f / 1000.0f)

#define FILTER_COEFFICIENT 0.1f

#define SAMPLE_INTERVAL_MS 500  // 0.5 seconds in milliseconds
#define SAMPLE_COUNT 40         // Number of samples to store

#define X 0
#define Y 1
#define Z 2

#define DEBUG \
  0  // Set to 1 to enable debug messages in serrial monitor and to use teleplot

LCD_DISCO_F429ZI lcd;  // Instantiate LCD object

InterruptIn button(PA_0);  // Blue button
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
void spi_cb(int event) { flags.set(SPI_FLAG); }

// data ready callback function
void data_cb() { flags.set(DATA_READY_FLAG); }

int bufferIndex = 0;

// Function to add data to the buffer
void addDataToBuffer(float gx, float gy, float gz) {
  gyroBuffer[bufferIndex].gx = gx;
  gyroBuffer[bufferIndex].gy = gy;
  gyroBuffer[bufferIndex].gz = gz;
  bufferIndex = (bufferIndex + 1) % SAMPLE_COUNT;
}

// Function to calculate velocity - Method 1
float getVelocity(const uint8_t axis, float height) {
  float avgVelocity = 0.0f;
  if (bufferIndex < 1) {
    return avgVelocity;
  }
  float legLength =
      (height * 0.45f) /
      100;  // Assume leg length is 45% of height and convert to meters
  float prevValue = (axis == X) ? gyroBuffer[bufferIndex - 1].gx
                                : (axis == Y) ? gyroBuffer[bufferIndex - 1].gy
                                              : gyroBuffer[bufferIndex - 1].gz;
  float currentValue = (axis == X) ? gyroBuffer[bufferIndex].gx
                                   : (axis == Y) ? gyroBuffer[bufferIndex].gy
                                                 : gyroBuffer[bufferIndex].gz;
  avgVelocity = ((prevValue + currentValue) / 2) * legLength;
  return avgVelocity;
}

// Function to calculate distance - Method 0
float getDistance(const float velocity) {
  float distance = velocity * SAMPLE_INTERVAL_MS / 1000;
  return distance;
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

// Function to calculate variance for a given axis
float calculateVariance(const uint8_t axis) {
  float sum = 0.0f, sumSquared = 0.0f;
  for (int i = 0; i < SAMPLE_COUNT; ++i) {
    float value = (axis == X)
                      ? gyroBuffer[i].gx
                      : (axis == Y) ? gyroBuffer[i].gy : gyroBuffer[i].gz;
    sum += value;
    sumSquared += value * value;
  }
  float mean = sum / SAMPLE_COUNT;
  return (sumSquared / SAMPLE_COUNT) - (mean * mean);
}

// Function to calculate total distance for a given axis
float calculateTotalDistance(const uint8_t axis, float height) {
  float totalDistance = 0.0f;
  float legLength =
      (height * 0.45f) /
      100;  // Assume leg length is 45% of height and convert to meters
  float totalAvgVelocity = 0.0f;
  for (int i = 1; i < bufferIndex; ++i) {
    float prevValue =
        (axis == X) ? gyroBuffer[i - 1].gx
                    : (axis == Y) ? gyroBuffer[i - 1].gy : gyroBuffer[i - 1].gz;
    float currentValue =
        (axis == X) ? gyroBuffer[i].gx
                    : (axis == Y) ? gyroBuffer[i].gy : gyroBuffer[i].gz;
    // convert to average linear velocity
    float avgVelocity = ((prevValue + currentValue) / 2) * legLength;
    // sum up the average linear velocity
    totalAvgVelocity += avgVelocity;
  }
  // calculate total distance
  // v = d/t --> d = v * t
  // t = SAMPLE_INTERVAL_MS * bufferIndex / 1000
  // d = (total v / bufferIndex) * (SAMPLE_INTERVAL_MS * bufferIndex / 1000)
  // d = (total v * SAMPLE_INTERVAL_MS) / 1000
  totalDistance = (totalAvgVelocity * SAMPLE_INTERVAL_MS) / 1000;
  return std::abs(totalDistance);
}

// Function to display 20s of angular velocity data in the buffer
// We store absolute values of angular velocity in the buffer
void displayBuffer() {
  for (int i = 0; i < SAMPLE_COUNT; ++i) {
    printf("gyroBuffer[%d]: gx = %.2f, gy = %.2f, gz = %.2f\n", i,
           gyroBuffer[i].gx, gyroBuffer[i].gy, gyroBuffer[i].gz);
  }
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
        height += 1;
      } else if (pressDuration >= 500 && pressDuration < 2000) {
        height += 10;
      } else {
        break;  // Exit the loop if press duration is 2000 ms or more
      }
      pressDuration = 0;
      updateDisplay(height);
    }
    ThisThread::sleep_for(10ms);
  }

  // spi initialization
  SPI spi(PF_9, PF_8, PF_7, PC_1, use_gpio_ssel);
  uint8_t write_buf[32], read_buf[32];

  // interrupt initialization
  InterruptIn int2(PA_2, PullDown);
  int2.rise(&data_cb);

  // spi format and frequency
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
  float filtered_gx;
  float filtered_gy;
  float filtered_gz;
  float linear_velocity;
  float distance;
  float time;
  float varX;
  float varY;
  float varZ;
  int i;

  // char buffer[32]; // Buffer for string conversion

  while (1) {
    char lineBuffer[128];  // Increased buffer size for floating-point numbers
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
      filtered_gx =
          FILTER_COEFFICIENT * gx + (1 - FILTER_COEFFICIENT) * filtered_gx;
      filtered_gy =
          FILTER_COEFFICIENT * gy + (1 - FILTER_COEFFICIENT) * filtered_gy;
      filtered_gz =
          FILTER_COEFFICIENT * gz + (1 - FILTER_COEFFICIENT) * filtered_gz;
      // Reset the timer
      sampleTimer.reset();
      if (DEBUG) {
        printf(">gx: %4.2f |g\n", filtered_gx);
        printf(">gy: %4.2f |g\n", filtered_gy);
        printf(">gz: %4.2f |g\n", filtered_gz);
      }

      addDataToBuffer(std::abs(filtered_gx), std::abs(filtered_gy), std::abs(filtered_gz));

      // Calculate variance for each axis
      varX = calculateVariance(X);
      varY = calculateVariance(Y);
      varZ = calculateVariance(Z);

      // Determine the axis with the highest variance
      axis = (varX > varY && varX > varZ) ? X : (varY > varZ) ? Y : Z;

      linear_velocity = getVelocity(axis, height);
      // distance += getDistance(linear_velocity);
      distance = calculateTotalDistance(axis, height);

      if (DEBUG) {
        printf("distance: %f\n", distance);
      }

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
      snprintf(lineBuffer, sizeof(lineBuffer), "velocity: %2.2f m/s",
               linear_velocity);
      lcd.DisplayStringAt(0, LINE(8), (uint8_t *)lineBuffer, CENTER_MODE);

      // Convert distance to string and display
      snprintf(lineBuffer, sizeof(lineBuffer), "distance: %2.2f m", distance);
      lcd.DisplayStringAt(0, LINE(9), (uint8_t *)lineBuffer, CENTER_MODE);

      // Convert distance to string and display
      time = (SAMPLE_INTERVAL_MS * bufferIndex) / 1000.0f;
      snprintf(lineBuffer, sizeof(lineBuffer), "time: %2.2fs", time);
      lcd.DisplayStringAt(0, LINE(10), (uint8_t *)lineBuffer, CENTER_MODE);
      if (time - 0.0f <= 0.0001) {
        if (DEBUG) {
          printf("reset\n");
        }
        // Display buffer to extract values before being wiped
        displayBuffer();
        distance = 0.0f;
        bufferIndex = 0;
        for (i = 0; i < SAMPLE_COUNT; ++i) {
          gyroBuffer[i].gx = 0.0f;
          gyroBuffer[i].gy = 0.0f;
          gyroBuffer[i].gz = 0.0f;
        }
      }
    }
    thread_sleep_for(100);
  }
}