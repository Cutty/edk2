/** @file
  SEC Core Debug Agent Library instance implementition.

  Copyright (c) 2010 - 2013, Intel Corporation. All rights reserved.<BR>
  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php.

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "SecPeiDebugAgentLib.h"

BOOLEAN  mSkipBreakpoint = FALSE;

EFI_PEI_NOTIFY_DESCRIPTOR mMemoryDiscoveredNotifyList[1] = {
  {
    (EFI_PEI_PPI_DESCRIPTOR_NOTIFY_CALLBACK | EFI_PEI_PPI_DESCRIPTOR_TERMINATE_LIST),
    &gEfiPeiMemoryDiscoveredPpiGuid,
    DebugAgentCallbackMemoryDiscoveredPpi
  }
};

/**
  Check if debug agent support multi-processor.

  @retval TRUE    Multi-processor is supported.
  @retval FALSE   Multi-processor is not supported.

**/
BOOLEAN
MultiProcessorDebugSupport (
  VOID
  )
{
  return FALSE;
}

/**
  Read the Attach/Break-in symbols from the debug port.

  @param[in]  Handle         Pointer to Debug Port handle.
  @param[out] BreakSymbol    Returned break symbol.

  @retval EFI_SUCCESS        Read the symbol in BreakSymbol.
  @retval EFI_NOT_FOUND      No read the break symbol.

**/
EFI_STATUS
DebugReadBreakSymbol (
  IN  DEBUG_PORT_HANDLE      Handle,
  OUT UINT8                  *BreakSymbol
  )
{
  EFI_STATUS                 Status;
  DEBUG_PACKET_HEADER        DebugHeader;
  UINT8                      *Data8;

  *BreakSymbol = 0;
  //
  // If Debug Port buffer has data, read it till it was break symbol or Debug Port buffer empty.
  //
  Data8 = (UINT8 *) &DebugHeader;
  while (TRUE) {
    //
    // If start symbol is not received
    //
    if (!DebugPortPollBuffer (Handle)) {
      //
      // If no data in Debug Port, exit
      //
      break;
    }
    //
    // Try to read the start symbol
    //
    DebugPortReadBuffer (Handle, Data8, 1, 0);
    if (*Data8 == DEBUG_STARTING_SYMBOL_ATTACH) {
      *BreakSymbol = *Data8;
      DebugAgentMsgPrint (DEBUG_AGENT_INFO, "Debug Timer attach symbol received %x", *BreakSymbol);
      return EFI_SUCCESS;
    } 
    if (*Data8 == DEBUG_STARTING_SYMBOL_NORMAL) {
      Status = ReadRemainingBreakPacket (Handle, &DebugHeader);
      if (Status == EFI_SUCCESS) {
        *BreakSymbol = DebugHeader.Command;
        DebugAgentMsgPrint (DEBUG_AGENT_INFO, "Debug Timer break symbol received %x", *BreakSymbol);
        return EFI_SUCCESS;
      }
      if (Status == EFI_TIMEOUT) {
        break;
      }
    }
  }
  
  return EFI_NOT_FOUND;
}

/**
  Get the pointer to location saved Mailbox pointer from IDT entry.

**/
VOID *
GetLocationSavedMailboxPointerInIdtEntry (
  VOID
  )
{
  UINTN                     *MailboxLocation;

  MailboxLocation = (UINTN *) GetExceptionHandlerInIdtEntry (DEBUG_MAILBOX_VECTOR);
  //
  // *MailboxLocation is the pointer to Mailbox
  //
  VerifyMailboxChecksum ((DEBUG_AGENT_MAILBOX *) (*MailboxLocation));
  return MailboxLocation;
}

/**
  Set the pointer of Mailbox into IDT entry before memory is ready.

  @param[in]  MailboxLocation    Pointer to location saved Mailbox pointer.

**/
VOID
SetLocationSavedMailboxPointerInIdtEntry (
  IN VOID                  *MailboxLocation
  )
{
  SetExceptionHandlerInIdtEntry (DEBUG_MAILBOX_VECTOR, MailboxLocation);
}

