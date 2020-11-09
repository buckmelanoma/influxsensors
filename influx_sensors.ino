/* atmospheric sensor data logger for influxdb
 *
 */

#include <Wire.h>
#include <SparkFunBME280.h> // atmospheric sensor
#include <SparkFunCCS811.h> // co2 & tvoc sensor

#include <Ethernet.h>

BME280 bme_sensor;      // sensor instances
CCS811 ccs_sensor(0x5B);

EthernetClient client; // client instance
byte eth_mac[] = { 0x90, 0xA2, 0xDA, 0x0D, 0xCD, 0xB8 };
IPAddress ip(10, 0, 0, 30);
IPAddress gateway(10, 0, 0, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(10, 0, 0, 1);

// InfluxDB server address
byte influx_server[] = {10, 0, 0, 5};
unsigned const influx_port = 8086;

unsigned const buffer_size = 90; // max size of influxdb post data
const char send_buffer[buffer_size] = {'\0'}; // char array for post data
unsigned short content_length; // Content-Length http header

// start the ethernet shield as client
bool eth_start() {
    Ethernet.begin(eth_mac, ip);
    client.connect(influx_server, influx_port);
    if (client.connected()) {
        Serial.println(F("Connected to InfluxDB server"));
        client.stop();
        return true;
    }
    // print the error number and return false
    Serial.println(F("Error, unable to connect to InfluxDB server"));
    return false;
}

// send data to influxdb
void eth_send_data(char* data, int dataSize) {
    client.connect(influx_server, influx_port);  // connect to InfluxDB server

    if (!client.connected()) { // check connection state
        Serial.println(F("Error, unable to connect to InfluxDB server"));
        return;
    }

    // send HTTP header and buffer
    client.println(F("POST /write?db=ard_tmp HTTP/1.1"));
    client.println(F("Host: 10.0.0.5:8086"));
    client.println(F("User-Agent: Arduino/1.0"));
    client.println(F("Content-Type: application/x-www-form-urlencoded"));
    client.print(F("Content-Length: "));
    client.println(dataSize);
    client.println();  // doesn't POST properly without
    client.println(data);

    delay(100); // wait for server to process data

    client.stop();
}

void setup() {
    Serial.begin(115200); // serial interface for debugging

    Wire.begin();
    Wire.setClock(100000);

    bme_sensor.beginI2C();
    bme_sensor.setMode(MODE_NORMAL);

    CCS811Core::status returnCode = ccs_sensor.begin();
    if (returnCode != CCS811Core::SENSOR_SUCCESS) {
        Serial.println(F("CCS811 sensor failed to init"));
        while (1); // hang if there was a problem.
    }
    delay(3500); // allow sensors to start

    eth_start();
}

void loop() {
    float temperature_f = bme_sensor.readTempF(); // get temp f for influx
    float temperature_c = bme_sensor.readTempC(); // get temp c for ccs calibration
    unsigned char temperature_s[6]; // hopefully never below zero indoors
    dtostrf(temperature_f, 2, 2, temperature_s);  // convert float to string

    unsigned short humidity = bme_sensor.readFloatHumidity(); // %RH
    unsigned long pressure = bme_sensor.readFloatPressure(); // Pa
    unsigned short dew = bme_sensor.dewPointF();

    unsigned short carbon_dioxide;
    unsigned short tvoc;

    if (ccs_sensor.dataAvailable()) {
        ccs_sensor.readAlgorithmResults(); // calling this function updates the global tVOC and eCO2 variables
        ccs_sensor.setEnvironmentalData(humidity, temperature_c); // send calibration data from bme to ccs

        carbon_dioxide = ccs_sensor.getCO2(); // ppm
        tvoc = ccs_sensor.getTVOC(); // ppb
    }

    content_length = sprintf(send_buffer, "indoor_data "); // returns # of chars 'printed'
    content_length += sprintf(&send_buffer[content_length], "temp=%s,", temperature_s); // 'print' next buffer chunk
    content_length += sprintf(&send_buffer[content_length], "humidity=%d,", humidity);
    content_length += sprintf(&send_buffer[content_length], "pressure=%6ld,", pressure);
    content_length += sprintf(&send_buffer[content_length], "dew=%d,", dew);
    content_length += sprintf(&send_buffer[content_length], "tVOC=%d,", tvoc);
    content_length += sprintf(&send_buffer[content_length], "carbon_dioxide=%d", carbon_dioxide);

    Serial.println(send_buffer); // show data on serial

    eth_send_data(send_buffer, content_length);  // send to InfluxDB

    memset(send_buffer, '\0', buffer_size); // null buffer for the next loop
}
