// //? Recitation: Intro to DSP
// //? ---------------------------

// //* Agenda
// // [ ] Create simple filters and see the effect they have on the output data


#include "mbed.h"
// #include <iomanip>
// #include <iostream>

#define WINDOW_SIZE 10 // Example window size, adjust as needed

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



int main() {
    // std::setw(2);
    // std::setprecision(5);
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

    // Example 1: Moving Average definitions
    // float window_gx[WINDOW_SIZE] = {0};
    // float window_gy[WINDOW_SIZE] = {0};
    // float window_gz[WINDOW_SIZE] = {0};
    // int window_index = 0;

    // float avg_gx = 0.0f, avg_gy = 0.0f, avg_gz = 0.0f;

    // Example 2: LPF definitions
    float filtered_gx = 0.0f, filtered_gy = 0.0f, filtered_gz = 0.0f;

    // Example 3: HPF definitions
    // use with the example 2 definitions
    // float high_pass_gx = 0.0f, high_pass_gy = 0.0f, high_pass_gz = 0.0f;


    while (1) {
        int16_t raw_gx, raw_gy, raw_gz;
        float gx, gy, gz;

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

        // printf(">x_axis:%d|g\n",raw_gx);
        // printf(">y_axis:%d|g\n",raw_gy);
        // printf(">z_axis:%d|g\n",raw_gz);

        //No Filter
        
        printf("RAW -> \t\tgx: %d \t gy: %d \t gz: %d\t\n", raw_gx, raw_gy, raw_gz);
        // std::cout << "x_axis_raw: " << std::setprecision(3) << gx << std::endl;
        // std::cout << ">y_axis_raw:" << gy << "|g\n";
        // std::cout << ">z_axis_raw:" << gz << "|g\n";
        // printf(">x_axis_raw:%10.5f |g\n", gx);
        // printf(">y_axis_raw:%f |g\n", gy);
        // printf(">z_axis_raw:%4.5d |g\n", gz);

        //Example 1: Moving Average Filter
        //Update window with new data
        
        // window_gx[window_index] = gx;
        // window_gy[window_index] = gy;
        // window_gz[window_index] = gz;

        // Compute the moving average
        
        // float avg_gx = 0.0f, avg_gy = 0.0f, avg_gz = 0.0f;
        // for (int i = 0; i < WINDOW_SIZE; i++) {
        //     avg_gx += window_gx[i];
        //     avg_gy += window_gy[i];
        //     avg_gz += window_gz[i];
        // }
        // avg_gx /= WINDOW_SIZE;
        // avg_gy /= WINDOW_SIZE;
        // avg_gz /= WINDOW_SIZE;

        // Increment and wrap the window index

        // window_index = (window_index + 1) % WINDOW_SIZE;

        // printf(">x_axis_mov:%4.5f|g\n", avg_gx);
        // printf(">y_axis_mov:%4.5f|g\n", avg_gy);
        // printf(">z_axis_mov:%4.5f|g\n", avg_gz);

        // printf("Moving Average -> \tgx: %4.5f \t gy: %4.5f \t gz: %4.5f\n\n", avg_gx, avg_gy, avg_gz);


        // Example 2: Apply Simple low-pass filter
        filtered_gx = FILTER_COEFFICIENT * gx + (1 - FILTER_COEFFICIENT) * filtered_gx;
        filtered_gy = FILTER_COEFFICIENT * gy + (1 - FILTER_COEFFICIENT) * filtered_gy;
        filtered_gz = FILTER_COEFFICIENT * gz + (1 - FILTER_COEFFICIENT) * filtered_gz;

        // printf("RAW -> \t\tgx: %d \t gy: %d \t gz: %d\t\n", raw_gx, raw_gy, raw_gz);

        printf(">x_axis_low:%g|g\n", filtered_gx);
        printf(">y_axis_low:%g|g\n", filtered_gy);
        printf(">z_axis_low:%g|g\n", filtered_gz);

        // Example 3: Apply simple high-pass filter with the lpf (by eliminating low freq elements)
        // to be used with example 2 (together)
        // high_pass_gx = gx - filtered_gx;
        // high_pass_gy = gy - filtered_gy;
        // high_pass_gz = gz - filtered_gz;

        // printf(">x_axis_high:%4.5f|g\n", high_pass_gx);
        // printf(">y_axis_high:%4.5f|g\n", high_pass_gy);
        // printf(">z_axis_high:%4.5f|g\n", high_pass_gz);


        thread_sleep_for(100);
    }
}