/**
  Get the location of Mailbox pointer from the GUIDed HOB.

  @return Pointer to the location saved Mailbox pointer.

**/
UINT64 *
GetMailboxLocationFromHob (
  VOID
  )
{
  EFI_HOB_GUID_TYPE        *GuidHob;

  GuidHob = GetFirstGuidHob (&gEfiDebugAgentGuid);
  if (GuidHob == NULL) {
    return NULL;
  }
  return (UINT64 *) (GET_GUID_HOB_DATA(GuidHob));
}

/**
  Get Debug Agent Mailbox pointer.

  @return Mailbox pointer.

**/
DEBUG_AGENT_MAILBOX *
GetMailboxPointer (
  VOID
  )
{
  UINT64               DebugPortHandle;
  UINT64               *MailboxLocationInIdt;
  UINT64               *MailboxLocationInHob;
  DEBUG_AGENT_MAILBOX  *Mailbox;
  
  //
  // Get mailbox from IDT entry firstly
  //
  MailboxLocationInIdt = GetLocationSavedMailboxPointerInIdtEntry ();
  Mailbox = (DEBUG_AGENT_MAILBOX *)(UINTN)(*MailboxLocationInIdt);
  //
  // Check if mailbox was setup in PEI firstly, cannot used GetDebugFlag() to 
  // get CheckMailboxInHob flag to avoid GetMailboxPointer() nesting.
  //
  if (Mailbox->DebugFlag.Bits.CheckMailboxInHob != 1) {
    //
    // If mailbox in IDT entry has already been the final one
    //
    return Mailbox;
  }

  MailboxLocationInHob = GetMailboxLocationFromHob ();
  //
  // Compare mailbox in IDT enry with mailbox in HOB
  //
  if (MailboxLocationInHob != MailboxLocationInIdt && MailboxLocationInHob != NULL) {
    Mailbox = (DEBUG_AGENT_MAILBOX *)(UINTN)(*MailboxLocationInHob);
    //
    // Fix up Debug Port handler and save new mailbox in IDT entry
    //
    Mailbox = (DEBUG_AGENT_MAILBOX *)((UINTN)Mailbox + ((UINTN)(MailboxLocationInHob) - (UINTN)MailboxLocationInIdt));
    DebugPortHandle = (UINT64)((UINTN)Mailbox->DebugPortHandle + ((UINTN)(MailboxLocationInHob) - (UINTN)MailboxLocationInIdt));
    UpdateMailboxContent (Mailbox, DEBUG_MAILBOX_DEBUG_PORT_HANDLE_INDEX, DebugPortHandle);
    *MailboxLocationInHob = (UINT64)(UINTN)Mailbox;
    SetLocationSavedMailboxPointerInIdtEntry (MailboxLocationInHob);
    //
    // Clean CheckMailboxInHob flag
    //
    Mailbox->DebugFlag.Bits.CheckMailboxInHob = 0;
    UpdateMailboxChecksum (Mailbox);
  }

  return Mailbox;
}

/**
  Get debug port handle.

  @return Debug port handle.

**/
DEBUG_PORT_HANDLE
GetDebugPortHandle (
  VOID
  )
{
  DEBUG_AGENT_MAILBOX    *DebugAgentMailbox;
  
  DebugAgentMailbox = GetMailboxPointer ();

  return (DEBUG_PORT_HANDLE) (UINTN)(DebugAgentMailbox->DebugPortHandle);
}

/**
  Debug Agent provided notify callback function on Memory Discovered PPI.

  @param[in] PeiServices      Indirect reference to the PEI Services Table.
  @param[in] NotifyDescriptor Address of the notification descriptor data structure.
  @param[in] Ppi              Address of the PPI that was installed.

  @retval EFI_SUCCESS If the function completed successfully.

**/
EFI_STATUS
EFIAPI
DebugAgentCallbackMemoryDiscoveredPpi (
  IN EFI_PEI_SERVICES                     **PeiServices,
  IN EFI_PEI_NOTIFY_DESCRIPTOR            *NotifyDescriptor,
  IN VOID                                 *Ppi
  )
{
  DEBUG_AGENT_MAILBOX            *Mailbox;
  BOOLEAN                        InterruptStatus;
  
  //
  // Save and disable original interrupt status
  //
  InterruptStatus = SaveAndDisableInterrupts ();
  
  //
  // Set physical memory ready flag
  //
  Mailbox = GetMailboxPointer ();
  SetDebugFlag (DEBUG_AGENT_FLAG_MEMORY_READY, 1);

  //
  // Memory has been ready
  //
  if (IsHostAttached ()) {
    //
    // Trigger one software interrupt to inform HOST
    //
    TriggerSoftInterrupt (MEMORY_READY_SIGNATURE);
  }

  //
  // Restore interrupt state.
  //
  SetInterruptState (InterruptStatus);
  
  return EFI_SUCCESS;
}

