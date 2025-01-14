/** @file

  Copyright (c) 2019, Intel Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiPei.h>
#include <SaPolicyCommon.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/HobLib.h>
#include <Library/PcdLib.h>
#include <Library/PchCycleDecodingLib.h>
#include <Library/PciLib.h>
#include <Library/PcdLib.h>
#include <Library/BaseMemoryLib.h>

#include <Library/PeiSaPolicyLib.h>
#include <Library/BoardInitLib.h>
#include <PchAccess.h>
#include <Library/GpioNativeLib.h>
#include <Library/GpioLib.h>
#include <GpioPinsSklLp.h>
#include <GpioPinsSklH.h>
#include <Library/GpioExpanderLib.h>
#include <SioRegs.h>
#include <Library/PchPcrLib.h>

#include "PeiWhiskeylakeURvpInitLib.h"
#include <ConfigBlock.h>
#include <ConfigBlock/MemoryConfig.h>
#include <Library/PeiServicesLib.h>
#include <Library/PchPcrLib.h>
#include <Library/PchInfoLib.h>
#include <Register/PchRegsPcr.h>
#include <Library/PchResetLib.h>
#include <Register/PchRegsLpc.h>
#include <Library/StallPpiLib.h>
#include <Library/PeiPolicyInitLib.h>
#include <Ppi/Reset.h>
#include <PlatformBoardConfig.h>
#include <GpioPinsCnlLp.h>
#include <Library/PmcLib.h>
#include <Library/PciSegmentLib.h>
#include <PeiPlatformHookLib.h>
#include <FirwmareConfigurations.h>
#include <Guid/TcoWdtHob.h>
#include <Library/OcWdtLib.h>

///
/// Reset Generator I/O Port
///
#define RESET_GENERATOR_PORT           0xCF9

typedef struct {
  EFI_PHYSICAL_ADDRESS    BaseAddress;
  UINT64                  Length;
} MEMORY_MAP;

//
// Reference RCOMP resistors on motherboard - for WHL RVP1
//
GLOBAL_REMOVE_IF_UNREFERENCED const UINT16 RcompResistorSklRvp1[SA_MRC_MAX_RCOMP] = { 200, 81, 162 };

//
// RCOMP target values for RdOdt, WrDS, WrDSCmd, WrDSCtl, WrDSClk - for WHL RVP1
//
GLOBAL_REMOVE_IF_UNREFERENCED const UINT16 RcompTargetSklRvp1[SA_MRC_MAX_RCOMP_TARGETS] = { 100, 40, 40, 23, 40 };

GLOBAL_REMOVE_IF_UNREFERENCED MEMORY_MAP MmioMap[] = {
  { FixedPcdGet64(PcdApicLocalAddress), FixedPcdGet32(PcdApicLocalMmioSize) },
  { FixedPcdGet64(PcdMchBaseAddress), FixedPcdGet32(PcdMchMmioSize) },
  { FixedPcdGet64(PcdDmiBaseAddress), FixedPcdGet32(PcdDmiMmioSize) },
  { FixedPcdGet64(PcdEpBaseAddress), FixedPcdGet32(PcdEpMmioSize) },
  { FixedPcdGet64(PcdGdxcBaseAddress), FixedPcdGet32(PcdGdxcMmioSize) }
};

EFI_STATUS
MrcConfigInit(
  IN UINT16 BoardId
);

EFI_STATUS
SaGpioConfigInit(
  IN UINT16 BoardId
);

EFI_STATUS
  SaMiscConfigInit(
IN UINT16         BoardId
);

EFI_STATUS
  RootPortClkInfoInit(
IN UINT16 BoardId
);

EFI_STATUS
  UsbConfigInit(
IN UINT16 BoardId
);

EFI_STATUS
GpioGroupTierInit(
  IN UINT16 BoardId
);

EFI_STATUS
GpioTablePreMemInit(
  IN UINT16 BoardId
);

EFI_STATUS
PchPmConfigInit(
  IN UINT16 BoardId
);

EFI_STATUS
SaDisplayConfigInit(
  IN UINT16 BoardId
);

EFI_STATUS
BoardFunctionInitPreMem(
  IN UINT16 BoardId
);

EFI_STATUS
EFIAPI
PlatformInitPreMemCallBack(
  IN CONST EFI_PEI_SERVICES      **PeiServices,
  IN EFI_PEI_NOTIFY_DESCRIPTOR   *NotifyDescriptor,
  IN VOID                        *Ppi
);

EFI_STATUS
EFIAPI
MemoryDiscoveredPpiNotify(
  IN CONST EFI_PEI_SERVICES      **PeiServices,
  IN EFI_PEI_NOTIFY_DESCRIPTOR   *NotifyDescriptor,
  IN VOID                        *Ppi
);

EFI_STATUS
EFIAPI
PchReset(
  IN CONST EFI_PEI_SERVICES    **PeiServices
);

static EFI_PEI_RESET_PPI mResetPpi = {
  PchReset
};

static EFI_PEI_PPI_DESCRIPTOR mPreMemPpiList[] = {
  {
    (EFI_PEI_PPI_DESCRIPTOR_PPI | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
    &gEfiPeiResetPpiGuid,
    &mResetPpi
  }
};

static EFI_PEI_NOTIFY_DESCRIPTOR mPreMemNotifyList = {
  (EFI_PEI_PPI_DESCRIPTOR_NOTIFY_CALLBACK | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
  &gEfiPeiReadOnlyVariable2PpiGuid,
  (EFI_PEIM_NOTIFY_ENTRY_POINT)PlatformInitPreMemCallBack
};

static EFI_PEI_NOTIFY_DESCRIPTOR mMemDiscoveredNotifyList = {
  (EFI_PEI_PPI_DESCRIPTOR_NOTIFY_CALLBACK | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
  &gEfiPeiMemoryDiscoveredPpiGuid,
  (EFI_PEIM_NOTIFY_ENTRY_POINT)MemoryDiscoveredPpiNotify
};

/**
Board misc init function for PEI pre-memory phase.

@param[in]  BoardId   An unsigned integer represent the board id.

@retval EFI_SUCCESS   The function completed successfully.
**/
EFI_STATUS
BoardMiscInitPreMem(
  IN UINT16 BoardId
)
{
  PCD64_BLOB PcdData;

  //
  // RecoveryMode GPIO
  //
  PcdData.Blob = 0;
  PcdData.BoardGpioConfig.Type = BoardGpioTypeNotSupported;

  switch (BoardId) {
    case BoardIdWhiskeyLakeRvp:
      PcdData.BoardGpioConfig.Type = BoardGpioTypePch;
      PcdData.BoardGpioConfig.u.Pin = GPIO_CNL_LP_GPP_F10;
    break;

    default:
      break;
  }

  //
  // Configure WWAN Full Card Power Off and reset pins
  //
  switch (BoardId) {
    case BoardIdWhiskeyLakeRvp:
      //
      // According to board default settings, GPP_D16 is used to enable/disable modem
      // power. An alternative way to contol modem power is to toggle FCP_OFF via GPP_D13
      // but board rework is required.
      //
      PcdSet32S(PcdWwanFullCardPowerOffGpio, GPIO_CNL_LP_GPP_D16);
      PcdSet32S(PcdWwanBbrstGpio, GPIO_CNL_LP_GPP_F1);
      PcdSet32S(PcdWwanPerstGpio, GPIO_CNL_LP_GPP_E15);
      PcdSet8S(PcdWwanPerstGpioPolarity, 1);
      break;

    default:
      break;
  }

  PcdSet64S(PcdRecoveryModeGpio, PcdData.Blob);

  //
  // Pc8374SioKbc Present
  //
  PcdSetBoolS(PcdPc8374SioKbcPresent, FALSE);

  return EFI_SUCCESS;
}

