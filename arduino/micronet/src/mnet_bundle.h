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

#define kNamespace mnet_identity_kNamespace
#define kBlobKey mnet_identity_kBlobKey
#define kMagic mnet_identity_kMagic
#define kVersion mnet_identity_kVersion
#include "mnet_identity.cpp"
#undef kVersion
#undef kMagic
#undef kBlobKey
#undef kNamespace

#define p2p_security_zero_node p2p_security_zero_node_impl_main
#include "p2p_security.c"
#undef p2p_security_zero_node

#define p2p_security_zero_node p2p_security_zero_node_impl_hs
#define p2p_security_zero_session p2p_security_zero_session_impl_hs
#include "p2p_security_handshake.c"
#undef p2p_security_zero_session
#undef p2p_security_zero_node

#define kNamespace p2p_security_port_kNamespace
#define kGroupsBlobKey p2p_security_port_kGroupsBlobKey
#define kMagic p2p_security_port_kMagic
#define kVersion p2p_security_port_kVersion
#include "p2p_security_port.cpp"
#undef kVersion
#undef kMagic
#undef kGroupsBlobKey
#undef kNamespace

#define kGroupsNamespace mnet_arduino_kGroupsNamespace
#define kGroupsBlobKey mnet_arduino_kGroupsBlobKey
#define kMagic mnet_arduino_kMagic
#define kVersion mnet_arduino_kVersion
#include "mnet_arduino.cpp"
#undef kVersion
#undef kMagic
#undef kGroupsBlobKey
#undef kGroupsNamespace

#include "mnet_transport.cpp"
#include "mnet_protocol.cpp"
#include "mnet_data.cpp"
#endif

#endif
