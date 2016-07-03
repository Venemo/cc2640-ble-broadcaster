
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

#include <driverlib/rfc.h>
#include <driverlib/rf_mailbox.h>
#include <driverlib/rf_common_cmd.h>
#include <driverlib/rf_ble_mailbox.h>
#include <driverlib/rf_ble_cmd.h>

#include "rf.h"

// Defined in "Supplement to the Bluetooth Core Specification"
// https://www.bluetooth.com/specifications/adopted-specifications
// and "assigned numbers": https://www.bluetooth.com/specifications/assigned-numbers/Generic-Access-Profile
#define BLE_ADV_TYPE_DEVINFO      0x01
#define BLE_ADV_TYPE_NAME         0x09
#define BLE_ADV_TYPE_TX_POWER_LVL 0x0A
#define BLE_ADV_TYPE_MANUFACTURER 0xFF

// Simple assertion function that blocks execution if the conditions are not met
extern void t_assert(uint32_t val);

//// https://github.com/contiki-os/contiki/blob/master/cpu/cc26xx-cc13xx/rf-core/rf-ble.c#L99
//static uint32_t ble_overrides[] = {
//  0x00364038, /* Synth: Set RTRIM (POTAILRESTRIM) to 6 */
//  0x000784A3, /* Synth: Set FREF = 3.43 MHz (24 MHz / 7) */
//  0xA47E0583, /* Synth: Set loop bandwidth after lock to 80 kHz (K2) */
//  0xEAE00603, /* Synth: Set loop bandwidth after lock to 80 kHz (K3, LSB) */
//  0x00010623, /* Synth: Set loop bandwidth after lock to 80 kHz (K3, MSB) */
//  0x00456088, /* Adjust AGC reference level */
//  0xFFFFFFFF, /* End of override list */
//};

// TI recommended overrides for Bluetooth Low Energy, differential mode internal bias
// If you use a different RF front-end, copy the appropriate overrides from:
// ble_sdk_2_02_00_31/src/icall/app/ble_user_config.c
static uint32_t ble_overrides[] = {
	0x00001007,
	0x00354038,
	0x4001402D,
	0x00608402,
	0x4001405D,
	0x1801F800,
	0x000784A3,
	0xA47E0583,
	0xEAE00603,
	0x00010623,
	0x02010403,
	0x40014035,
	0x177F0408,
	0x38000463,
	0x00456088,
	0x013800C3,
	0x036052AC,
	0x01AD02A3,
	0x01680263,
	0xFFFFFFFF,
};

// Sends a command structure to the radio CPU
static uint32_t rf_send_command(void *command) {
	uint32_t val = RFCDoorbellSendTo((uint32_t)command);

	// Return errors immediately
	if (val > 1) {
		return val;
	}

	// Wait for command to complete
	while (((rfc_CMD_NOP_t*)command)->status <= ACTIVE) { }

	// Return command status
	return ((rfc_CMD_NOP_t*)command)->status;
}

// Turns on the RF code and all necessary power domains
static void rf_core_on() {
	// Disable RF core in case it was enabled
    PRCMPowerDomainOff(PRCM_DOMAIN_RFCORE);
    PRCMLoadSet();
    while (!PRCMLoadGet()) { }

    // Set RF mode
    // IMPORTANT: this register is _very_ poorly documented, but:
    //            - you need to set it while the RF CPU is powered down
    //            - without properly setting it, you'll get 0x82 (unknown command) for any protocol-specific commands
    //            - it is unclear which RFC mode corresponds to which RF protocol
    //            - setting it to PRCM_RFCMODESEL_CURR_MODE1 will make BLE work on the CC2640
    //
    // See:
    // https://e2e.ti.com/support/wireless_connectivity/zigbee_6lowpan_802-15-4_mac/f/158/t/420672
    // https://e2e.ti.com/support/wireless_connectivity/bluetooth_low_energy/f/538/t/491766
    // https://e2e.ti.com/support/wireless_connectivity/proprietary_2-4_ghz/f/964/p/519711/1889768
    // https://e2e.ti.com/support/wireless_connectivity/proprietary_2-4_ghz/f/964/p/520300/1895357
    // https://e2e.ti.com/support/wireless_connectivity/bluetooth_low_energy/f/538/t/519960
    HWREG(PRCM_BASE + PRCM_O_RFCMODESEL) = PRCM_RFCMODESEL_CURR_MODE1;

	// Power on RF core
    PRCMPowerDomainOn(PRCM_DOMAIN_RFCORE);
    while (PRCMPowerDomainStatus(PRCM_DOMAIN_RFCORE) != PRCM_DOMAIN_POWER_ON) { }
    PRCMLoadSet();
    while (!PRCMLoadGet()) { }

    // Enable RF core clock
	PRCMDomainEnable(PRCM_DOMAIN_RFCORE);
    PRCMLoadSet();
    while (!PRCMLoadGet()) { }

    // Enablde RF clocks
    // NOTE: This line was copypasted from SysCtrlPowerEverything
    HWREG(RFC_PWR_NONBUF_BASE + RFC_PWR_O_PWMCLKEN) = 0x7FF;

    // Wait for it...
    PRCMLoadSet();
    while (!PRCMLoadGet()) { }
}

