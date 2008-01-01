//===- SPUInstrInfo.cpp - Cell SPU Instruction Information ----------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the Cell SPU implementation of the TargetInstrInfo class.
//
//===----------------------------------------------------------------------===//

#include "SPURegisterNames.h"
#include "SPUInstrInfo.h"
#include "SPUTargetMachine.h"
#include "SPUGenInstrInfo.inc"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include <iostream>

using namespace llvm;

SPUInstrInfo::SPUInstrInfo(SPUTargetMachine &tm)
  : TargetInstrInfoImpl(SPUInsts, sizeof(SPUInsts)/sizeof(SPUInsts[0])),
    TM(tm),
    RI(*TM.getSubtargetImpl(), *this)
{
  /* NOP */
}

/// getPointerRegClass - Return the register class to use to hold pointers.
/// This is used for addressing modes.
const TargetRegisterClass *
SPUInstrInfo::getPointerRegClass() const
{
  return &SPU::R32CRegClass;
}

bool
SPUInstrInfo::isMoveInstr(const MachineInstr& MI,
                          unsigned& sourceReg,
                          unsigned& destReg) const {
  // Primarily, ORI and OR are generated by copyRegToReg. But, there are other
  // cases where we can safely say that what's being done is really a move
  // (see how PowerPC does this -- it's the model for this code too.)
  switch (MI.getOpcode()) {
  default:
    break;
  case SPU::ORIv4i32:
  case SPU::ORIr32:
  case SPU::ORIr64:
  case SPU::ORHIv8i16:
  case SPU::ORHIr16:
  case SPU::ORHI1To2:
  case SPU::ORBIv16i8:
  case SPU::ORBIr8:
  case SPU::ORI2To4:
  case SPU::ORI1To4:
  case SPU::AHIvec:
  case SPU::AHIr16:
  case SPU::AIvec:
    assert(MI.getNumOperands() == 3 &&
           MI.getOperand(0).isRegister() &&
           MI.getOperand(1).isRegister() &&
           MI.getOperand(2).isImmediate() &&
           "invalid SPU ORI/ORHI/ORBI/AHI/AI/SFI/SFHI instruction!");
    if (MI.getOperand(2).getImm() == 0) {
      sourceReg = MI.getOperand(1).getReg();
      destReg = MI.getOperand(0).getReg();
      return true;
    }
    break;
  case SPU::AIr32:
    assert(MI.getNumOperands() == 3 &&
           "wrong number of operands to AIr32");
    if (MI.getOperand(0).isRegister() &&
        (MI.getOperand(1).isRegister() ||
         MI.getOperand(1).isFrameIndex()) &&
        (MI.getOperand(2).isImmediate() &&
         MI.getOperand(2).getImm() == 0)) {
      sourceReg = MI.getOperand(1).getReg();
      destReg = MI.getOperand(0).getReg();
      return true;
    }
    break;
  case SPU::ORv16i8_i8:
  case SPU::ORv8i16_i16:
  case SPU::ORv4i32_i32:
  case SPU::ORv2i64_i64:
  case SPU::ORv4f32_f32:
  case SPU::ORv2f64_f64:
  case SPU::ORi8_v16i8:
  case SPU::ORi16_v8i16:
  case SPU::ORi32_v4i32:
  case SPU::ORi64_v2i64:
  case SPU::ORf32_v4f32:
  case SPU::ORf64_v2f64:
  case SPU::ORv16i8:
  case SPU::ORv8i16:
  case SPU::ORv4i32:
  case SPU::ORr32:
  case SPU::ORr64:
  case SPU::ORf32:
  case SPU::ORf64:
  case SPU::ORgprc:
    assert(MI.getNumOperands() == 3 &&
           MI.getOperand(0).isRegister() &&
           MI.getOperand(1).isRegister() &&
           MI.getOperand(2).isRegister() &&
           "invalid SPU OR(vec|r32|r64|gprc) instruction!");
    if (MI.getOperand(1).getReg() == MI.getOperand(2).getReg()) {
      sourceReg = MI.getOperand(1).getReg();
      destReg = MI.getOperand(0).getReg();
      return true;
    }
    break;
  }

  return false;
}

