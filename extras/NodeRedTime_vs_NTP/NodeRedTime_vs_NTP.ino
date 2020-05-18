/*
 *  Comparison of NTP behaviour with NodeRedTime behaviour.
 *  
 *  Test scenario is an ESP8266 which:
 *  
 *  1. Wakes from deep sleep.
 *  2. Fetches time from NTP.
 *  3. Fetches time from NodeRedTime.
 *  4. Goes into deep sleep for 60 seconds.
 *  
 *  The test performs 100 iterations, reports its findings,
 *  then stops.
 *  
 *  Assumes GPIO16 is tied to RST (needed for deep-sleep)
 *
 *  Created 2019-12-06. BSD License.
 */

#if (!ESP8266)
    #error "Sketch only tested with ESP8266 - will need adaptation to work with ESP32"
#endif

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <user_interface.h>
#include <NodeRedTime.h>

// Configure for your situation
const char * WiFi_SSID = "REPLACE ME";
const char * WiFi_PSK  = "REPLACE ME";
const char * TZ_INFO    = "AEST-10AEDT,M10.1.0,M4.1.0/3";
const char * NTP_SERVER = "0.au.pool.ntp.org";
NodeRedTime nodeRedTime("http://MYHOST.MYDOMAIN.com:1880/time/");
const int TestCycles = 100;

/* 
 *  Version is is a simple (ie non-bullet-proof) mechanism to force a reset 
 *  of persistent data stored in RTC memory. If the ESP8266 has been powered
 *  off then RTC memory will generally contain random junk and, generally,
 *  that random junk will not match the version number and that, in turn
 *  will force a re-initialisation. If the ESP8266 has NOT been powered off
 *  then RTC memory will generally contain whatever was left over from any
 *  prior run of this sketch. In that case, changing Version will force a
 *  re-initialisation. If a re-initialisation does not occur when it should
 *  (ie at the start of a new test run) then results are undefined.
 */
const int Version = 1;

// a single metric
struct Metric {
    double sum_x = 0.0;     // ∑x
    double sum_x_x = 0.0;   // ∑x²
    int n = 0;
};

// all metrics
struct PersistentData {
    Metric ntp_times;
    Metric ntp_iterations;
    Metric nodered_times;
    int testCycle = 0;
    int ver = Version;      // best placed last - will also catch size changes
};

// a correctly-initialised copy for resetting to a known state
const PersistentData pristine;

// data that persists across sleep cycles (save & restore)
PersistentData persistentData;


void displayRawMetric(Metric metric, const char * tag) {
    Serial.printf(
        "  %s, ∑x=%f, ∑x²=%f, n=%d\n",
        tag,
        metric.sum_x,
        metric.sum_x_x,
        metric.n
    ); 
}


void displayRawData(const char * tag) {
    Serial.printf(
        "%s, data version=%d:\n",
        tag,
        persistentData.ver
    );
    displayRawMetric(persistentData.ntp_times,"ntp_times");
    displayRawMetric(persistentData.ntp_iterations,"ntp_iterations");
    displayRawMetric(persistentData.nodered_times,"nodered_times");
}


void addObservationToMetric(double observation, Metric * metric) {
    metric->sum_x += observation;
    metric->sum_x_x += observation * observation;
    metric->n++;
}


void savePersistentData() {
    system_rtc_mem_write(64, &persistentData, sizeof(persistentData));
}


void restorePersistentData() {
    system_rtc_mem_read(64, &persistentData, sizeof(persistentData));
}


void displayResultForMetric (Metric metric, const char * tag, const char * units) {

    // calculate the mean
    double xBar = metric.sum_x / metric.n;

    // calculate the standard deviation
    double sx = sqrt((metric.sum_x_x - xBar * xBar * metric.n) / (metric.n - 1));

    Serial.printf(
        "  %s, x̄=%f %s, σx=%f %s, n=%d\n",
        tag,
        xBar,
        units,
        sx,
        units,
        metric.n
    ); 
    
}


void displayTestResults() {
    
    Serial.printf(
        "Results. Test cycles = %d\n",
        persistentData.testCycle
    );
    
    displayResultForMetric(persistentData.ntp_times,"ntp_times","ms");
    displayResultForMetric(persistentData.ntp_iterations,"ntp_iterations","iterations");
    displayResultForMetric(persistentData.nodered_times,"nodered_times","ms");

    Serial.printf(
        "Remember, if n for any metric is not equal to %d then it implies a failed test\n",
        persistentData.testCycle
    );
    
}


void prepareForSleepOrRestart() {
    Serial.println();
    delay(1000);
    Serial.flush();
    Serial.end();
}


