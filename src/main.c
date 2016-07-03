
// This file is part of the cc2640-ble-broadcaster app
// Copyright (c) 2016 Timur Kristóf
// Licensed to you under the terms of the MIT license
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <driverlib/prcm.h>
#include <driverlib/sys_ctrl.h>
#include <driverlib/gpio.h>
#include <driverlib/setup.h>

#include "rf.h"

// Simple assertion function that blocks execution if the conditions are not met
void t_assert(uint32_t val) {
	if (0 == val) {
		for (;;) { }
	}
}

// Boots the MCU and turns on the necessary power domains
void mcu_boot() {
	// Trim device
	trimDevice();

	// Power on necessary power domains
    PRCMPowerDomainOn(PRCM_DOMAIN_VIMS | PRCM_DOMAIN_SYSBUS | PRCM_DOMAIN_CPU | PRCM_DOMAIN_SERIAL | PRCM_DOMAIN_PERIPH);
    while (PRCMPowerDomainStatus(PRCM_DOMAIN_VIMS | PRCM_DOMAIN_SYSBUS | PRCM_DOMAIN_CPU | PRCM_DOMAIN_SERIAL | PRCM_DOMAIN_PERIPH) != PRCM_DOMAIN_POWER_ON) { }
    PRCMLoadSet();
    while(!PRCMLoadGet()) { }

	// Enable clock to necessary power domains
	PRCMDomainEnable(PRCM_DOMAIN_VIMS | PRCM_DOMAIN_SYSBUS | PRCM_DOMAIN_CPU | PRCM_DOMAIN_SERIAL | PRCM_DOMAIN_PERIPH);
    PRCMLoadSet();
    while(!PRCMLoadGet()) { }

    // Use HF crystal as clock source
    OSCClockSourceSet(OSC_SRC_CLK_MF | OSC_SRC_CLK_HF, OSC_XOSC_HF);
    for (uint32_t i = OSCClockSourceGet(OSC_SRC_CLK_HF); i != OSC_XOSC_HF; i = OSCClockSourceGet(OSC_SRC_CLK_HF)) {
    	OSCHfSourceSwitch();
    }

    // Use LF crystal as clock source
    OSCClockSourceSet(OSC_SRC_CLK_LF, OSC_XOSC_LF);

    PRCMLoadSet();
    while(!PRCMLoadGet()) { }
}

// Main function
int main(void) {
	// Boot MCU
	mcu_boot();

    // Boot radio
    rf_core_boot();

    for (;;) {
    	/* Uncomment the following section for TX PHY tests */
    	//// Send PHY test TX packets
    	//rf_core_txtest();

    	// Send BLE advertisement
    	rf_core_advertise();

    	// Wait before sending next one
    	for (uint32_t i = 1e6; i--; ) { }
    }
	
	return 0;
}
