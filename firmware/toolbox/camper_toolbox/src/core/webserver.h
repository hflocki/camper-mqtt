#ifndef CT_WEBSERVER_H
#define CT_WEBSERVER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "../../config.h"

void webserverInit();
void webserverStart();
void webserverLoop();

// Live data of the core and all enabled plugins (web UI, serial "read")
void buildLiveJson(JsonDocument& doc);

#endif // CT_WEBSERVER_H
