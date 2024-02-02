#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>

// DS18B20 Temperature Sensor
#define DS18B20_PATH "/sys/bus/iio/devices/iio:device0/in_voltage0_raw" 
#define A2D_FILE_WATER_QUALITY "/sys/bus/iio/devices/iio:device0/in_voltage1_raw"
#define GPIO_PATH "/sys/class/gpio"
#define A2D_VOLTAGE_REF_V 1.8
#define A2D_MAX_READING 4095

const double TDS_CALIBRATION_FACTOR = 0.5; 
const double TDS_OFFSET = 0.0;  
volatile int exitFlag = 0; // Flag to indicate when the program should exit
volatile int ledFlag = 0;  // Flag to indicate LED state (0 for off, 1 is for on)

pthread_mutex_t ledFlagMutex = PTHREAD_MUTEX_INITIALIZER;

void printError(const char *action) {
    perror(action);
    exit(EXIT_FAILURE);
}

int export_gpio_pin(int gpio_num) {
    FILE *export_file = fopen(GPIO_PATH "/export", "w");
    if (export_file == NULL) {
        printError("Error opening GPIO export file");
    }

    if (fprintf(export_file, "%d", gpio_num) < 0) {
        fclose(export_file);
        printError("Error exporting GPIO pin");
    }

    fclose(export_file);

    // Set permissions for the new GPIO directory
    char gpio_directory_path[128];
    snprintf(gpio_directory_path, sizeof(gpio_directory_path), GPIO_PATH "/gpio%d", gpio_num);

    if (chmod(gpio_directory_path, 0666) < 0) {
        printError("Error setting GPIO directory permissions");
    }

    return 0;
}

int set_gpio_pin_direction(int gpio_num, const char *direction) {
    char direction_path[128];
    snprintf(direction_path, sizeof(direction_path), GPIO_PATH "/gpio%d/direction", gpio_num);

    FILE *direction_file = fopen(direction_path, "w");
    if (direction_file == NULL) {
        printError("Error opening GPIO direction file");
    }

    if (fprintf(direction_file, "%s", direction) < 0) {
        fclose(direction_file);
        printError("Error setting GPIO direction");
    }

    fclose(direction_file);
    return 0;
}

int set_gpio_pin_value(int gpio_num, int value) {
    char value_path[128];
    snprintf(value_path, sizeof(value_path), GPIO_PATH "/gpio%d/value", gpio_num);

    FILE *value_file = fopen(value_path, "w");
    if (value_file == NULL) {
        printError("Error opening GPIO value file");
    }

    if (fprintf(value_file, "%d", value) < 0) {
        fclose(value_file);
        printError("Error setting GPIO value");
    }

    fclose(value_file);
    return 0;
}

int unexport_gpio_pin(int gpio_num) {
    FILE *unexport_file = fopen(GPIO_PATH "/unexport", "w");
    if (unexport_file == NULL) {
        printError("Error opening GPIO unexport file");
    }

    if (fprintf(unexport_file, "%d", gpio_num) < 0) {
        fclose(unexport_file);
        printError("Error unexporting GPIO pin");
    }

    fclose(unexport_file);
    return 0;
}

//Read temperature sensor
int getTemperatureReading() {
    FILE *file;
    char path[100];
    char line[100];
    float temperature;

    // Construct the path to the DS18B20 sensor file
    snprintf(path, sizeof(path), "%s", DS18B20_PATH);

    // Open the DS18B20 sensor file
    file = fopen(path, "r");
    if (file == NULL) {
       
        return -1; // Return an error code
    }

    // Read the content of the DS18B20 sensor file
    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "t=%f",&temperature) == 1) {
            // Temperature is in millidegrees Celsius, so we divide it by 1000 to get degrees Celsius
            fclose(file);
            return temperature / 1000.0;
        }
    }

    // If we reach here, the temperature value wasn't found in the file
    fclose(file);
    return -1; // Return an error code
}

//Read TDS water quality values
int getWaterQualityReading() {
    FILE *file;
    char line[100];
    int waterQualityReading;

    // Open the water quality sensor file
    file = fopen(A2D_FILE_WATER_QUALITY, "r");
    if (file == NULL) {
        perror("Error opening water quality sensor file");
        return -1; // Return an error code
    }

    // Read the content of the water quality sensor file
    if (fgets(line, sizeof(line), file) != NULL) {
        // Convert the value to an integer
        waterQualityReading = atoi(line);

        // Close the file
        fclose(file);

        return waterQualityReading;
    }

    // If we reach here, there was an error reading the file
    fclose(file);
    return -1; // Return an error code
}

