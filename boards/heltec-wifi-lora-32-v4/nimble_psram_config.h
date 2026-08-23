#pragma once

// The framework sdkconfig selects internal-only NimBLE host allocation. This board
// boots its BruceNet AP and phone-facing GATT server together, so that default leaves
// too little internal/DMA heap for either radio to accept a new connection.
//
// This file is force-included by the Heltec V4 environment before NimBLE's
// nimconfig.h. Including sdkconfig first lets us replace only the allocation policy
// while preserving the framework's remaining Bluetooth configuration.
#include <sdkconfig.h>

#if !defined(BOARD_HAS_PSRAM)
#error "The Heltec V4 NimBLE configuration requires PSRAM"
#endif

#undef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL
#undef CONFIG_NIMBLE_MEM_ALLOC_MODE_INTERNAL
#undef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_IRAM_8BIT
#undef CONFIG_NIMBLE_MEM_ALLOC_MODE_IRAM_8BIT

#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1
#define CONFIG_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1
