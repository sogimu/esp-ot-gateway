#pragma once

#include "endpoints/opentherm/opentherm_endpoint.h"
#include "endpoints/webserver/webserver_endpoint.h"
#include "endpoints/sensors/sensors_endpoint.h"
#include "endpoints/sntp/sntp_endpoint.h"
#include "endpoints/wifi/wifi_endpoint.h"

class Endpoints {
public:
    Endpoints();

    void start();
    void stop();

    WifiEndpoint        wifi_;
    OpenthermEndpoint   ot_;
    WebServerEndpoint   web_;
    SensorsEndpoint     sensors_;
    SntpEndpoint        sntp_;
};