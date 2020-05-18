/*
 *  Demonstrates use of NodeRedTime library to obtain Unix Epoch
 *  timestamps from Node-Red (as an alternative to NTP).
 *
 *  1. Replace "REPLACE ME" with WiFi credentials.
 *  2. Change "MYHOST.MYDOMAIN" to point to your Node-Red server.
 *  3. Read the discussion prior to TZ_INFO then set it to the string
 *     for your local time-zone.
 *  4. Assumes you have set up a Node-Red flow as documented.
 *
 *  Boots, connects to WiFi, then loops calling serverTime() at
 *  once per minute, otherwise calling syntheticTime() once per
 *  second. Converts and displays both local time and UTC.
 *
 *  Created 2019-12-03. MIT License.
 */

#include <Arduino.h>

#if (ESP8266)
    #include <ESP8266WiFi.h>
#endif

#if (ESP32)
    #include <WiFi.h>
#endif

#include <NodeRedTime.h>


/*
 * WiFi Credentials
 */
const char * WiFi_SSID = "REPLACE ME";
const char * WiFi_PSK  = "REPLACE ME";


/*
 * ESP8266 can resolve mDNS names (eg iot-hub.local) but ESP32 does not
 * do that by default. Best practice is to use a fully-qualified domain
 * name (FQDN) or IP address. You should supply port 1880 (or whatever
 * port Node-Red is listening on) plus the path suffix (eg /time/) that
 * the Node-Red flow is expecting. See API documentation for additional
 * arguments for this constructor.
 */
NodeRedTime nodeRedTime("http://MYHOST.MYDOMAIN.com:1880/time/");

/*
 * Find this file in your ESP8266 installation package:
 * 
 *  [OS-specific]/esp8266/hardware/esp8266/x.x.x/cores/esp8266/TZ.h
 *      
 * In that file you will find strings like this:
 * 
 *  #define TZ_Australia_Sydney PSTR("AEST-10AEDT,M10.1.0,M4.1.0/3")
 *      
 * PSTR is a macro that stores strings in PROGMEM. That's overkill for
 * most IoT devices which only need the definition for the timezone the
 * device is running in. Just copy the relevant string from TZ.h and
 * paste it into TZ_INFO below.
 * 
 * What does the string mean? Using TZ_Australia_Sydney as the example:
 * 
 *      "AEST-10AEDT,M10.1.0,M4.1.0/3"
 *      
 * Implied in the middle comma-separated field of the above is a default
 * "/2" so this has the same meaning:
 * 
 *      "AEST-10AEDT,M10.1.0/2,M4.1.0/3"
 *      
 * Decoded, this means:
 * 
 *      AEST    = Australian Eastern Standard Time (the name† when
 *                summer time is NOT in effect)
 *      -10     = Standard time (AEST) is UTC+10 hours (ie you have to
 *                flip the sign)
 *      AEDT    = Australian Eastern Daylight Time (the name† when
 *                summer time IS in effect)
 *      M10.1.0 = Summer time starts in M (month) 10 (October) on the
 *                .1 (first) .0 (Sunday)
 *      /2      = Summer time starts at 2am. The clock runs:
 *                  01:59:58, 01:59:59, 03:00:00, 03:00:01
 *      M4.1.0  = Summer time ends in M (month) 4 (April) on the .1
 *                (first) .0 (Sunday)
 *      /3      = Summer time ends at 3am. The clock runs:
 *                  02:59:58, 02:59:59, 02:00:00, 02:00:01
 * 
 * † Time-zone names are tricky things and there are no guarantees that
 *   any given name is either consistent or unique. It is best to think
 *   of them as only having meaning to the people actually living in
 *   that time zone.
 *
 * A second example:
 *
 *      TZ_America_Los_Angeles  PSTR("PST8PDT,M3.2.0,M11.1.0")
 * 
 * Augment with the implied "/2" for both the second and third fields:
 * 
 *      "PST8PDT,M3.2.0/2,M11.1.0/2"
 * 
 * Winter time is called PST (Pacific Standard Time). Summer time is
 * called PDT (Pacific Daylight Time). PST is UTC-8. Summer time begins
 * in Month 3 (March) on the .2 (second) .0 (Sunday) at 2am. Summer 
 * time ends in Month 11 (November) on the .1 (first) .0 (Sunday) at
 * 2am. When going onto daylight saving, the clock runs:
 *      01:59:58, 01:59:59, 03:00:00, 03:00:01
 * When going off daylight saving, the clock runs:
 *      01:59:58, 01:59:59, 01:00:00, 01:00:01
 */

// from TZ_Australia_Sydney in TZ.h - replace with yours
const char* TZ_INFO    = "AEST-10AEDT,M10.1.0,M4.1.0/3";


void fatalError (const char * message) {

    Serial.printf("\nFatal Error: %s\n",message);
    Serial.flush();
    Serial.end();
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


void showTime(const char * tag, tm t) {

    /*
        Display time in human-readable form but in a way that
        demonstrates how to pick out the various values. Some
        implementations of "tm" also include:
            t.tm_zone   (char*) timezone abbreviation
            t.tm_gmtoff (long)  UTC offset in seconds
        but that does not appear to be the case for Arduino,
        at least not by default.
    */
    Serial.printf(
        "%s = %04d-%02d-%02d %02d:%02d:%02d, day %d, %s time\n",
        tag,
        t.tm_year + 1900,
        t.tm_mon + 1,       // why? Lost in time (bad pun)
        t.tm_mday,
        t.tm_hour,
        t.tm_min,
        t.tm_sec,
        (t.tm_wday > 0 ? t.tm_wday : 7 ),
        (t.tm_isdst == 1 ? "summer" : "standard")
    );
}


void setup() {

    Serial.begin(74880); while (!Serial); Serial.println();

    #if (ESP8266)
    // ESP8266 can be in weird state after IDE code upload
    // This cures the problem (providing GPIO16 is tied to RST)
    if (ESP.getResetInfoPtr()->reason == REASON_EXT_SYS_RST) {
        ESP.deepSleep(1E6);
    }
    #endif

    // ensure WiFi in known state
    WiFi.disconnect();

	// inform time.h of the rules for local time conversion
	setenv("TZ", TZ_INFO, 1);

}


void loop() {

    static int loopCount = 0;

    // bump loopcount
    loopCount++;

    // indicate progress
    Serial.printf("\nloop=%ld, millis=%lu\n",loopCount,millis());

    // ensure wifi connected and remains connected
    if (WiFi.status() != WL_CONNECTED) {

        connectToWiFiNetwork(WiFi_SSID,WiFi_PSK);

    }

    // fetch the epoch time (seconds since 1970-01-01T00:00:00Z)
    time_t epochTime;
    bool success = nodeRedTime.syntheticTime(&epochTime);

    // display the epoch time where possible
    if (success) {

        // display the epoch seconds
        Serial.printf("  epoch = %lu\n",epochTime);

        tm timeinfo;

        // convert epoch seconds to local time
        if (localtime_r(&epochTime, &timeinfo)) {

            // display a human-readable form
            showTime("  local",timeinfo);

        } else {

            Serial.println("  Could not convert to local time");

        }

        // convert epoch seconds to local time
        if (gmtime_r(&epochTime, &timeinfo)) {

            // display a human-readable form
            showTime("    UTC",timeinfo);

        } else {

            Serial.println("  Could not convert to UTC");

        }

	} else {

		Serial.println("  Could not get time from NodeRed");

	}

	delay(1000);

}
