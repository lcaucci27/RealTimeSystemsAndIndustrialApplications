/******************************************************************************
* Copyright (c) 2015 - 2020 Xilinx, Inc.  All rights reserved.
* SPDX-License-Identifier: MIT
******************************************************************************/


/*****************************************************************************/
/**
*
* @file xfsbl_hooks.c
*
* This is the file which contains FSBL hook functions.
*
* <pre>
* MODIFICATION HISTORY:
*
* Ver   Who  Date        Changes
* ----- ---- -------- -------------------------------------------------------
* 1.00  kc   04/21/14 Initial release
* 2.0   bv   12/05/16 Made compliance to MISRAC 2012 guidelines
*       ssc  03/25/17 Set correct value for SYSMON ANALOG_BUS register
*
* </pre>
*
* @note
*
******************************************************************************/
/***************************** Include Files *********************************/
#include "xfsbl_hw.h"
#include "xfsbl_hooks.h"
#include "psu_init.h"
/************************** Constant Definitions *****************************/

/**************************** Type Definitions *******************************/

/***************** Macros (Inline Functions) Definitions *********************/

/************************** Function Prototypes ******************************/

/************************** Variable Definitions *****************************/
#ifdef XFSBL_BS
u32 XFsbl_HookBeforeBSDownload(void )
{
	u32 Status = XFSBL_SUCCESS;

	/**
	 * Add the code here
	 */


	return Status;
}


u32 XFsbl_HookAfterBSDownload(void )
{
	u32 Status = XFSBL_SUCCESS;

	/**
	 * Add the code here
	 */

	return Status;
}
#endif

u32 XFsbl_HookBeforeHandoff(u32 EarlyHandoff)
{
	u32 Status = XFSBL_SUCCESS;

	/**
	 * Add the code here
	 */

	return Status;
}

/*****************************************************************************/
/**
 * This is a hook function where user can include the functionality to be run
 * before FSBL fallback happens
 *
 * @param none
 *
 * @return error status based on implemented functionality (SUCCESS by default)
 *
  *****************************************************************************/

u32 XFsbl_HookBeforeFallback(void)
{
	u32 Status = XFSBL_SUCCESS;

	/**
	 * Add the code here
	 */

	return Status;
}

/*****************************************************************************/
/**
 * This function facilitates users to define different variants of psu_init()
 * functions based on different configurations in Vivado. The default call to
 * psu_init() can then be swapped with the alternate variant based on the
 * requirement.
 *
 * @param none
 *
 * @return error status based on implemented functionality (SUCCESS by default)
 *
  *****************************************************************************/

/*****************************************************************************
*
* Modifiche per abilitare la Coerenza I/O tra APU e RPU
* File: xfsbl_hooks.c
*
******************************************************************************/

#include "xil_io.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xfsbl_hw.h"

// Usa le definizioni già presenti in xfsbl_hw.h per RPU_RPU_0_CFG e RPU_RPU_1_CFG
// NON ridefinirle!

// Altri indirizzi necessari
#define CCI_SNOOP_CTRL_S3 0xFD6E4000U
#define LPD_SLCR_LPD_APU 0xFF41A040U

// Valori da scrivere
#define RPU_CFG_COHERENT 0x00000002U  // Bit 1: COHERENT
#define CCI_SNOOP_ENABLE 0x00000003U  // Bit 0: enable, Bit 1: support_dvm
#define LPD_APU_BROADCAST 0x00000003U // Bit 0: brdc_inner, Bit 1: brdc_outer

/*****************************************************************************
* Modifiche per abilitare la Coerenza I/O tra APU e RPU
******************************************************************************/

#include "xil_io.h"
#include "xil_printf.h"
#include "xparameters.h"
#include "xfsbl_hw.h"

u32 XFsbl_HookPsuInit(void)
{
	u32 Status;
	// u32 rpu0_cfg;

	// Inizializzazione base sistema
	Status = (u32)psu_init();
	if (Status != XFSBL_SUCCESS) {
		return Status;
	}

	// Conferma configurazione coerenza
	//Xil_Out32(0xFF41A040U, 0x00000003U);

	// CCI-400: enable snoop + DVM support
	//Xil_Out32(0xFD6E4000U, 0x00000003U);

	// RPU_0: set COHERENT bit
	//rpu0_cfg = Xil_In32(RPU_RPU_0_CFG);
	//rpu0_cfg |= 0x00000002U;
	//Xil_Out32(RPU_RPU_0_CFG, rpu0_cfg);

	//xil_printf("Done.\n\r");

	return Status;
}

/*****************************************************************************/
/**
 * This function detects type of boot based on information from
 * PMU_GLOBAL_GLOB_GEN_STORAGE1. If Power Off Suspend is supported FSBL must
 * wait for PMU to detect boot type and provide that information using register.
 * In case of resume from Power Off Suspend PMU will wait for FSBL to confirm
 * detection by writing 1 to PMU_GLOBAL_GLOB_GEN_STORAGE2.
 *
 * @return Boot type, 0 in case of cold boot, 1 for warm boot
 *
 * @note none
 *****************************************************************************/
#ifdef ENABLE_POS
u32 XFsbl_HookGetPosBootType(void)
{
	u32 WarmBoot = 0;
	u32 RegValue = 0;

	do {
		RegValue = XFsbl_In32(PMU_GLOBAL_GLOB_GEN_STORAGE1);
	} while (0U == RegValue);

	/* Clear Gen Storage register so it can be used later in system */
	XFsbl_Out32(PMU_GLOBAL_GLOB_GEN_STORAGE1, 0U);

	WarmBoot = RegValue - 1;

	/* Confirm detection in case of resume from Power Off Suspend */
	if (0 != RegValue) {
		XFsbl_Out32(PMU_GLOBAL_GLOB_GEN_STORAGE2, 1U);
	}

	return WarmBoot;
}
#endif
