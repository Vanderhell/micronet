#define MTEST_IMPLEMENTATION
#include "mtest.h"

void run_transport_suite(void);
void run_security_suite(void);
void run_network_suite(void);
void run_data_suite(void);
void run_protocol_suite(void);
void run_api_suite(void);

int main(int argc, char **argv)
{
    MTEST_BEGIN(argc, argv);
    run_transport_suite();
    run_security_suite();
    run_network_suite();
    run_data_suite();
    run_protocol_suite();
    run_api_suite();
    return MTEST_END();
}
