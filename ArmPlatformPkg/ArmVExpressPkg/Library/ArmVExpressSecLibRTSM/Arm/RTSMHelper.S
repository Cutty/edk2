#
#  Copyright (c) 2011-2012, ARM Limited. All rights reserved.
#  
#  This program and the accompanying materials                          
#  are licensed and made available under the terms and conditions of the BSD License         
#  which accompanies this distribution.  The full text of the license may be found at        
#  http:#opensource.org/licenses/bsd-license.php                                            
#
#  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,                     
#  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.             
#
#

#include <AsmMacroIoLib.h>
#include <Base.h>
#include <Library/PcdLib.h>
#include <AutoGen.h>
#.include AsmMacroIoLib.inc

#include <Chipset/ArmCortexA9.h>

.text
.align 2

GCC_ASM_EXPORT(ArmGetCpuCountPerCluster)

# IN None
# OUT r0 = SCU Base Address
ASM_PFX(ArmGetScuBaseAddress):
  # Read Configuration Base Address Register. ArmCBar cannot be called to get
  # the Configuration BAR as a stack is not necessary setup. The SCU is at the
  # offset 0x0000 from the Private Memory Region.
  mrc   p15, 4, r0, c15, c0, 0
  bx  lr

# IN None
# OUT r0 = number of cores present in the system
ASM_PFX(ArmGetCpuCountPerCluster):
  stmfd SP!, {r1-r2}

  # Read CP15 MIDR
  mrc   p15, 0, r1, c0, c0, 0

  # Check if the CPU is A15
  mov   r1, r1, LSR #4
  LoadConstantToReg (ARM_CPU_TYPE_MASK, r0)
  and   r1, r1, r0

  LoadConstantToReg (ARM_CPU_TYPE_A15, r0)
  cmp   r1, r0
  beq   _Read_cp15_reg

_CPU_is_not_A15:
  mov   r2, lr                           @ Save link register
  bl    ArmGetScuBaseAddress             @ Read SCU Base Address
  mov   lr, r2                           @ Restore link register val
  ldr   r0, [r0, #A9_SCU_CONFIG_OFFSET]     @ Read SCU Config reg to get CPU count
  b     _Return

_Read_cp15_reg:
  mrc   p15, 1, r0, c9, c0, 2            @ Read C9 register of CP15 to get CPU count
  lsr   r0, #24

_Return:
  and   r0, r0, #3
  # Add '1' to the number of CPU on the Cluster
  add   r0, r0, #1
  ldmfd SP!, {r1-r2}
  bx lr

ASM_FUNCTION_REMOVE_IF_UNREFERENCED 