/**
  Initialize debug agent.

  This function is used to set up debug environment for SEC and PEI phase.

  If InitFlag is DEBUG_AGENT_INIT_PREMEM_SEC, it will overirde IDT table entries
  and initialize debug port. It will enable interrupt to support break-in feature.
  It will set up debug agent Mailbox in cache-as-ramfrom. It will be called before
  physical memory is ready.
  If InitFlag is DEBUG_AGENT_INIT_POSTMEM_SEC, debug agent will build one GUIDed
  HOB to copy debug agent Mailbox. It will be called after physical memory is ready.

  This function is used to set up debug environment to support source level debugging.
  If certain Debug Agent Library instance has to save some private data in the stack,
  this function must work on the mode that doesn't return to the caller, then
  the caller needs to wrap up all rest of logic after InitializeDebugAgent() into one
  function and pass it into InitializeDebugAgent(). InitializeDebugAgent() is
  responsible to invoke the passing-in function at the end of InitializeDebugAgent().

  If the parameter Function is not NULL, Debug Agent Libary instance will invoke it by
  passing in the Context to be its parameter.

  If Function() is NULL, Debug Agent Library instance will return after setup debug
  environment.

  @param[in] InitFlag     Init flag is used to decide the initialize process.
  @param[in] Context      Context needed according to InitFlag; it was optional.
  @param[in] Function     Continue function called by debug agent library; it was
                          optional.

**/
VOID
EFIAPI
InitializeDebugAgent (
  IN UINT32                InitFlag,
  IN VOID                  *Context, OPTIONAL
  IN DEBUG_AGENT_CONTINUE  Function  OPTIONAL
  )
{
  DEBUG_AGENT_MAILBOX              *Mailbox;
  DEBUG_AGENT_MAILBOX              MailboxInStack;
  DEBUG_AGENT_PHASE2_CONTEXT       Phase2Context;
  DEBUG_AGENT_CONTEXT_POSTMEM_SEC  *DebugAgentContext;
  EFI_STATUS                       Status;
  IA32_DESCRIPTOR                  *Ia32Idtr;
  IA32_IDT_ENTRY                   *Ia32IdtEntry;
  UINT64                           DebugPortHandle;
  UINT64                           MailboxLocation;
  UINT64                           *MailboxLocationPointer;
  
  DisableInterrupts ();

  switch (InitFlag) {

  case DEBUG_AGENT_INIT_PREMEM_SEC:

    InitializeDebugIdt ();

    MailboxLocation = (UINT64)(UINTN)&MailboxInStack;
    Mailbox = &MailboxInStack;
    ZeroMem ((VOID *) Mailbox, sizeof (DEBUG_AGENT_MAILBOX));
    //
    // Get and save debug port handle and set the length of memory block.
    //
    SetLocationSavedMailboxPointerInIdtEntry (&MailboxLocation);
    SetDebugFlag (DEBUG_AGENT_FLAG_PRINT_ERROR_LEVEL, DEBUG_AGENT_ERROR);

    InitializeDebugTimer ();

    Phase2Context.Context  = Context;
    Phase2Context.Function = Function;
    DebugPortInitialize ((VOID *) &Phase2Context, InitializeDebugAgentPhase2);
    //
    // If reaches here, it means Debug Port initialization failed.
    //
    DEBUG ((EFI_D_ERROR, "Debug Agent: Debug port initialization failed.\n"));

    break;

  case DEBUG_AGENT_INIT_POSTMEM_SEC:
    Mailbox = GetMailboxPointer ();
    //
    // Memory has been ready
    //
    SetDebugFlag (DEBUG_AGENT_FLAG_MEMORY_READY, 1);
    if (IsHostAttached ()) {
      //
      // Trigger one software interrupt to inform HOST
      //
      TriggerSoftInterrupt (MEMORY_READY_SIGNATURE);
    }

    //
    // Fix up Debug Port handle address and mailbox address
    //
    DebugAgentContext = (DEBUG_AGENT_CONTEXT_POSTMEM_SEC *) Context;
    DebugPortHandle = (UINT64)(UINT32)(Mailbox->DebugPortHandle + DebugAgentContext->StackMigrateOffset);
    UpdateMailboxContent (Mailbox, DEBUG_MAILBOX_DEBUG_PORT_HANDLE_INDEX, DebugPortHandle);
    Mailbox = (DEBUG_AGENT_MAILBOX *) ((UINTN) Mailbox + DebugAgentContext->StackMigrateOffset);
    MailboxLocation = (UINT64)(UINTN)Mailbox;
    //
    // Build mailbox location in HOB and fix-up its address
    //
    MailboxLocationPointer = BuildGuidDataHob (
                               &gEfiDebugAgentGuid,
                               &MailboxLocation,
                               sizeof (UINT64)
                               );
    MailboxLocationPointer = (UINT64 *) ((UINTN) MailboxLocationPointer + DebugAgentContext->HeapMigrateOffset);
    //
    // Update IDT entry to save the location saved mailbox pointer
    //
    SetLocationSavedMailboxPointerInIdtEntry (MailboxLocationPointer);
    //
    // Enable CPU interrupts so debug timer interrupts can be delivered
    //
    EnableInterrupts ();

    break;

  case DEBUG_AGENT_INIT_PEI:
    //
    // Check if Debug Agent has initialized before
    //
    if (IsDebugAgentInitialzed()) {
      DEBUG ((EFI_D_WARN, "Debug Agent: It has already initialized in SEC Core!\n"));
      break;
    }
    //
    // Set up IDT entries
    //
    InitializeDebugIdt ();
    //
    // Build mailbox in HOB and setup Mailbox Set In Pei flag
    //
    Mailbox = AllocateZeroPool (sizeof (DEBUG_AGENT_MAILBOX));
    MailboxLocation = (UINT64)(UINTN)Mailbox;
    MailboxLocationPointer = BuildGuidDataHob (
                               &gEfiDebugAgentGuid,
                               &MailboxLocation,
                               sizeof (UINT64)
                               );

    InitializeDebugTimer ();
    //
    // Update IDT entry to save the location pointer saved mailbox pointer
    //
    SetLocationSavedMailboxPointerInIdtEntry (MailboxLocationPointer);
    //
    // Register for a callback once memory has been initialized.
    // If memery has been ready, the callback funtion will be invoked immediately
    //
    Status = PeiServicesNotifyPpi (&mMemoryDiscoveredNotifyList[0]);
    ASSERT_EFI_ERROR (Status);
    //
    // Set HOB check flag if memory has not been ready yet
    //
    if (GetDebugFlag (DEBUG_AGENT_FLAG_MEMORY_READY) == 0) {
      SetDebugFlag (DEBUG_AGENT_FLAG_CHECK_MAILBOX_IN_HOB, 1);
    }

    Phase2Context.Context  = Context;
    Phase2Context.Function = Function;
    DebugPortInitialize ((VOID *) &Phase2Context, InitializeDebugAgentPhase2);

    FindAndReportModuleImageInfo (4);

    break;

  case DEBUG_AGENT_INIT_THUNK_PEI_IA32TOX64:
    if (Context == NULL) {
      DEBUG ((EFI_D_ERROR, "DebugAgent: Input parameter Context cannot be NULL!\n"));
      CpuDeadLoop ();
    } else {
      Ia32Idtr =  (IA32_DESCRIPTOR *) Context;
      Ia32IdtEntry = (IA32_IDT_ENTRY *)(Ia32Idtr->Base);
      MailboxLocationPointer = (UINT64 *) (UINTN) (Ia32IdtEntry[DEBUG_MAILBOX_VECTOR].Bits.OffsetLow +
                                                (Ia32IdtEntry[DEBUG_MAILBOX_VECTOR].Bits.OffsetHigh << 16));
      Mailbox = (DEBUG_AGENT_MAILBOX *) (UINTN)(*MailboxLocationPointer);
      VerifyMailboxChecksum (Mailbox);

      DebugPortHandle = (UINT64) (UINTN)DebugPortInitialize ((VOID *)(UINTN)Mailbox->DebugPortHandle, NULL);
      UpdateMailboxContent (Mailbox, DEBUG_MAILBOX_DEBUG_PORT_HANDLE_INDEX, DebugPortHandle);
      //
      // Set up IDT entries
      //
      InitializeDebugIdt ();
      //
      // Update IDT entry to save location pointer saved the mailbox pointer
      //
      SetLocationSavedMailboxPointerInIdtEntry (MailboxLocationPointer);

      FindAndReportModuleImageInfo (4);
    }
    break;

  default:
    //
    // Only DEBUG_AGENT_INIT_PREMEM_SEC and DEBUG_AGENT_INIT_POSTMEM_SEC are allowed for this 
    // Debug Agent library instance.
    //
    DEBUG ((EFI_D_ERROR, "Debug Agent: The InitFlag value is not allowed!\n"));
    CpuDeadLoop ();
    break;

  }

  EnableInterrupts ();

  //
  // If Function is not NULL, invoke it always whatever debug agent was initialized sucesssfully or not.
  //
  if (Function != NULL) {
    Function (Context);
  }
}

