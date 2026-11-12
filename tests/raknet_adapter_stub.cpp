#include "RakNetAdapter.h"
namespace WorldRPG::Net::Vendor {
RakNetAdapter::RakNetAdapter() {}
RakNetAdapter::~RakNetAdapter() {}
bool RakNetAdapter::startup(int, int) { return false; }
bool RakNetAdapter::connect(const char*, unsigned short) { return false; }
bool RakNetAdapter::send(const unsigned char*, int, int, int, int) { return false; }
int RakNetAdapter::receive(unsigned char*, int) { return 0; }
int RakNetAdapter::getPing() const { return 0; }
bool RakNetAdapter::isConnected() const { return false; }
void RakNetAdapter::shutdown() {}
}
