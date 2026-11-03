#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include "config.h"

void webserverInit();     // mounts LittleFS, loads the config, registers the routes
void webserverStart();    // after WiFi is up
void webserverLoop();     // deferred work (flash writes, restart) + live updates
void loadConfig();
void saveConfig();

#endif // WEBSERVER_H
