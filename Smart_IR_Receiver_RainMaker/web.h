#pragma once
#include "httpsrv.h"
// The console's REST API and pages (see README "REST API").
void webBegin();
void webTick();      // starts the server once the network (or the setup hotspot) is up