//@todo it should be moved to Si Pkg.
/**
Early Platform PCH initialization
**/
VOID
EarlyPlatformPchInit(
  VOID
)
{
  UINT8        Data8;
  UINT8        TcoRebootHappened;
  TCO_WDT_HOB  *TcoWdtHobPtr;
  EFI_STATUS   Status;

  ///
  /// Read the Second TO status bit
  ///
  Data8 = IoRead8(PcdGet16(PcdTcoBaseAddress) + R_TCO_IO_TCO2_STS);
  if ((Data8 & B_TCO_IO_TCO2_STS_SECOND_TO) == B_TCO_IO_TCO2_STS_SECOND_TO) {
    TcoRebootHappened = 1;
    DEBUG((DEBUG_INFO, "PlatformInitPreMem - TCO Second TO status bit is set. This might be a TCO reboot\n"));
  }
  else {
    TcoRebootHappened = 0;
  }

  ///
  /// Create HOB
  ///
  Status = PeiServicesCreateHob(EFI_HOB_TYPE_GUID_EXTENSION, sizeof(TCO_WDT_HOB), (VOID **)&TcoWdtHobPtr);
  if (!EFI_ERROR(Status)) {
    TcoWdtHobPtr->Header.Name = gTcoWdtHobGuid;
    TcoWdtHobPtr->TcoRebootHappened = TcoRebootHappened;
  }

  ///
  /// Clear the Second TO status bit
  ///
  IoWrite8(PcdGet16(PcdTcoBaseAddress) + R_TCO_IO_TCO2_STS, B_TCO_IO_TCO2_STS_SECOND_TO);
}

