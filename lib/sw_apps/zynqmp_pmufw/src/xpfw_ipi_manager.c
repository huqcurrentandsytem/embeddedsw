/******************************************************************************
* Copyright (C) 2015 Xilinx, Inc.  All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* XILINX  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
* OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
* Except as contained in this notice, the name of the Xilinx shall not be used
* in advertising or otherwise to promote the sale, use or other dealings in
* this Software without prior written authorization from Xilinx.
******************************************************************************/


#include "xpfw_ipi_manager.h"

/**
 * This file provides a framework for modules to send and receive IPI messages
 * PMU IPI-0 is used for communication initiated by other master.
 * PMU IPI-1 is used for communication initiated by PMU.
 *
 * Currently this framework supports for checking/embedding IPI ID of a module.
 * IPI ID is the MSB 16-bits of the first word in pay load.
 * Depending on the application requirements, more features like CheckSum,
 * Message Sequencing etc can be added.
 */

/* Instance of IPI Driver */
static XIpiPsu Ipi0Inst, Ipi1Inst;
static XIpiPsu *Ipi0InstPtr = &Ipi0Inst;
static XIpiPsu *Ipi1InstPtr = &Ipi1Inst;
u32 IpiMaskList[XPFW_IPI_MASK_COUNT] = {0U};

#ifdef ENABLE_SAFETY
#define XPFW_IPI_W0_TO_W6_SIZE 7U
#endif

s32 XPfw_IpiManagerInit(void)
 {
	s32 Status;
	XIpiPsu_Config *Ipi0CfgPtr, *Ipi1CfgPtr;
	u32 i;

	/* Load Config for PMU IPI-0 */
	Ipi0CfgPtr = XIpiPsu_LookupConfig(XPAR_XIPIPSU_0_DEVICE_ID);
	if (Ipi0CfgPtr == NULL) {
		Status = XST_FAILURE;
		goto Done;
	}

	/* Load Config for PMU IPI-1 */
	Ipi1CfgPtr = XIpiPsu_LookupConfig(XPAR_XIPIPSU_1_DEVICE_ID);
	if (Ipi1CfgPtr == NULL) {
		Status = XST_FAILURE;
		goto Done;
	}

	/* Init Mask Lists */
	for (i = 0U; i < XPFW_IPI_MASK_COUNT; i++) {
		IpiMaskList[i] = Ipi0CfgPtr->TargetList[i].Mask;
	}

	/* Initialize the Instance pointer of IPI-0 channel */
	Status = XIpiPsu_CfgInitialize(Ipi0InstPtr, Ipi0CfgPtr,
			Ipi0CfgPtr->BaseAddress);
	if (XST_SUCCESS != Status) {
		return Status;
	}

	/* Initialize the Instance pointer of IPI-1 channel */
	Status = XIpiPsu_CfgInitialize(Ipi1InstPtr, Ipi1CfgPtr,
			Ipi1CfgPtr->BaseAddress);
	if (XST_SUCCESS != Status) {
		return Status;
	}

	/* Enable IPI-0 and IPI-1 from all Masters */
	for (i = 0U; i < XPFW_IPI_MASK_COUNT; i++) {
		XIpiPsu_InterruptEnable(Ipi0InstPtr,
					Ipi0CfgPtr->TargetList[i].Mask);
		XIpiPsu_InterruptEnable(Ipi1InstPtr,
					Ipi1CfgPtr->TargetList[i].Mask);
	}

Done:
	return Status;
}


s32 XPfw_IpiWriteMessage(const XPfw_Module_t *ModPtr, u32 DestCpuMask, u32 *MsgPtr, u32 MsgLen)
{
	s32 Status;

	if ((ModPtr == NULL) || (MsgPtr == NULL)) {
		Status = XST_FAILURE;
		goto Done;
	}

	MsgPtr[0] = (MsgPtr[0] & 0x0000FFFFU) | ((u32)ModPtr->IpiId << 16U);
#ifdef ENABLE_SAFETY
	/* For CRC, IPI message should have max allowed length i.e.,8 words */
	if (MsgLen != XPFW_IPI_MAX_MSG_LEN) {
		Status = XST_FAILURE;
		goto Done;
	}

	/*
	 * Note : The last word MsgPtr[7] in IPI Msg is reserved for CRC.
	 * This is only for safety applications.
	 */
	MsgPtr[7] = XPfw_CalculateCRC((u32)MsgPtr, XPFW_IPI_W0_TO_W6_SIZE);
#endif
	Status = XIpiPsu_WriteMessage(Ipi1InstPtr, DestCpuMask, MsgPtr, MsgLen,
	XIPIPSU_BUF_TYPE_MSG);

Done:
	return Status;
}


