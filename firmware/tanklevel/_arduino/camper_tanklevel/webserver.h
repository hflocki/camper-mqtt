#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include "config.h"

void webserverInit();
void saveConfig();
void loadConfig();
void webserverStart();
void webserverLoop();
void wsSendSensorData();

#endif // WEBSERVER_H