/**
Board init function for PEI pre-memory phase.

@param  Content  pointer to the buffer contain init information for board init.

@retval EFI_SUCCESS             The function completed successfully.
@retval EFI_INVALID_PARAMETER   The parameter is NULL.
**/
EFI_STATUS
BoardConfigInitPreMem(
  VOID
)
{
  EFI_STATUS Status;
  UINT16 BoardId;

  BoardId = BoardIdWhiskeyLakeRvp;

  Status = MrcConfigInit(BoardId);
  Status = SaGpioConfigInit(BoardId);
  Status = SaMiscConfigInit(BoardId);
  Status = RootPortClkInfoInit(BoardId);
  Status = UsbConfigInit(BoardId);
  Status = GpioGroupTierInit(BoardId);
  Status = GpioTablePreMemInit(BoardId);
  Status = PchPmConfigInit(BoardId);
  Status = BoardMiscInitPreMem(BoardId);
  Status = SaDisplayConfigInit(BoardId);
  Status = BoardFunctionInitPreMem(BoardId);

  return EFI_SUCCESS;
}

/**
This function handles PlatformInit task after PeiReadOnlyVariable2 PPI produced

@param[in]  PeiServices   Pointer to PEI Services Table.
@param[in]  NotifyDesc    Pointer to the descriptor for the Notification event that
                          caused this function to execute.
@param[in]  Ppi           Pointer to the PPI data associated with this function.

@retval     EFI_SUCCESS  The function completes successfully
@retval     others
**/
EFI_STATUS
EFIAPI
PlatformInitPreMemCallBack(
  IN CONST EFI_PEI_SERVICES     **PeiServices,
  IN EFI_PEI_NOTIFY_DESCRIPTOR  *NotifyDescriptor,
  IN VOID                       *Ppi
)
{
  EFI_STATUS                        Status;
  UINT16                            ABase;
  UINT8                             FwConfig;
  UINT8                             SynchDelay;

  //
  // Init Board Config Pcd.
  //
  BoardConfigInitPreMem();

  DEBUG((DEBUG_ERROR, "Fail to get System Configuration and set the configuration to production mode!\n"));
  FwConfig = FwConfigProduction;
  SynchDelay = 0;
  PcdSetBoolS(PcdPcieWwanEnable, FALSE);
  PcdSetBoolS(PcdWwanResetWorkaround, FALSE);

  //
  // Early Board Configuration before memory is ready.
  //
  Status = BoardInitEarlyPreMem();
  ASSERT_EFI_ERROR(Status);

  ///
  /// If there was unexpected reset but no WDT expiration and no resume from S3/S4,
  /// clear unexpected reset status and enforce expiration. This is to inform Firmware
  /// which has no access to unexpected reset status bit, that something went wrong.
  ///
  OcWdtResetCheck();

  Status = OcWdtInit();
  ASSERT_EFI_ERROR(Status);

  //
  // Initialize Intel PEI Platform Policy
  //
  PeiPolicyInitPreMem(FwConfig);

  ///
  /// Configure GPIO and SIO
  ///
  Status = BoardInitPreMem();
  ASSERT_EFI_ERROR(Status);

  ABase = PmcGetAcpiBase();

  ///
  /// Clear all pending SMI. On S3 clear power button enable so it will not generate an SMI.
  ///
  IoWrite16(ABase + R_ACPI_IO_PM1_EN, 0);
  IoWrite32(ABase + R_ACPI_IO_GPE0_EN_127_96, 0);

  ///
  /// Install Pre Memory PPIs
  ///
  Status = PeiServicesInstallPpi(&mPreMemPpiList[0]);
  ASSERT_EFI_ERROR(Status);

  return Status;
}