void goToSleep(uint64_t time_us) {
    savePersistentData();
    prepareForSleepOrRestart();
    ESP.deepSleep(time_us);
}


void fatalError (const char * message) {
    Serial.printf("\nFatal Error: %s\n",message);
    prepareForSleepOrRestart();
    ESP.restart();
}
   

void connectToWiFiNetwork (
    const char * ssid,
    const char * password
) {

    // set connection mode (as a station)
    WiFi.mode(WIFI_STA);

    Serial.printf("Connecting to WiFi network %s",ssid);
    
    WiFi.begin(ssid,password);

    // start a 30-second timer
    unsigned long timeout = millis() + 30000;

    while (WiFi.status() != WL_CONNECTED) {
        
        // sense timeout expired
        if ((long)(millis() - timeout) >= 0) {
            fatalError("connectToWiFiNetwork - unable to connect");
        }

        delay(50);

    }

    Serial.printf(
        " - connected at %s\n",
        WiFi.localIP().toString().c_str()
    );
    
}


void getNTPtime(int sec) {

    /*
     * Based on getNTPtime() function in:
     *      https://github.com/SensorsIot/NTP-time-for-ESP8266-and-ESP32
     * as at 2019-12-05. Adjusted to use local variables and to count both
     * elapsed time until a solution and the number of iterations required.
     */

    tm timeinfo;
    time_t now;
    unsigned long start = millis();
    unsigned long start_us = micros();
    int iterations = 0;
    
    do {
      iterations++;
      time(&now);
      localtime_r(&now, &timeinfo);
      delay(10);
    } while (((millis() - start) <= (1000 * sec)) && (timeinfo.tm_year < (2016 - 1900)));

    bool success = (timeinfo.tm_year > (2016 - 1900));
    double elapsed = (micros()-start_us) / 1000.0;

    // exclude any failed calls from time values
    if (success) {
        addObservationToMetric(elapsed,&persistentData.ntp_times);
    }

    // count iterations for both successful and failed calls
    addObservationToMetric((double)iterations,&persistentData.ntp_iterations);

    // display local time
    Serial.printf("   getNTPtime: %s", asctime(&timeinfo));

}


void getServerTime() {

    time_t epochTime;
    unsigned long start_us = micros();
    bool success = nodeRedTime.serverTime(&epochTime);
    double elapsed = (micros()-start_us) / 1000.0;

    if (success) {
        addObservationToMetric(elapsed,&persistentData.nodered_times);
    }

    // There is no need to count "iterations" because serverTime() does not iterate.
    
    // display local time
    tm timeinfo;
    if (localtime_r(&epochTime, &timeinfo)) {
        Serial.printf("getServerTime: %s",asctime(&timeinfo));
    }

}


void setup() {

    Serial.begin(74880); while (!Serial); Serial.println();

    Serial.printf("Version %d\n",Version);

    // ESP8266 can be in weird state after IDE code upload
    // This cures the problem (providing GPIO16 is tied to RST)
    if (ESP.getResetInfoPtr()->reason == REASON_EXT_SYS_RST) {
        goToSleep(1E6);
    }

    // retrieve data from RTC memory
    restorePersistentData();

    // simple version check
    if (persistentData.ver != Version) {

        // failed - must reset to known state
        Serial.println("Re-initialising persistent data");
        persistentData = pristine;
        savePersistentData();
        displayRawData("after re-initialisation");
        
    }

    // bump the cycle count
    persistentData.testCycle++;

    Serial.printf("Test number %d of %d\n",persistentData.testCycle,TestCycles);

    // create the test environment
    WiFi.disconnect();
    connectToWiFiNetwork(WiFi_SSID,WiFi_PSK);
    configTime(0, 0, NTP_SERVER);
    setenv("TZ", TZ_INFO, 1);

    // fetch time using Network Time Protocol
    getNTPtime(10);

    // fetch time from Node-Red using HTTP
    getServerTime();

    // have the required number of test cycles been executed?
    if (persistentData.testCycle < TestCycles) {

        // not yet! go to sleep for a minute and continue
        goToSleep(60*1E6);
        
    }

    /*
     * The test is now complete. Time to report findings
     */

    // otherwise, the test is complete and it is time to report
    displayRawData("Raw data at end of test");

    displayTestResults();

    persistentData = pristine;
    savePersistentData();

    Serial.println("RTC Memory set to pristine state");

}


void loop() {

    /*
     * What this code does is to bang out a few lines of dots and then go silent.
     * The purpose is to make sure that the test results actually make it to the
     * serial monitor.
     */

    static int loopCount = 0;

    delay(1000);

    if (loopCount > 250) return;

    Serial.print(".");

    loopCount++;

    if (loopCount % 50 == 0) Serial.println();

}
