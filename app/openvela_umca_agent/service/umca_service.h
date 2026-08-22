#ifndef UMCA_SERVICE_H
#define UMCA_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

int umca_service_start(void);
int umca_service_stop(void);
bool umca_service_is_running(void);

#endif