s32 XPfw_IpiWriteResponse(const XPfw_Module_t *ModPtr, u32 DestCpuMask, u32 *MsgPtr, u32 MsgLen)
 {
	s32 Status;

	if ((ModPtr == NULL) || (MsgPtr == NULL)) {
		Status = XST_FAILURE;
		goto Done;
	}

	MsgPtr[0] = (MsgPtr[0] & 0x0000FFFFU) | ((u32)ModPtr->IpiId << 16U);
#ifdef ENABLE_SAFETY
	/* For CRC, IPI message should have max allowed length i.e.,8 words */
	if (MsgLen != XPFW_IPI_MAX_MSG_LEN) {
		Status = XST_FAILURE;
		goto Done;
	}

	/*
	 * Note : The last word MsgPtr[7] in IPI Msg is reserved for CRC.
	 * This is only for safety applications.
	 */
	MsgPtr[7] = XPfw_CalculateCRC((u32)MsgPtr, XPFW_IPI_W0_TO_W6_SIZE);
#endif
	Status = XIpiPsu_WriteMessage(Ipi0InstPtr, DestCpuMask, MsgPtr, MsgLen,
			XIPIPSU_BUF_TYPE_RESP);

Done:
	return Status;
}

s32 XPfw_IpiReadMessage(u32 SrcCpuMask, u32 *MsgPtr, u32 MsgLen)
 {
	s32 Status = XST_FAILURE;

	if (MsgPtr == NULL) {
		Status = XST_FAILURE;
		goto Done;
	}

	/* Read Entire Message to Buffer */
	Status = XIpiPsu_ReadMessage(Ipi0InstPtr, SrcCpuMask, MsgPtr, MsgLen,
			XIPIPSU_BUF_TYPE_MSG);

#ifdef ENABLE_SAFETY
	if (XST_SUCCESS != Status) {
		goto Done;
	}

	/* For CRC, IPI message should have max allowed length i.e.,8 words */
	if (MsgLen != XPFW_IPI_MAX_MSG_LEN) {
		Status = XST_FAILURE;
		goto Done;
	}

	/*
	 * Note : The last word MsgPtr[7] in IPI Msg is reserved for CRC.
	 * Compute the CRC and compare.
	 * This is only for safety applications.
	 */
	if (MsgPtr[7] != XPfw_CalculateCRC((u32)MsgPtr, XPFW_IPI_W0_TO_W6_SIZE)) {
		Status = XST_FAILURE;
	}
#endif

Done:
	return Status;
}

s32 XPfw_IpiReadResponse(const XPfw_Module_t *ModPtr, u32 SrcCpuMask, u32 *MsgPtr, u32 MsgLen)
 {
	s32 Status = XST_FAILURE;
	u32 MsgHeader;

	if ((ModPtr == NULL) || (MsgPtr == NULL)) {
		Status = XST_FAILURE;
		goto Done;
	}
	/* Read the first word */
	Status = XIpiPsu_ReadMessage(Ipi1InstPtr, SrcCpuMask, &MsgHeader, 1U,
			XIPIPSU_BUF_TYPE_RESP);

	if (Status != XST_SUCCESS) {
		Status = XST_FAILURE;
		goto Done;
	}
	/* Check if IPI Id matches the upper 16 bits of first word*/
	if ((MsgHeader >> 16) != ModPtr->IpiId) {
		Status = XST_FAILURE;
		goto Done;
	}
	/* Read Entire Message to Buffer */
	Status = XIpiPsu_ReadMessage(Ipi1InstPtr, SrcCpuMask, MsgPtr, MsgLen,
			XIPIPSU_BUF_TYPE_RESP);

#ifdef ENABLE_SAFETY
	if (XST_SUCCESS != Status) {
		goto Done;
	}

	/* For CRC, IPI message should have max allowed length i.e.,8 words */
	if (MsgLen != XPFW_IPI_MAX_MSG_LEN) {
		Status = XST_FAILURE;
		goto Done;
	}
	/*
	 * Note : The last word MsgPtr[7] in IPI Msg is reserved for CRC.
	 * Compute the CRC and compare.
	 * This is only for safety applications.
	 */
	if (MsgPtr[7] != XPfw_CalculateCRC((u32)MsgPtr, XPFW_IPI_W0_TO_W6_SIZE)) {
		Status = XST_FAILURE;
	}
#endif

Done:
	return Status;
}

inline s32 XPfw_IpiTrigger(u32 DestCpuMask)
{
	return XIpiPsu_TriggerIpi(Ipi1InstPtr, DestCpuMask);
}

inline s32 XPfw_IpiPollForAck(u32 DestCpuMask, u32 TimeOutCount)
{
	return XIpiPsu_PollForAck(Ipi1InstPtr, DestCpuMask, TimeOutCount);
}
