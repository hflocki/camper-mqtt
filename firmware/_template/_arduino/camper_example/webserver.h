#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include "config.h"

void webserverInit();      // mount LittleFS, load config, register the routes
void webserverStart();
void webserverLoop();
void saveConfig();
void loadConfig();
void wsSendLiveData();

#endif // WEBSERVER_H
