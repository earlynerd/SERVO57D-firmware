#ifndef TEST_MOCK_N32L40X_H
#define TEST_MOCK_N32L40X_H

/* The real backend has no register accesses. Model only its CMSIS critical
 * section operations; peripheral behavior lives in test_bootstrap_backend.c. */
#include <stdint.h>
#define __NVIC_PRIO_BITS 4u
extern uint32_t mock_basepri;
static inline uint32_t __get_BASEPRI(void) { return mock_basepri; }
static inline void __set_BASEPRI(uint32_t value) { mock_basepri = value; }
static inline void __set_BASEPRI_MAX(uint32_t value)
{
    if ((mock_basepri == 0u) || (value < mock_basepri))
    {
        mock_basepri = value;
    }
}
static inline void __DSB(void) { }
static inline void __ISB(void) { }
static inline void __DMB(void) { }
#endif
