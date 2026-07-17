#define MTEST_IMPLEMENTATION
#include "mtest.h"

void run_transport_suite(void);
void run_security_suite(void);
void run_network_suite(void);
void run_data_suite(void);
void run_protocol_suite(void);
void run_api_suite(void);
void run_arduino_parity_suite(void);

static int run_suite_by_name(const char *name)
{
    if (name == NULL) {
        return 0;
    }
    if (strcmp(name, "transport") == 0) {
        run_transport_suite();
        return 1;
    }
    if (strcmp(name, "security") == 0) {
        run_security_suite();
        return 1;
    }
    if (strcmp(name, "network") == 0) {
        run_network_suite();
        return 1;
    }
    if (strcmp(name, "data") == 0) {
        run_data_suite();
        return 1;
    }
    if (strcmp(name, "protocol") == 0) {
        run_protocol_suite();
        return 1;
    }
    if (strcmp(name, "api") == 0) {
        run_api_suite();
        return 1;
    }
    if (strcmp(name, "arduino_parity") == 0) {
        run_arduino_parity_suite();
        return 1;
    }
    if (strcmp(name, "all") == 0) {
        run_transport_suite();
        run_security_suite();
        run_network_suite();
        run_data_suite();
        run_protocol_suite();
        run_api_suite();
        run_arduino_parity_suite();
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    int i;

    MTEST_BEGIN(argc, argv);
    if (argc <= 1) {
        run_transport_suite();
        run_security_suite();
        run_network_suite();
        run_data_suite();
        run_protocol_suite();
        run_api_suite();
        run_arduino_parity_suite();
    } else {
        for (i = 1; i < argc; ++i) {
            if (!run_suite_by_name(argv[i])) {
                fprintf(stderr, "Unknown suite: %s\n", argv[i]);
                return 2;
            }
        }
    }
    return MTEST_END();
}
