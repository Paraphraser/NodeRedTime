//
//  NodeRedTime.cpp
//
//  Created by Phill Kelley on 2019-11-29.
//

#include "NodeRedTime.h"

NodeRedTime::NodeRedTime(
	const char * url,
	const unsigned int recall_s,
	const time_t minEpoch_s
) {

	// remember the URL
	_url = String(url);

    /*
	 *	minEpoch_s is the earliest moment in time which serverTime()
	 *	can treat as a valid seconds value. Converted to milliseconds
	 *	here for consistency in later comparisons.
	 */
    _minEpoch_ms = 1000.0 * minEpoch_s;

	/*
	 *	remember how often syntheticTime() should force a call
	 *	to serverTime() to maintain synchronisation. Converted to
	 *	milliseconds here.
	 */
	_recall_ms = 1000.0 * min(max(recall_s,60U),14400U);

}


bool NodeRedTime::serverTime(time_t * epoch) {

	// required classes
	WiFiClient client;
	HTTPClient http;

	// interpreted response from server (in integer milliseconds)
	double serverTime_ms = 0.0;

	// for estimating millis() when server read its own time
	unsigned long sync_ms = 0;

	// try to obtain time from Node-Red server
	if (http.begin(client, _url)) {

		// uptime now
		sync_ms = millis();

		// send query
		int httpCode = http.GET();

		/*
		 *	mid-point of query round-trip time (floating
		 *	point arithmetic to avoid wrap of unsigned
		 *	integer arguments, guaranteed to yield a
		 *	positive number, with implied truncation
		 *	back to unsigned integer on the assignment)
		 */
		sync_ms = (1.0 * sync_ms + millis()) / 2.0;

		// valid server reply?
		if (httpCode == HTTP_CODE_OK) {

			// yes! try to interpret reply
			serverTime_ms = http.getString().toDouble();

		}

		http.end();

	}

	// valid response received from server?
	if (serverTime_ms >= _minEpoch_ms) {

        // record estimated synchronisation point
		_uptimeLastSync_ms = 1.0 * sync_ms;

        // remember the server's reply
		_epochLastSync_ms = serverTime_ms;

        // copy the server's reply in whole seconds to the caller
        // (implicit truncation to nearest second)
		*epoch = serverTime_ms / 1000.0;

        // good to go
		return true;

	}

	// force future calls to syntheticTime to call serverTime()
	_epochLastSync_ms = 0.0;

	// force sentinel value for the caller
	*epoch = 0;

    // problem
	return false;

}


bool NodeRedTime::syntheticTime(time_t * epoch) {

    // has valid time previously been obtained from NodeRed?
	if (_epochLastSync_ms >= _minEpoch_ms) {

		// yes! the millisecond clock now is...
		double now_ms = 1.0 * millis();

		/*
		 *	Conditions for calling serverTime() again are:
		 *	1. millis() has wrapped; or
		 *	2. _recall_ms has elapsed;
		 *	3. both
		 *	While the timeout calculation is wrap-safe, the
		 *	rationale for forcing serverTime() when millis()
		 *	wraps is because we don't know what else might
		 *	happen under the ESP hood.
		 */
		if (
			(now_ms > _uptimeLastSync_ms) &&
			((long)(now_ms - (_uptimeLastSync_ms + _recall_ms)) < 0)
		) {

			// Safe to estimate epoch time by adding
			// whole seconds elapsed since last Node-Red sync
			// (implicit truncation to nearest second)
			*epoch =
				(now_ms + _epochLastSync_ms - _uptimeLastSync_ms) /
				1000.0;

       		// good to go
			return true;

		}
 
	}

	/*
	 * Arriving here means either:
	 * 1. serverTime() has never been called.
	 * 2. serverTime() was called but did not supply a valid answer.
	 * 3. serverTime() was called AND supplied a valid answer BUT
	 *    either millis() has wrapped or _recall_ms milliseconds
	 *	  have since elapsed.
	 */

    // syntheticTime() can't answer - ask Node-Red
	return serverTime(epoch);

}