// Sets up the RF core for use with BLE
static void rf_core_setup() {
	__attribute__((aligned(4)))
	rfc_CMD_RADIO_SETUP_t cmd;
	memset(&cmd, 0, sizeof(rfc_CMD_RADIO_SETUP_t));

	cmd.commandNo = CMD_RADIO_SETUP;
	cmd.condition.rule = COND_NEVER;
	cmd.mode = 0; // IMPORTANT: do NOT use RF_MODE_BLE because that has an incorrect value
	cmd.txPower.IB = 0x30;
	cmd.txPower.GC = 0;
	cmd.txPower.boost = (uint8_t) 5;
	cmd.txPower.tempCoeff = (uint8_t) 0x93;
	cmd.pRegOverride = ble_overrides;

	uint32_t result = rf_send_command(&cmd);
	t_assert(result == DONE_OK);
}

// Starts the radio timer
static void rf_core_start_rat() {
	uint32_t result = RFCDoorbellSendTo(CMDR_DIR_CMD(CMD_START_RAT));

	// ContextError means: RAT already running
	t_assert(result == CMDSTA_Done || result == CMDSTA_ContextError);
}

// Boots the RF core
void rf_core_boot() {
	rf_core_on();
	rf_core_setup();
	rf_core_start_rat();
}

// Sends TX PHY test packets
void rf_core_txtest() {
	// Output structure
	__attribute__((aligned(4)))
	rfCoreHal_bleTxTestOutput_t out;
	memset(&out, 0, sizeof(rfCoreHal_bleTxTestOutput_t));

	// Parameters structure
	__attribute__((aligned(4)))
	rfCoreHal_bleTxTestPar_t param;
	memset(&param, 0, sizeof(rfCoreHal_bleTxTestPar_t));

	param.numPackets = 50;
	param.payloadLength = 35;
	param.packetType = 2; // Repeated 0x55

	// Command structure
	__attribute__((aligned(4)))
	rfCoreHal_CMD_BLE_TX_TEST_t cmd;
	memset(&cmd, 0, sizeof(rfCoreHal_CMD_BLE_TX_TEST_t));

	cmd.commandNo = CMD_BLE_TX_TEST;
	cmd.channel = 37;
	cmd.condition.rule = COND_NEVER;
	cmd.pOutput = (uint8_t*)&out;
	cmd.pParams = (uint8_t*)&param;

	// Send command and wait for results
	uint32_t result = rf_send_command(&cmd);
	t_assert(result >= BLE_DONE_OK && result <= BLE_DONE_STOPPED);

}

// Sends a non-connectable BLE advertisement
void rf_core_advertise() {
	// Advertisement output structure
	__attribute__((aligned(4)))
	rfCoreHal_bleAdvOutput_t out;
	memset(&out, 0, sizeof(rfCoreHal_bleAdvOutput_t));

	// Advertisement parameters structure
	__attribute__((aligned(4)))
	rfCoreHal_bleAdvPar_t params;
	memset(&params, 0, sizeof(rfCoreHal_bleAdvPar_t));

	// Payload structure:
	// <length (1 byte)><type (1 byte)><data>
	uint8_t bleAdvPayload[] = {
		// See core supplement v6, 1.3.2, flags: LE discoverable, BR/EDR not supported
		2, BLE_ADV_TYPE_DEVINFO, 0x06,
		// TX power level: 5dBm (this not a setting, it is just for informing the scanner, so that it can calculate the losses)
		2, BLE_ADV_TYPE_TX_POWER_LVL, 5,
		// Device name
		12, BLE_ADV_TYPE_NAME, 'A', 'w', 'e', 's', 'o', 'm', 'e', 'n', 'e', 's', 's'
	};

	// Set advertising parameters
	params.advLen = sizeof(bleAdvPayload);
	params.pAdvData = bleAdvPayload;

	// Create RF command
	__attribute__((aligned(4)))
	rfCoreHal_CMD_BLE_ADV_NC_t cmd;
	memset(&cmd, 0, sizeof(rfCoreHal_CMD_BLE_ADV_NC_t));

	cmd.commandNo = CMD_BLE_ADV_NC;
	cmd.channel = 37;
	cmd.condition.rule = COND_NEVER;
	cmd.pOutput = (uint8_t*)&out;
	cmd.pParams = (uint8_t*)&params;

	// Send command and wait for results
	uint32_t result = rf_send_command(&cmd);

	t_assert(result >= BLE_DONE_OK && result <= BLE_DONE_STOPPED);
}