/**
Provide hard reset PPI service.
To generate full hard reset, write 0x0E to PCH RESET_GENERATOR_PORT (0xCF9).

@param[in]  PeiServices       General purpose services available to every PEIM.

@retval     Not return        System reset occured.
@retval     EFI_DEVICE_ERROR  Device error, could not reset the system.
**/
EFI_STATUS
EFIAPI
PchReset(
  IN CONST EFI_PEI_SERVICES    **PeiServices
)
{
  DEBUG((DEBUG_INFO, "Perform Cold Reset\n"));
  IoWrite8(RESET_GENERATOR_PORT, 0x0E);

  CpuDeadLoop();

  ///
  /// System reset occured, should never reach at this line.
  ///
  ASSERT_EFI_ERROR(EFI_DEVICE_ERROR);

  return EFI_DEVICE_ERROR;
}

/**
Install Firmware Volume Hob's once there is main memory

@param[in]  PeiServices       General purpose services available to every PEIM.
@param[in]  NotifyDescriptor  Notify that this module published.
@param[in]  Ppi               PPI that was installed.

@retval     EFI_SUCCESS       The function completed successfully.
**/
EFI_STATUS
EFIAPI
MemoryDiscoveredPpiNotify(
  IN CONST EFI_PEI_SERVICES     **PeiServices,
  IN EFI_PEI_NOTIFY_DESCRIPTOR  *NotifyDescriptor,
  IN VOID                       *Ppi
)
{
  EFI_STATUS                    Status;
  EFI_BOOT_MODE                 BootMode;
  UINTN                         Index;
  UINT8                         PhysicalAddressBits;
  UINT32                        RegEax;
  MEMORY_MAP                    PcieMmioMap;

  Index = 0;

  Status = PeiServicesGetBootMode(&BootMode);
  ASSERT_EFI_ERROR(Status);

  AsmCpuid(0x80000000, &RegEax, NULL, NULL, NULL);
  if (RegEax >= 0x80000008) {
    AsmCpuid(0x80000008, &RegEax, NULL, NULL, NULL);
    PhysicalAddressBits = (UINT8)RegEax;
  }
  else {
    PhysicalAddressBits = 36;
  }

  ///
  /// Create a CPU hand-off information
  ///
  BuildCpuHob(PhysicalAddressBits, 16);

  ///
  /// Build Memory Mapped IO Resource which is used to build E820 Table in LegacyBios.
  ///
  PcieMmioMap.BaseAddress = FixedPcdGet64(PcdPciExpressBaseAddress);
  PcieMmioMap.Length = PcdGet32(PcdPciExpressRegionLength);

  BuildResourceDescriptorHob(
    EFI_RESOURCE_MEMORY_MAPPED_IO,
    (EFI_RESOURCE_ATTRIBUTE_PRESENT |
     EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
     EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE),
    PcieMmioMap.BaseAddress,
    PcieMmioMap.Length
  );
  BuildMemoryAllocationHob(
    PcieMmioMap.BaseAddress,
    PcieMmioMap.Length,
    EfiMemoryMappedIO
  );
  for (Index = 0; Index < sizeof(MmioMap) / (sizeof(MEMORY_MAP)); Index++) {
    BuildResourceDescriptorHob(
      EFI_RESOURCE_MEMORY_MAPPED_IO,
      (EFI_RESOURCE_ATTRIBUTE_PRESENT |
       EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
       EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE),
      MmioMap[Index].BaseAddress,
      MmioMap[Index].Length
    );
    BuildMemoryAllocationHob(
      MmioMap[Index].BaseAddress,
      MmioMap[Index].Length,
      EfiMemoryMappedIO
    );
  }

  //
  // Report resource HOB for flash FV
  //
  BuildResourceDescriptorHob(
    EFI_RESOURCE_MEMORY_MAPPED_IO,
    (EFI_RESOURCE_ATTRIBUTE_PRESENT |
     EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
     EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE),
    (UINTN)FixedPcdGet32(PcdFlashAreaBaseAddress),
    (UINTN)FixedPcdGet32(PcdFlashAreaSize)
  );
  BuildMemoryAllocationHob(
    (UINTN)FixedPcdGet32(PcdFlashAreaBaseAddress),
    (UINTN)FixedPcdGet32(PcdFlashAreaSize),
    EfiMemoryMappedIO
  );

  BuildFvHob(
    (UINTN)FixedPcdGet32(PcdFlashAreaBaseAddress),
    (UINTN)FixedPcdGet32(PcdFlashAreaSize)
  );

  return Status;
}


