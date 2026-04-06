#ifndef MNET_BUNDLE_H
#define MNET_BUNDLE_H

#include "mcrypt.h"
#include "mdh.h"
#include "mnet_identity.h"
#include "p2p_security.h"
#include "mnet_arduino.h"
#include "mnet_transport.h"
#include "mnet_protocol.h"
#include "mnet_data.h"

#ifdef MNET_ARDUINO_IMPLEMENTATION
#include "mcrypt.c"
#include "mdh.c"
#include "mnet_identity.cpp"
#include "p2p_security.c"
#include "p2p_security_handshake.c"
#include "p2p_security_port.cpp"
#include "mnet_arduino.cpp"
#include "mnet_transport.cpp"
#include "mnet_protocol.cpp"
#include "mnet_data.cpp"
#endif

#endif
