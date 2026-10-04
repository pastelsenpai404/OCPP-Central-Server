#pragma once
#include "billing/management/application/backoffice.hpp"
#include <httplib.h>
namespace billing::management {
void install_routes(httplib::Server &server, Backoffice &service);
}