/**
  Board configuration init function for PEI pre-memory phase.

  @retval EFI_SUCCESS             The function completed successfully.
  @retval EFI_INVALID_PARAMETER   The parameter is NULL.
**/
EFI_STATUS
EFIAPI
WhiskeylakeURvpInitPreMem (
  VOID
  )
{
  EFI_STATUS Status;

  ///
  /// Install Stall PPI
  ///
  Status = InstallStallPpi();
  ASSERT_EFI_ERROR(Status);

  ///@todo it should be moved to Si Pkg.
  ///
  /// Do Early PCH init
  ///
  EarlyPlatformPchInit();

  //
  // Install PCH RESET PPI and EFI RESET2 PeiService
  //
  Status = PchInitializeReset();
  ASSERT_EFI_ERROR(Status);

  ///
  /// Performing PlatformInitPreMemCallBack after PeiReadOnlyVariable2 PPI produced
  ///
  Status = PeiServicesNotifyPpi(&mPreMemNotifyList);

  ///
  /// After code reorangized, memorycallback will run because the PPI is already
  /// installed when code run to here, it is supposed that the InstallEfiMemory is
  /// done before.
  ///
  Status = PeiServicesNotifyPpi(&mMemDiscoveredNotifyList);

  return EFI_SUCCESS;
}

/**
  Configure GPIO and SIO before memory ready

  @retval  EFI_SUCCESS   Operation success.
**/
EFI_STATUS
EFIAPI
WhiskeylakeURvpBoardInitBeforeMemoryInit (
  VOID
  )
{
  WhiskeylakeURvpInitPreMem ();

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
WhiskeylakeURvpBoardDebugInit (
  VOID
  )
{
  UINT64                            LpcBaseAddress;

  ///
  /// LPC I/O Configuration
  ///
  PchLpcIoDecodeRangesSet(
    (V_LPC_CFG_IOD_LPT_378 << N_LPC_CFG_IOD_LPT) |
    (V_LPC_CFG_IOD_COMB_3E8 << N_LPC_CFG_IOD_COMB) |
    (V_LPC_CFG_IOD_COMA_3F8 << N_LPC_CFG_IOD_COMA)
  );

  PchLpcIoEnableDecodingSet(
    B_LPC_CFG_IOE_ME2 |
    B_LPC_CFG_IOE_SE |
    B_LPC_CFG_IOE_ME1 |
    B_LPC_CFG_IOE_KE |
    B_LPC_CFG_IOE_HGE |
    B_LPC_CFG_IOE_LGE |
    B_LPC_CFG_IOE_FDE |
    B_LPC_CFG_IOE_PPE |
    B_LPC_CFG_IOE_CBE |
    B_LPC_CFG_IOE_CAE
  );

  ///
  /// Enable LPC IO decode for EC access
  ///
  LpcBaseAddress = PCI_SEGMENT_LIB_ADDRESS(
    DEFAULT_PCI_SEGMENT_NUMBER_PCH,
    DEFAULT_PCI_BUS_NUMBER_PCH,
    PCI_DEVICE_NUMBER_PCH_LPC,
    PCI_FUNCTION_NUMBER_PCH_LPC,
    0
  );

  return EFI_SUCCESS;
}

EFI_BOOT_MODE
EFIAPI
WhiskeylakeURvpBoardBootModeDetect (
  VOID
  )
{
  return BOOT_WITH_FULL_CONFIGURATION;
}


