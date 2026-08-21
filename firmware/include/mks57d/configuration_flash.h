#ifndef MKS57D_CONFIGURATION_FLASH_H
#define MKS57D_CONFIGURATION_FLASH_H

#include <stdbool.h>

#include "mks57d/configuration_store.h"

bool configuration_flash_backend_init(
    configuration_store_backend_t* backend);

#endif
