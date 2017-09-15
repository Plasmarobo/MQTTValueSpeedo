#An MQTT Speedometer and average value tracker.
Consumes MQTT Json payloads.
Required keys are "spend_amount" and "tip_amount" in cents.

##Compiling
Ensure PubSubClient has a message size of at least 1024 bytes or whatever you maximum Json payload size is.

##Tesing
None. Works on ESP8266 (Adafruit Huzzah).