double convertToTemperature(int temperatureReading) {
    double result = (A2D_VOLTAGE_REF_V) * ((double)temperatureReading / (double)A2D_MAX_READING);
    return (1000 * result - 500) / 10.0;
}

double convertToPPM(int waterQualityReading) {
    double calibratedTDS = (waterQualityReading * TDS_CALIBRATION_FACTOR) + TDS_OFFSET;
    calibratedTDS = (calibratedTDS < 0.0) ? 0.0 : calibratedTDS;
    return calibratedTDS;
}

//Controlling LED on or off
void controlLED(int state) {
    int gpio_num_1 = 49;
      // Set GPIO pin values based on the LED state
    switch (state) {
        case 0:
               set_gpio_pin_value(gpio_num_1, 0); 
               printf("LED state: %d\n", state);
                        break;
        case 1:       
            set_gpio_pin_value(gpio_num_1, 1);    
            printf("LED state: %d\n", state);    
            sleep(1); // Sleep for 1 second

        default:
            // Invalid state
            printf("Invalid LED state: %d\n", state);
            break;
    }
}

// First thread for temperature sensor reading
void* temperatureThread(void* arg) {
    while (!exitFlag) {
        int temperatureReading = getTemperatureReading();
        double temperature = convertToTemperature(temperatureReading);

        printf("Temperature Sensor Temperature: %.2f °C\n", temperature);

        // Check if the temperature exceeds a threshold
        if (temperature > 45.0) {
            // Set the LED flag to indicate LED should be on
            pthread_mutex_lock(&ledFlagMutex);
            ledFlag = 1;
            pthread_mutex_unlock(&ledFlagMutex);
        } else {
            // Set the LED flag to indicate LED should be off
            pthread_mutex_lock(&ledFlagMutex);
            ledFlag = 0;
            pthread_mutex_unlock(&ledFlagMutex);
        }

        sleep(2);
    }

    pthread_exit(NULL);
}

// Second thread for water quality sensor reading
void* waterQualityThread(void* arg) {
    while (!exitFlag) {
        int waterQualityReading = getWaterQualityReading();
        double waterQualityPPM = convertToPPM(waterQualityReading);

        printf("Water Quality Sensor PPM: %.2f ppm\n", waterQualityPPM);

        // Check if the water quality exceeds a threshold
        if (waterQualityPPM > 700.0) {
            // Set the LED flag to indicate LED should be on
            pthread_mutex_lock(&ledFlagMutex);
            ledFlag = 1;
            pthread_mutex_unlock(&ledFlagMutex);
        } else {
            // Set the LED flag to indicate LED should be off
            pthread_mutex_lock(&ledFlagMutex);
            ledFlag = 0;
            pthread_mutex_unlock(&ledFlagMutex);
        }

        sleep(2);
    }

    pthread_exit(NULL);
}

//Third sensor for blinking the LED
void* ledThread(void* arg) {
    while (!exitFlag) {
        // Check the LED flag
        pthread_mutex_lock(&ledFlagMutex);
        int currentLedFlag = ledFlag;
        pthread_mutex_unlock(&ledFlagMutex);

        // Perform LED-related tasks based on the LED flag
        if (currentLedFlag) {
            // Blink the LED
            controlLED(1); // Assuming 1 means turning on the LED
        } else {
            // Turn off the LED
            controlLED(0); // Assuming 0 means turning off the LED
        }

        sleep(1); // Adjust the sleep duration as needed
    }

    pthread_exit(NULL);
}

int main() {

    signal(SIGINT, handleCtrlC); // When pressed ctrl+c stop the code

    pthread_t tempThreadId, waterQualityThreadId, ledThreadId;

    // Create threads
    pthread_create(&tempThreadId, NULL, temperatureThread, NULL);
    pthread_create(&waterQualityThreadId, NULL, waterQualityThread, NULL);
    pthread_create(&ledThreadId, NULL, ledThread, NULL);

    // Run the program until user press ctrl+c
    while (1) {
        // Sleep for a short duration
        usleep(100000); // Sleep for 100 milliseconds

        // Check if the exit flag is set
        if (exitFlag) {
            break;
        }
    }

    // Set the exit flag to 1 to terminate threads
    exitFlag = 1;

    // Wait for threads to finish
    pthread_join(tempThreadId, NULL);
    pthread_join(waterQualityThreadId, NULL);
    pthread_join(ledThreadId, NULL);

    return 0;
}