/**
  Caller provided function to be invoked at the end of DebugPortInitialize().

  Refer to the descrption for DebugPortInitialize() for more details.

  @param[in] Context           The first input argument of DebugPortInitialize().
  @param[in] DebugPortHandle   Debug port handle created by Debug Communication Libary.

**/
VOID
EFIAPI
InitializeDebugAgentPhase2 (
  IN VOID                  *Context,
  IN DEBUG_PORT_HANDLE     DebugPortHandle
  )
{
  DEBUG_AGENT_PHASE2_CONTEXT *Phase2Context;
  UINT64                     *MailboxLocation;
  DEBUG_AGENT_MAILBOX        *Mailbox;
  EFI_SEC_PEI_HAND_OFF       *SecCoreData;
  UINT16                     BufferSize;
  UINT64                     NewDebugPortHandle;

  Phase2Context = (DEBUG_AGENT_PHASE2_CONTEXT *) Context;
  MailboxLocation = GetLocationSavedMailboxPointerInIdtEntry ();
  Mailbox = (DEBUG_AGENT_MAILBOX *)(UINTN)(*MailboxLocation);
  BufferSize = PcdGet16(PcdDebugPortHandleBufferSize);
  if (Phase2Context->Function == NULL && DebugPortHandle != NULL && BufferSize != 0) {
    NewDebugPortHandle = (UINT64)(UINTN)AllocateCopyPool (BufferSize, DebugPortHandle);
  } else {
    NewDebugPortHandle = (UINT64)(UINTN)DebugPortHandle;
  }
  UpdateMailboxContent (Mailbox, DEBUG_MAILBOX_DEBUG_PORT_HANDLE_INDEX, NewDebugPortHandle);

  //
  // Trigger one software interrupt to inform HOST
  //
  TriggerSoftInterrupt (SYSTEM_RESET_SIGNATURE);

  //
  // If Temporary RAM region is below 128 MB, then send message to 
  // host to disable low memory filtering.
  //
  SecCoreData = (EFI_SEC_PEI_HAND_OFF *)Phase2Context->Context;
  if ((UINTN)SecCoreData->TemporaryRamBase < BASE_128MB && IsHostAttached ()) {
    SetDebugFlag (DEBUG_AGENT_FLAG_MEMORY_READY, 1);
    TriggerSoftInterrupt (MEMORY_READY_SIGNATURE);
  }

  //
  // Enable CPU interrupts so debug timer interrupts can be delivered
  //
  EnableInterrupts ();

  //
  // Call continuation function if it is not NULL.
  //
  if (Phase2Context->Function != NULL) {
    Phase2Context->Function (Phase2Context->Context);
  }
}