unsigned
SPUInstrInfo::isLoadFromStackSlot(MachineInstr *MI, int &FrameIndex) const {
  switch (MI->getOpcode()) {
  default: break;
  case SPU::LQDv16i8:
  case SPU::LQDv8i16:
  case SPU::LQDv4i32:
  case SPU::LQDv4f32:
  case SPU::LQDv2f64:
  case SPU::LQDr128:
  case SPU::LQDr64:
  case SPU::LQDr32:
  case SPU::LQDr16:
  case SPU::LQXv4i32:
  case SPU::LQXr128:
  case SPU::LQXr64:
  case SPU::LQXr32:
  case SPU::LQXr16:
    if (MI->getOperand(1).isImmediate() && !MI->getOperand(1).getImm() &&
        MI->getOperand(2).isFrameIndex()) {
      FrameIndex = MI->getOperand(2).getIndex();
      return MI->getOperand(0).getReg();
    }
    break;
  }
  return 0;
}

unsigned
SPUInstrInfo::isStoreToStackSlot(MachineInstr *MI, int &FrameIndex) const {
  switch (MI->getOpcode()) {
  default: break;
  case SPU::STQDv16i8:
  case SPU::STQDv8i16:
  case SPU::STQDv4i32:
  case SPU::STQDv4f32:
  case SPU::STQDv2f64:
  case SPU::STQDr128:
  case SPU::STQDr64:
  case SPU::STQDr32:
  case SPU::STQDr16:
    // case SPU::STQDr8:
  case SPU::STQXv16i8:
  case SPU::STQXv8i16:
  case SPU::STQXv4i32:
  case SPU::STQXv4f32:
  case SPU::STQXv2f64:
  case SPU::STQXr128:
  case SPU::STQXr64:
  case SPU::STQXr32:
  case SPU::STQXr16:
    // case SPU::STQXr8:
    if (MI->getOperand(1).isImmediate() && !MI->getOperand(1).getImm() &&
        MI->getOperand(2).isFrameIndex()) {
      FrameIndex = MI->getOperand(2).getIndex();
      return MI->getOperand(0).getReg();
    }
    break;
  }
  return 0;
}

void SPUInstrInfo::copyRegToReg(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MI,
                                   unsigned DestReg, unsigned SrcReg,
                                   const TargetRegisterClass *DestRC,
                                   const TargetRegisterClass *SrcRC) const
{
  if (DestRC != SrcRC) {
    cerr << "SPURegisterInfo::copyRegToReg(): DestRC != SrcRC not supported!\n";
    abort();
  }

  if (DestRC == SPU::R8CRegisterClass) {
    BuildMI(MBB, MI, get(SPU::ORBIr8), DestReg).addReg(SrcReg).addImm(0);
  } else if (DestRC == SPU::R16CRegisterClass) {
    BuildMI(MBB, MI, get(SPU::ORHIr16), DestReg).addReg(SrcReg).addImm(0);
  } else if (DestRC == SPU::R32CRegisterClass) {
    BuildMI(MBB, MI, get(SPU::ORIr32), DestReg).addReg(SrcReg).addImm(0);
  } else if (DestRC == SPU::R32FPRegisterClass) {
    BuildMI(MBB, MI, get(SPU::ORf32), DestReg).addReg(SrcReg)
      .addReg(SrcReg);
  } else if (DestRC == SPU::R64CRegisterClass) {
    BuildMI(MBB, MI, get(SPU::ORIr64), DestReg).addReg(SrcReg).addImm(0);
  } else if (DestRC == SPU::R64FPRegisterClass) {
    BuildMI(MBB, MI, get(SPU::ORf64), DestReg).addReg(SrcReg)
      .addReg(SrcReg);
  } else if (DestRC == SPU::GPRCRegisterClass) {
    BuildMI(MBB, MI, get(SPU::ORgprc), DestReg).addReg(SrcReg)
      .addReg(SrcReg);
  } else if (DestRC == SPU::VECREGRegisterClass) {
    BuildMI(MBB, MI, get(SPU::ORv4i32), DestReg).addReg(SrcReg)
      .addReg(SrcReg);
  } else {
    std::cerr << "Attempt to copy unknown/unsupported register class!\n";
    abort();
  }
}
