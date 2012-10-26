//===-- TwoAddressInstructionPass.cpp - Two-Address instruction pass ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the TwoAddress instruction pass which is used
// by most register allocators. Two-Address instructions are rewritten
// from:
//
//     A = B op C
//
// to:
//
//     A = B
//     A op= C
//
// Note that if a register allocator chooses to use this pass, that it
// has to be capable of handling the non-SSA nature of these rewritten
// virtual registers.
//
// It is also worth noting that the duplicate operand of the two
// address instruction is removed.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "twoaddrinstr"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Function.h"
#include "llvm/CodeGen/LiveIntervalAnalysis.h"
#include "llvm/CodeGen/LiveVariables.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/MC/MCInstrItineraries.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h"
using namespace llvm;

STATISTIC(NumTwoAddressInstrs, "Number of two-address instructions");
STATISTIC(NumCommuted        , "Number of instructions commuted to coalesce");
STATISTIC(NumAggrCommuted    , "Number of instructions aggressively commuted");
STATISTIC(NumConvertedTo3Addr, "Number of instructions promoted to 3-address");
STATISTIC(Num3AddrSunk,        "Number of 3-address instructions sunk");
STATISTIC(NumReSchedUps,       "Number of instructions re-scheduled up");
STATISTIC(NumReSchedDowns,     "Number of instructions re-scheduled down");

namespace {
class TwoAddressInstructionPass : public MachineFunctionPass {
  MachineFunction *MF;
  const TargetInstrInfo *TII;
  const TargetRegisterInfo *TRI;
  const InstrItineraryData *InstrItins;
  MachineRegisterInfo *MRI;
  LiveVariables *LV;
  SlotIndexes *Indexes;
  LiveIntervals *LIS;
  AliasAnalysis *AA;
  CodeGenOpt::Level OptLevel;

  // DistanceMap - Keep track the distance of a MI from the start of the
  // current basic block.
  DenseMap<MachineInstr*, unsigned> DistanceMap;

  // SrcRegMap - A map from virtual registers to physical registers which are
  // likely targets to be coalesced to due to copies from physical registers to
  // virtual registers. e.g. v1024 = move r0.
  DenseMap<unsigned, unsigned> SrcRegMap;

  // DstRegMap - A map from virtual registers to physical registers which are
  // likely targets to be coalesced to due to copies to physical registers from
  // virtual registers. e.g. r1 = move v1024.
  DenseMap<unsigned, unsigned> DstRegMap;

  /// RegSequences - Keep track the list of REG_SEQUENCE instructions seen
  /// during the initial walk of the machine function.
  SmallVector<MachineInstr*, 16> RegSequences;

  bool sink3AddrInstruction(MachineBasicBlock *MBB, MachineInstr *MI,
                            unsigned Reg,
                            MachineBasicBlock::iterator OldPos);

  bool noUseAfterLastDef(unsigned Reg, MachineBasicBlock *MBB, unsigned Dist,
                         unsigned &LastDef);

  bool isProfitableToCommute(unsigned regA, unsigned regB, unsigned regC,
                             MachineInstr *MI, MachineBasicBlock *MBB,
                             unsigned Dist);

  bool commuteInstruction(MachineBasicBlock::iterator &mi,
                          MachineFunction::iterator &mbbi,
                          unsigned RegB, unsigned RegC, unsigned Dist);

  bool isProfitableToConv3Addr(unsigned RegA, unsigned RegB);

  bool convertInstTo3Addr(MachineBasicBlock::iterator &mi,
                          MachineBasicBlock::iterator &nmi,
                          MachineFunction::iterator &mbbi,
                          unsigned RegA, unsigned RegB, unsigned Dist);

  bool isDefTooClose(unsigned Reg, unsigned Dist,
                     MachineInstr *MI, MachineBasicBlock *MBB);

  bool rescheduleMIBelowKill(MachineBasicBlock *MBB,
                             MachineBasicBlock::iterator &mi,
                             MachineBasicBlock::iterator &nmi,
                             unsigned Reg);
  bool rescheduleKillAboveMI(MachineBasicBlock *MBB,
                             MachineBasicBlock::iterator &mi,
                             MachineBasicBlock::iterator &nmi,
                             unsigned Reg);

  bool tryInstructionTransform(MachineBasicBlock::iterator &mi,
                               MachineBasicBlock::iterator &nmi,
                               MachineFunction::iterator &mbbi,
                               unsigned SrcIdx, unsigned DstIdx,
                               unsigned Dist,
                               SmallPtrSet<MachineInstr*, 8> &Processed);

  void scanUses(unsigned DstReg, MachineBasicBlock *MBB,
                SmallPtrSet<MachineInstr*, 8> &Processed);

  void processCopy(MachineInstr *MI, MachineBasicBlock *MBB,
                   SmallPtrSet<MachineInstr*, 8> &Processed);

  typedef SmallVector<std::pair<unsigned, unsigned>, 4> TiedPairList;
  typedef SmallDenseMap<unsigned, TiedPairList> TiedOperandMap;
  bool collectTiedOperands(MachineInstr *MI, TiedOperandMap&);
  void processTiedPairs(MachineInstr *MI, TiedPairList&, unsigned &Dist);

  /// eliminateRegSequences - Eliminate REG_SEQUENCE instructions as part of
  /// the de-ssa process. This replaces sources of REG_SEQUENCE as sub-register
  /// references of the register defined by REG_SEQUENCE.
  bool eliminateRegSequences();

public:
  static char ID; // Pass identification, replacement for typeid
  TwoAddressInstructionPass() : MachineFunctionPass(ID) {
    initializeTwoAddressInstructionPassPass(*PassRegistry::getPassRegistry());
  }

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.setPreservesCFG();
    AU.addRequired<AliasAnalysis>();
    AU.addPreserved<LiveVariables>();
    AU.addPreserved<SlotIndexes>();
    AU.addPreserved<LiveIntervals>();
    AU.addPreservedID(MachineLoopInfoID);
    AU.addPreservedID(MachineDominatorsID);
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  /// runOnMachineFunction - Pass entry point.
  bool runOnMachineFunction(MachineFunction&);
};
} // end anonymous namespace

char TwoAddressInstructionPass::ID = 0;
INITIALIZE_PASS_BEGIN(TwoAddressInstructionPass, "twoaddressinstruction",
                "Two-Address instruction pass", false, false)
INITIALIZE_AG_DEPENDENCY(AliasAnalysis)
INITIALIZE_PASS_END(TwoAddressInstructionPass, "twoaddressinstruction",
                "Two-Address instruction pass", false, false)

char &llvm::TwoAddressInstructionPassID = TwoAddressInstructionPass::ID;

/// sink3AddrInstruction - A two-address instruction has been converted to a
/// three-address instruction to avoid clobbering a register. Try to sink it
/// past the instruction that would kill the above mentioned register to reduce
/// register pressure.
bool TwoAddressInstructionPass::sink3AddrInstruction(MachineBasicBlock *MBB,
                                           MachineInstr *MI, unsigned SavedReg,
                                           MachineBasicBlock::iterator OldPos) {
  // FIXME: Shouldn't we be trying to do this before we three-addressify the
  // instruction?  After this transformation is done, we no longer need
  // the instruction to be in three-address form.

  // Check if it's safe to move this instruction.
  bool SeenStore = true; // Be conservative.
  if (!MI->isSafeToMove(TII, AA, SeenStore))
    return false;

  unsigned DefReg = 0;
  SmallSet<unsigned, 4> UseRegs;

  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg())
      continue;
    unsigned MOReg = MO.getReg();
    if (!MOReg)
      continue;
    if (MO.isUse() && MOReg != SavedReg)
      UseRegs.insert(MO.getReg());
    if (!MO.isDef())
      continue;
    if (MO.isImplicit())
      // Don't try to move it if it implicitly defines a register.
      return false;
    if (DefReg)
      // For now, don't move any instructions that define multiple registers.
      return false;
    DefReg = MO.getReg();
  }

  // Find the instruction that kills SavedReg.
  MachineInstr *KillMI = NULL;
  for (MachineRegisterInfo::use_nodbg_iterator
         UI = MRI->use_nodbg_begin(SavedReg),
         UE = MRI->use_nodbg_end(); UI != UE; ++UI) {
    MachineOperand &UseMO = UI.getOperand();
    if (!UseMO.isKill())
      continue;
    KillMI = UseMO.getParent();
    break;
  }

  // If we find the instruction that kills SavedReg, and it is in an
  // appropriate location, we can try to sink the current instruction
  // past it.
  if (!KillMI || KillMI->getParent() != MBB || KillMI == MI ||
      KillMI == OldPos || KillMI->isTerminator())
    return false;

  // If any of the definitions are used by another instruction between the
  // position and the kill use, then it's not safe to sink it.
  //
  // FIXME: This can be sped up if there is an easy way to query whether an
  // instruction is before or after another instruction. Then we can use
  // MachineRegisterInfo def / use instead.
  MachineOperand *KillMO = NULL;
  MachineBasicBlock::iterator KillPos = KillMI;
  ++KillPos;

  unsigned NumVisited = 0;
  for (MachineBasicBlock::iterator I = llvm::next(OldPos); I != KillPos; ++I) {
    MachineInstr *OtherMI = I;
    // DBG_VALUE cannot be counted against the limit.
    if (OtherMI->isDebugValue())
      continue;
    if (NumVisited > 30)  // FIXME: Arbitrary limit to reduce compile time cost.
      return false;
    ++NumVisited;
    for (unsigned i = 0, e = OtherMI->getNumOperands(); i != e; ++i) {
      MachineOperand &MO = OtherMI->getOperand(i);
      if (!MO.isReg())
        continue;
      unsigned MOReg = MO.getReg();
      if (!MOReg)
        continue;
      if (DefReg == MOReg)
        return false;

      if (MO.isKill()) {
        if (OtherMI == KillMI && MOReg == SavedReg)
          // Save the operand that kills the register. We want to unset the kill
          // marker if we can sink MI past it.
          KillMO = &MO;
        else if (UseRegs.count(MOReg))
          // One of the uses is killed before the destination.
          return false;
      }
    }
  }
  assert(KillMO && "Didn't find kill");

  // Update kill and LV information.
  KillMO->setIsKill(false);
  KillMO = MI->findRegisterUseOperand(SavedReg, false, TRI);
  KillMO->setIsKill(true);

  if (LV)
    LV->replaceKillInstruction(SavedReg, KillMI, MI);

  // Move instruction to its destination.
  MBB->remove(MI);
  MBB->insert(KillPos, MI);

  if (LIS)
    LIS->handleMove(MI);

  ++Num3AddrSunk;
  return true;
}

/// noUseAfterLastDef - Return true if there are no intervening uses between the
/// last instruction in the MBB that defines the specified register and the
/// two-address instruction which is being processed. It also returns the last
/// def location by reference
bool TwoAddressInstructionPass::noUseAfterLastDef(unsigned Reg,
                                                  MachineBasicBlock *MBB,
                                                  unsigned Dist,
                                                  unsigned &LastDef) {
  LastDef = 0;
  unsigned LastUse = Dist;
  for (MachineRegisterInfo::reg_iterator I = MRI->reg_begin(Reg),
         E = MRI->reg_end(); I != E; ++I) {
    MachineOperand &MO = I.getOperand();
    MachineInstr *MI = MO.getParent();
    if (MI->getParent() != MBB || MI->isDebugValue())
      continue;
    DenseMap<MachineInstr*, unsigned>::iterator DI = DistanceMap.find(MI);
    if (DI == DistanceMap.end())
      continue;
    if (MO.isUse() && DI->second < LastUse)
      LastUse = DI->second;
    if (MO.isDef() && DI->second > LastDef)
      LastDef = DI->second;
  }

  return !(LastUse > LastDef && LastUse < Dist);
}

/// isCopyToReg - Return true if the specified MI is a copy instruction or
/// a extract_subreg instruction. It also returns the source and destination
/// registers and whether they are physical registers by reference.
static bool isCopyToReg(MachineInstr &MI, const TargetInstrInfo *TII,
                        unsigned &SrcReg, unsigned &DstReg,
                        bool &IsSrcPhys, bool &IsDstPhys) {
  SrcReg = 0;
  DstReg = 0;
  if (MI.isCopy()) {
    DstReg = MI.getOperand(0).getReg();
    SrcReg = MI.getOperand(1).getReg();
  } else if (MI.isInsertSubreg() || MI.isSubregToReg()) {
    DstReg = MI.getOperand(0).getReg();
    SrcReg = MI.getOperand(2).getReg();
  } else
    return false;

  IsSrcPhys = TargetRegisterInfo::isPhysicalRegister(SrcReg);
  IsDstPhys = TargetRegisterInfo::isPhysicalRegister(DstReg);
  return true;
}

/// isKilled - Test if the given register value, which is used by the given
/// instruction, is killed by the given instruction. This looks through
/// coalescable copies to see if the original value is potentially not killed.
///
/// For example, in this code:
///
///   %reg1034 = copy %reg1024
///   %reg1035 = copy %reg1025<kill>
///   %reg1036 = add %reg1034<kill>, %reg1035<kill>
///
/// %reg1034 is not considered to be killed, since it is copied from a
/// register which is not killed. Treating it as not killed lets the
/// normal heuristics commute the (two-address) add, which lets
/// coalescing eliminate the extra copy.
///
static bool isKilled(MachineInstr &MI, unsigned Reg,
                     const MachineRegisterInfo *MRI,
                     const TargetInstrInfo *TII) {
  MachineInstr *DefMI = &MI;
  for (;;) {
    if (!DefMI->killsRegister(Reg))
      return false;
    if (TargetRegisterInfo::isPhysicalRegister(Reg))
      return true;
    MachineRegisterInfo::def_iterator Begin = MRI->def_begin(Reg);
    // If there are multiple defs, we can't do a simple analysis, so just
    // go with what the kill flag says.
    if (llvm::next(Begin) != MRI->def_end())
      return true;
    DefMI = &*Begin;
    bool IsSrcPhys, IsDstPhys;
    unsigned SrcReg,  DstReg;
    // If the def is something other than a copy, then it isn't going to
    // be coalesced, so follow the kill flag.
    if (!isCopyToReg(*DefMI, TII, SrcReg, DstReg, IsSrcPhys, IsDstPhys))
      return true;
    Reg = SrcReg;
  }
}

/// isTwoAddrUse - Return true if the specified MI uses the specified register
/// as a two-address use. If so, return the destination register by reference.
static bool isTwoAddrUse(MachineInstr &MI, unsigned Reg, unsigned &DstReg) {
  const MCInstrDesc &MCID = MI.getDesc();
  unsigned NumOps = MI.isInlineAsm()
    ? MI.getNumOperands() : MCID.getNumOperands();
  for (unsigned i = 0; i != NumOps; ++i) {
    const MachineOperand &MO = MI.getOperand(i);
    if (!MO.isReg() || !MO.isUse() || MO.getReg() != Reg)
      continue;
    unsigned ti;
    if (MI.isRegTiedToDefOperand(i, &ti)) {
      DstReg = MI.getOperand(ti).getReg();
      return true;
    }
  }
  return false;
}

/// findOnlyInterestingUse - Given a register, if has a single in-basic block
/// use, return the use instruction if it's a copy or a two-address use.
static
MachineInstr *findOnlyInterestingUse(unsigned Reg, MachineBasicBlock *MBB,
                                     MachineRegisterInfo *MRI,
                                     const TargetInstrInfo *TII,
                                     bool &IsCopy,
                                     unsigned &DstReg, bool &IsDstPhys) {
  if (!MRI->hasOneNonDBGUse(Reg))
    // None or more than one use.
    return 0;
  MachineInstr &UseMI = *MRI->use_nodbg_begin(Reg);
  if (UseMI.getParent() != MBB)
    return 0;
  unsigned SrcReg;
  bool IsSrcPhys;
  if (isCopyToReg(UseMI, TII, SrcReg, DstReg, IsSrcPhys, IsDstPhys)) {
    IsCopy = true;
    return &UseMI;
  }
  IsDstPhys = false;
  if (isTwoAddrUse(UseMI, Reg, DstReg)) {
    IsDstPhys = TargetRegisterInfo::isPhysicalRegister(DstReg);
    return &UseMI;
  }
  return 0;
}

/// getMappedReg - Return the physical register the specified virtual register
/// might be mapped to.
static unsigned
getMappedReg(unsigned Reg, DenseMap<unsigned, unsigned> &RegMap) {
  while (TargetRegisterInfo::isVirtualRegister(Reg))  {
    DenseMap<unsigned, unsigned>::iterator SI = RegMap.find(Reg);
    if (SI == RegMap.end())
      return 0;
    Reg = SI->second;
  }
  if (TargetRegisterInfo::isPhysicalRegister(Reg))
    return Reg;
  return 0;
}

/// regsAreCompatible - Return true if the two registers are equal or aliased.
///
static bool
regsAreCompatible(unsigned RegA, unsigned RegB, const TargetRegisterInfo *TRI) {
  if (RegA == RegB)
    return true;
  if (!RegA || !RegB)
    return false;
  return TRI->regsOverlap(RegA, RegB);
}


/// isProfitableToCommute - Return true if it's potentially profitable to commute
/// the two-address instruction that's being processed.
bool
TwoAddressInstructionPass::isProfitableToCommute(unsigned regA, unsigned regB,
                                       unsigned regC,
                                       MachineInstr *MI, MachineBasicBlock *MBB,
                                       unsigned Dist) {
  if (OptLevel == CodeGenOpt::None)
    return false;

  // Determine if it's profitable to commute this two address instruction. In
  // general, we want no uses between this instruction and the definition of
  // the two-address register.
  // e.g.
  // %reg1028<def> = EXTRACT_SUBREG %reg1027<kill>, 1
  // %reg1029<def> = MOV8rr %reg1028
  // %reg1029<def> = SHR8ri %reg1029, 7, %EFLAGS<imp-def,dead>
  // insert => %reg1030<def> = MOV8rr %reg1028
  // %reg1030<def> = ADD8rr %reg1028<kill>, %reg1029<kill>, %EFLAGS<imp-def,dead>
  // In this case, it might not be possible to coalesce the second MOV8rr
  // instruction if the first one is coalesced. So it would be profitable to
  // commute it:
  // %reg1028<def> = EXTRACT_SUBREG %reg1027<kill>, 1
  // %reg1029<def> = MOV8rr %reg1028
  // %reg1029<def> = SHR8ri %reg1029, 7, %EFLAGS<imp-def,dead>
  // insert => %reg1030<def> = MOV8rr %reg1029
  // %reg1030<def> = ADD8rr %reg1029<kill>, %reg1028<kill>, %EFLAGS<imp-def,dead>

  if (!MI->killsRegister(regC))
    return false;

  // Ok, we have something like:
  // %reg1030<def> = ADD8rr %reg1028<kill>, %reg1029<kill>, %EFLAGS<imp-def,dead>
  // let's see if it's worth commuting it.

  // Look for situations like this:
  // %reg1024<def> = MOV r1
  // %reg1025<def> = MOV r0
  // %reg1026<def> = ADD %reg1024, %reg1025
  // r0            = MOV %reg1026
  // Commute the ADD to hopefully eliminate an otherwise unavoidable copy.
  unsigned ToRegA = getMappedReg(regA, DstRegMap);
  if (ToRegA) {
    unsigned FromRegB = getMappedReg(regB, SrcRegMap);
    unsigned FromRegC = getMappedReg(regC, SrcRegMap);
    bool BComp = !FromRegB || regsAreCompatible(FromRegB, ToRegA, TRI);
    bool CComp = !FromRegC || regsAreCompatible(FromRegC, ToRegA, TRI);
    if (BComp != CComp)
      return !BComp && CComp;
  }

  // If there is a use of regC between its last def (could be livein) and this
  // instruction, then bail.
  unsigned LastDefC = 0;
  if (!noUseAfterLastDef(regC, MBB, Dist, LastDefC))
    return false;

  // If there is a use of regB between its last def (could be livein) and this
  // instruction, then go ahead and make this transformation.
  unsigned LastDefB = 0;
  if (!noUseAfterLastDef(regB, MBB, Dist, LastDefB))
    return true;

  // Since there are no intervening uses for both registers, then commute
  // if the def of regC is closer. Its live interval is shorter.
  return LastDefB && LastDefC && LastDefC > LastDefB;
}

/// commuteInstruction - Commute a two-address instruction and update the basic
/// block, distance map, and live variables if needed. Return true if it is
/// successful.
bool
TwoAddressInstructionPass::commuteInstruction(MachineBasicBlock::iterator &mi,
                               MachineFunction::iterator &mbbi,
                               unsigned RegB, unsigned RegC, unsigned Dist) {
  MachineInstr *MI = mi;
  DEBUG(dbgs() << "2addr: COMMUTING  : " << *MI);
  MachineInstr *NewMI = TII->commuteInstruction(MI);

  if (NewMI == 0) {
    DEBUG(dbgs() << "2addr: COMMUTING FAILED!\n");
    return false;
  }

  DEBUG(dbgs() << "2addr: COMMUTED TO: " << *NewMI);
  // If the instruction changed to commute it, update livevar.
  if (NewMI != MI) {
    if (LV)
      // Update live variables
      LV->replaceKillInstruction(RegC, MI, NewMI);
    if (Indexes)
      Indexes->replaceMachineInstrInMaps(MI, NewMI);

    mbbi->insert(mi, NewMI);           // Insert the new inst
    mbbi->erase(mi);                   // Nuke the old inst.
    mi = NewMI;
    DistanceMap.insert(std::make_pair(NewMI, Dist));
  }

  // Update source register map.
  unsigned FromRegC = getMappedReg(RegC, SrcRegMap);
  if (FromRegC) {
    unsigned RegA = MI->getOperand(0).getReg();
    SrcRegMap[RegA] = FromRegC;
  }

  return true;
}

/// isProfitableToConv3Addr - Return true if it is profitable to convert the
/// given 2-address instruction to a 3-address one.
bool
TwoAddressInstructionPass::isProfitableToConv3Addr(unsigned RegA,unsigned RegB){
  // Look for situations like this:
  // %reg1024<def> = MOV r1
  // %reg1025<def> = MOV r0
  // %reg1026<def> = ADD %reg1024, %reg1025
  // r2            = MOV %reg1026
  // Turn ADD into a 3-address instruction to avoid a copy.
  unsigned FromRegB = getMappedReg(RegB, SrcRegMap);
  if (!FromRegB)
    return false;
  unsigned ToRegA = getMappedReg(RegA, DstRegMap);
  return (ToRegA && !regsAreCompatible(FromRegB, ToRegA, TRI));
}

/// convertInstTo3Addr - Convert the specified two-address instruction into a
/// three address one. Return true if this transformation was successful.
bool
TwoAddressInstructionPass::convertInstTo3Addr(MachineBasicBlock::iterator &mi,
                                              MachineBasicBlock::iterator &nmi,
                                              MachineFunction::iterator &mbbi,
                                              unsigned RegA, unsigned RegB,
                                              unsigned Dist) {
  MachineInstr *NewMI = TII->convertToThreeAddress(mbbi, mi, LV);
  if (NewMI) {
    DEBUG(dbgs() << "2addr: CONVERTING 2-ADDR: " << *mi);
    DEBUG(dbgs() << "2addr:         TO 3-ADDR: " << *NewMI);
    bool Sunk = false;

    if (Indexes)
      Indexes->replaceMachineInstrInMaps(mi, NewMI);

    if (NewMI->findRegisterUseOperand(RegB, false, TRI))
      // FIXME: Temporary workaround. If the new instruction doesn't
      // uses RegB, convertToThreeAddress must have created more
      // then one instruction.
      Sunk = sink3AddrInstruction(mbbi, NewMI, RegB, mi);

    mbbi->erase(mi); // Nuke the old inst.

    if (!Sunk) {
      DistanceMap.insert(std::make_pair(NewMI, Dist));
      mi = NewMI;
      nmi = llvm::next(mi);
    }

    // Update source and destination register maps.
    SrcRegMap.erase(RegA);
    DstRegMap.erase(RegB);
    return true;
  }

  return false;
}

/// scanUses - Scan forward recursively for only uses, update maps if the use
/// is a copy or a two-address instruction.
void
TwoAddressInstructionPass::scanUses(unsigned DstReg, MachineBasicBlock *MBB,
                                    SmallPtrSet<MachineInstr*, 8> &Processed) {
  SmallVector<unsigned, 4> VirtRegPairs;
  bool IsDstPhys;
  bool IsCopy = false;
  unsigned NewReg = 0;
  unsigned Reg = DstReg;
  while (MachineInstr *UseMI = findOnlyInterestingUse(Reg, MBB, MRI, TII,IsCopy,
                                                      NewReg, IsDstPhys)) {
    if (IsCopy && !Processed.insert(UseMI))
      break;

    DenseMap<MachineInstr*, unsigned>::iterator DI = DistanceMap.find(UseMI);
    if (DI != DistanceMap.end())
      // Earlier in the same MBB.Reached via a back edge.
      break;

    if (IsDstPhys) {
      VirtRegPairs.push_back(NewReg);
      break;
    }
    bool isNew = SrcRegMap.insert(std::make_pair(NewReg, Reg)).second;
    if (!isNew)
      assert(SrcRegMap[NewReg] == Reg && "Can't map to two src registers!");
    VirtRegPairs.push_back(NewReg);
    Reg = NewReg;
  }

  if (!VirtRegPairs.empty()) {
    unsigned ToReg = VirtRegPairs.back();
    VirtRegPairs.pop_back();
    while (!VirtRegPairs.empty()) {
      unsigned FromReg = VirtRegPairs.back();
      VirtRegPairs.pop_back();
      bool isNew = DstRegMap.insert(std::make_pair(FromReg, ToReg)).second;
      if (!isNew)
        assert(DstRegMap[FromReg] == ToReg &&"Can't map to two dst registers!");
      ToReg = FromReg;
    }
    bool isNew = DstRegMap.insert(std::make_pair(DstReg, ToReg)).second;
    if (!isNew)
      assert(DstRegMap[DstReg] == ToReg && "Can't map to two dst registers!");
  }
}

/// processCopy - If the specified instruction is not yet processed, process it
/// if it's a copy. For a copy instruction, we find the physical registers the
/// source and destination registers might be mapped to. These are kept in
/// point-to maps used to determine future optimizations. e.g.
/// v1024 = mov r0
/// v1025 = mov r1
/// v1026 = add v1024, v1025
/// r1    = mov r1026
/// If 'add' is a two-address instruction, v1024, v1026 are both potentially
/// coalesced to r0 (from the input side). v1025 is mapped to r1. v1026 is
/// potentially joined with r1 on the output side. It's worthwhile to commute
/// 'add' to eliminate a copy.
void TwoAddressInstructionPass::processCopy(MachineInstr *MI,
                                     MachineBasicBlock *MBB,
                                     SmallPtrSet<MachineInstr*, 8> &Processed) {
  if (Processed.count(MI))
    return;

  bool IsSrcPhys, IsDstPhys;
  unsigned SrcReg, DstReg;
  if (!isCopyToReg(*MI, TII, SrcReg, DstReg, IsSrcPhys, IsDstPhys))
    return;

  if (IsDstPhys && !IsSrcPhys)
    DstRegMap.insert(std::make_pair(SrcReg, DstReg));
  else if (!IsDstPhys && IsSrcPhys) {
    bool isNew = SrcRegMap.insert(std::make_pair(DstReg, SrcReg)).second;
    if (!isNew)
      assert(SrcRegMap[DstReg] == SrcReg &&
             "Can't map to two src physical registers!");

    scanUses(DstReg, MBB, Processed);
  }

  Processed.insert(MI);
  return;
}

/// rescheduleMIBelowKill - If there is one more local instruction that reads
/// 'Reg' and it kills 'Reg, consider moving the instruction below the kill
/// instruction in order to eliminate the need for the copy.
bool TwoAddressInstructionPass::
rescheduleMIBelowKill(MachineBasicBlock *MBB,
                      MachineBasicBlock::iterator &mi,
                      MachineBasicBlock::iterator &nmi,
                      unsigned Reg) {
  // Bail immediately if we don't have LV available. We use it to find kills
  // efficiently.
  if (!LV)
    return false;

  MachineInstr *MI = &*mi;
  DenseMap<MachineInstr*, unsigned>::iterator DI = DistanceMap.find(MI);
  if (DI == DistanceMap.end())
    // Must be created from unfolded load. Don't waste time trying this.
    return false;

  MachineInstr *KillMI = LV->getVarInfo(Reg).findKill(MBB);
  if (!KillMI || MI == KillMI || KillMI->isCopy() || KillMI->isCopyLike())
    // Don't mess with copies, they may be coalesced later.
    return false;

  if (KillMI->hasUnmodeledSideEffects() || KillMI->isCall() ||
      KillMI->isBranch() || KillMI->isTerminator())
    // Don't move pass calls, etc.
    return false;

  unsigned DstReg;
  if (isTwoAddrUse(*KillMI, Reg, DstReg))
    return false;

  bool SeenStore = true;
  if (!MI->isSafeToMove(TII, AA, SeenStore))
    return false;

  if (TII->getInstrLatency(InstrItins, MI) > 1)
    // FIXME: Needs more sophisticated heuristics.
    return false;

  SmallSet<unsigned, 2> Uses;
  SmallSet<unsigned, 2> Kills;
  SmallSet<unsigned, 2> Defs;
  for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = MI->getOperand(i);
    if (!MO.isReg())
      continue;
    unsigned MOReg = MO.getReg();
    if (!MOReg)
      continue;
    if (MO.isDef())
      Defs.insert(MOReg);
    else {
      Uses.insert(MOReg);
      if (MO.isKill() && MOReg != Reg)
        Kills.insert(MOReg);
    }
  }

  // Move the copies connected to MI down as well.
  MachineBasicBlock::iterator From = MI;
  MachineBasicBlock::iterator To = llvm::next(From);
  while (To->isCopy() && Defs.count(To->getOperand(1).getReg())) {
    Defs.insert(To->getOperand(0).getReg());
    ++To;
  }

  // Check if the reschedule will not break depedencies.
  unsigned NumVisited = 0;
  MachineBasicBlock::iterator KillPos = KillMI;
  ++KillPos;
  for (MachineBasicBlock::iterator I = To; I != KillPos; ++I) {
    MachineInstr *OtherMI = I;
    // DBG_VALUE cannot be counted against the limit.
    if (OtherMI->isDebugValue())
      continue;
    if (NumVisited > 10)  // FIXME: Arbitrary limit to reduce compile time cost.
      return false;
    ++NumVisited;
    if (OtherMI->hasUnmodeledSideEffects() || OtherMI->isCall() ||
        OtherMI->isBranch() || OtherMI->isTerminator())
      // Don't move pass calls, etc.
      return false;
    for (unsigned i = 0, e = OtherMI->getNumOperands(); i != e; ++i) {
      const MachineOperand &MO = OtherMI->getOperand(i);
      if (!MO.isReg())
        continue;
      unsigned MOReg = MO.getReg();
      if (!MOReg)
        continue;
      if (MO.isDef()) {
        if (Uses.count(MOReg))
          // Physical register use would be clobbered.
          return false;
        if (!MO.isDead() && Defs.count(MOReg))
          // May clobber a physical register def.
          // FIXME: This may be too conservative. It's ok if the instruction
          // is sunken completely below the use.
          return false;
      } else {
        if (Defs.count(MOReg))
          return false;
        if (MOReg != Reg &&
            ((MO.isKill() && Uses.count(MOReg)) || Kills.count(MOReg)))
          // Don't want to extend other live ranges and update kills.
          return false;
        if (MOReg == Reg && !MO.isKill())
          // We can't schedule across a use of the register in question.
          return false;
        // Ensure that if this is register in question, its the kill we expect.
        assert((MOReg != Reg || OtherMI == KillMI) &&
               "Found multiple kills of a register in a basic block");
      }
    }
  }

  // Move debug info as well.
  while (From != MBB->begin() && llvm::prior(From)->isDebugValue())
    --From;

  // Copies following MI may have been moved as well.
  nmi = To;
  MBB->splice(KillPos, MBB, From, To);
  DistanceMap.erase(DI);

  // Update live variables
  LV->removeVirtualRegisterKilled(Reg, KillMI);
  LV->addVirtualRegisterKilled(Reg, MI);
  if (LIS)
    LIS->handleMove(MI);

  DEBUG(dbgs() << "\trescheduled below kill: " << *KillMI);
  return true;
}

/// isDefTooClose - Return true if the re-scheduling will put the given
/// instruction too close to the defs of its register dependencies.
bool TwoAddressInstructionPass::isDefTooClose(unsigned Reg, unsigned Dist,
                                              MachineInstr *MI,
                                              MachineBasicBlock *MBB) {
  for (MachineRegisterInfo::def_iterator DI = MRI->def_begin(Reg),
         DE = MRI->def_end(); DI != DE; ++DI) {
    MachineInstr *DefMI = &*DI;
    if (DefMI->getParent() != MBB || DefMI->isCopy() || DefMI->isCopyLike())
      continue;
    if (DefMI == MI)
      return true; // MI is defining something KillMI uses
    DenseMap<MachineInstr*, unsigned>::iterator DDI = DistanceMap.find(DefMI);
    if (DDI == DistanceMap.end())
      return true;  // Below MI
    unsigned DefDist = DDI->second;
    assert(Dist > DefDist && "Visited def already?");
    if (TII->getInstrLatency(InstrItins, DefMI) > (Dist - DefDist))
      return true;
  }
  return false;
}

/// rescheduleKillAboveMI - If there is one more local instruction that reads
/// 'Reg' and it kills 'Reg, consider moving the kill instruction above the
/// current two-address instruction in order to eliminate the need for the
/// copy.
bool TwoAddressInstructionPass::
rescheduleKillAboveMI(MachineBasicBlock *MBB,
                      MachineBasicBlock::iterator &mi,
                      MachineBasicBlock::iterator &nmi,
                      unsigned Reg) {
  // Bail immediately if we don't have LV available. We use it to find kills
  // efficiently.
  if (!LV)
    return false;

  MachineInstr *MI = &*mi;
  DenseMap<MachineInstr*, unsigned>::iterator DI = DistanceMap.find(MI);
  if (DI == DistanceMap.end())
    // Must be created from unfolded load. Don't waste time trying this.
    return false;

  MachineInstr *KillMI = LV->getVarInfo(Reg).findKill(MBB);
  if (!KillMI || MI == KillMI || KillMI->isCopy() || KillMI->isCopyLike())
    // Don't mess with copies, they may be coalesced later.
    return false;

  unsigned DstReg;
  if (isTwoAddrUse(*KillMI, Reg, DstReg))
    return false;

  bool SeenStore = true;
  if (!KillMI->isSafeToMove(TII, AA, SeenStore))
    return false;

  SmallSet<unsigned, 2> Uses;
  SmallSet<unsigned, 2> Kills;
  SmallSet<unsigned, 2> Defs;
  SmallSet<unsigned, 2> LiveDefs;
  for (unsigned i = 0, e = KillMI->getNumOperands(); i != e; ++i) {
    const MachineOperand &MO = KillMI->getOperand(i);
    if (!MO.isReg())
      continue;
    unsigned MOReg = MO.getReg();
    if (MO.isUse()) {
      if (!MOReg)
        continue;
      if (isDefTooClose(MOReg, DI->second, MI, MBB))
        return false;
      if (MOReg == Reg && !MO.isKill())
        return false;
      Uses.insert(MOReg);
      if (MO.isKill() && MOReg != Reg)
        Kills.insert(MOReg);
    } else if (TargetRegisterInfo::isPhysicalRegister(MOReg)) {
      Defs.insert(MOReg);
      if (!MO.isDead())
        LiveDefs.insert(MOReg);
    }
  }

  // Check if the reschedule will not break depedencies.
  unsigned NumVisited = 0;
  MachineBasicBlock::iterator KillPos = KillMI;
  for (MachineBasicBlock::iterator I = mi; I != KillPos; ++I) {
    MachineInstr *OtherMI = I;
    // DBG_VALUE cannot be counted against the limit.
    if (OtherMI->isDebugValue())
      continue;
    if (NumVisited > 10)  // FIXME: Arbitrary limit to reduce compile time cost.
      return false;
    ++NumVisited;
    if (OtherMI->hasUnmodeledSideEffects() || OtherMI->isCall() ||
        OtherMI->isBranch() || OtherMI->isTerminator())
      // Don't move pass calls, etc.
      return false;
    SmallVector<unsigned, 2> OtherDefs;
    for (unsigned i = 0, e = OtherMI->getNumOperands(); i != e; ++i) {
      const MachineOperand &MO = OtherMI->getOperand(i);
      if (!MO.isReg())
        continue;
      unsigned MOReg = MO.getReg();
      if (!MOReg)
        continue;
      if (MO.isUse()) {
        if (Defs.count(MOReg))
          // Moving KillMI can clobber the physical register if the def has
          // not been seen.
          return false;
        if (Kills.count(MOReg))
          // Don't want to extend other live ranges and update kills.
          return false;
        if (OtherMI != MI && MOReg == Reg && !MO.isKill())
          // We can't schedule across a use of the register in question.
          return false;
      } else {
        OtherDefs.push_back(MOReg);
      }
    }

    for (unsigned i = 0, e = OtherDefs.size(); i != e; ++i) {
      unsigned MOReg = OtherDefs[i];
      if (Uses.count(MOReg))
        return false;
      if (TargetRegisterInfo::isPhysicalRegister(MOReg) &&
          LiveDefs.count(MOReg))
        return false;
      // Physical register def is seen.
      Defs.erase(MOReg);
    }
  }

  // Move the old kill above MI, don't forget to move debug info as well.
  MachineBasicBlock::iterator InsertPos = mi;
  while (InsertPos != MBB->begin() && llvm::prior(InsertPos)->isDebugValue())
    --InsertPos;
  MachineBasicBlock::iterator From = KillMI;
  MachineBasicBlock::iterator To = llvm::next(From);
  while (llvm::prior(From)->isDebugValue())
    --From;
  MBB->splice(InsertPos, MBB, From, To);

  nmi = llvm::prior(InsertPos); // Backtrack so we process the moved instr.
  DistanceMap.erase(DI);

  // Update live variables
  LV->removeVirtualRegisterKilled(Reg, KillMI);
  LV->addVirtualRegisterKilled(Reg, MI);
  if (LIS)
    LIS->handleMove(KillMI);

  DEBUG(dbgs() << "\trescheduled kill: " << *KillMI);
  return true;
}

/// tryInstructionTransform - For the case where an instruction has a single
/// pair of tied register operands, attempt some transformations that may
/// either eliminate the tied operands or improve the opportunities for
/// coalescing away the register copy.  Returns true if no copy needs to be
/// inserted to untie mi's operands (either because they were untied, or
/// because mi was rescheduled, and will be visited again later).
bool TwoAddressInstructionPass::
tryInstructionTransform(MachineBasicBlock::iterator &mi,
                        MachineBasicBlock::iterator &nmi,
                        MachineFunction::iterator &mbbi,
                        unsigned SrcIdx, unsigned DstIdx, unsigned Dist,
                        SmallPtrSet<MachineInstr*, 8> &Processed) {
  if (OptLevel == CodeGenOpt::None)
    return false;

  MachineInstr &MI = *mi;
  unsigned regA = MI.getOperand(DstIdx).getReg();
  unsigned regB = MI.getOperand(SrcIdx).getReg();

  assert(TargetRegisterInfo::isVirtualRegister(regB) &&
         "cannot make instruction into two-address form");
  bool regBKilled = isKilled(MI, regB, MRI, TII);

  if (TargetRegisterInfo::isVirtualRegister(regA))
    scanUses(regA, &*mbbi, Processed);

  // Check if it is profitable to commute the operands.
  unsigned SrcOp1, SrcOp2;
  unsigned regC = 0;
  unsigned regCIdx = ~0U;
  bool TryCommute = false;
  bool AggressiveCommute = false;
  if (MI.isCommutable() && MI.getNumOperands() >= 3 &&
      TII->findCommutedOpIndices(&MI, SrcOp1, SrcOp2)) {
    if (SrcIdx == SrcOp1)
      regCIdx = SrcOp2;
    else if (SrcIdx == SrcOp2)
      regCIdx = SrcOp1;

    if (regCIdx != ~0U) {
      regC = MI.getOperand(regCIdx).getReg();
      if (!regBKilled && isKilled(MI, regC, MRI, TII))
        // If C dies but B does not, swap the B and C operands.
        // This makes the live ranges of A and C joinable.
        TryCommute = true;
      else if (isProfitableToCommute(regA, regB, regC, &MI, mbbi, Dist)) {
        TryCommute = true;
        AggressiveCommute = true;
      }
    }
  }

  // If it's profitable to commute, try to do so.
  if (TryCommute && commuteInstruction(mi, mbbi, regB, regC, Dist)) {
    ++NumCommuted;
    if (AggressiveCommute)
      ++NumAggrCommuted;
    return false;
  }

  // If there is one more use of regB later in the same MBB, consider
  // re-schedule this MI below it.
  if (rescheduleMIBelowKill(mbbi, mi, nmi, regB)) {
    ++NumReSchedDowns;
    return true;
  }

  if (MI.isConvertibleTo3Addr()) {
    // This instruction is potentially convertible to a true
    // three-address instruction.  Check if it is profitable.
    if (!regBKilled || isProfitableToConv3Addr(regA, regB)) {
      // Try to convert it.
      if (convertInstTo3Addr(mi, nmi, mbbi, regA, regB, Dist)) {
        ++NumConvertedTo3Addr;
        return true; // Done with this instruction.
      }
    }
  }

  // If there is one more use of regB later in the same MBB, consider
  // re-schedule it before this MI if it's legal.
  if (rescheduleKillAboveMI(mbbi, mi, nmi, regB)) {
    ++NumReSchedUps;
    return true;
  }

  // If this is an instruction with a load folded into it, try unfolding
  // the load, e.g. avoid this:
  //   movq %rdx, %rcx
  //   addq (%rax), %rcx
  // in favor of this:
  //   movq (%rax), %rcx
  //   addq %rdx, %rcx
  // because it's preferable to schedule a load than a register copy.
  if (MI.mayLoad() && !regBKilled) {
    // Determine if a load can be unfolded.
    unsigned LoadRegIndex;
    unsigned NewOpc =
      TII->getOpcodeAfterMemoryUnfold(MI.getOpcode(),
                                      /*UnfoldLoad=*/true,
                                      /*UnfoldStore=*/false,
                                      &LoadRegIndex);
    if (NewOpc != 0) {
      const MCInstrDesc &UnfoldMCID = TII->get(NewOpc);
      if (UnfoldMCID.getNumDefs() == 1) {
        // Unfold the load.
        DEBUG(dbgs() << "2addr:   UNFOLDING: " << MI);
        const TargetRegisterClass *RC =
          TRI->getAllocatableClass(
            TII->getRegClass(UnfoldMCID, LoadRegIndex, TRI, *MF));
        unsigned Reg = MRI->createVirtualRegister(RC);
        SmallVector<MachineInstr *, 2> NewMIs;
        if (!TII->unfoldMemoryOperand(*MF, &MI, Reg,
                                      /*UnfoldLoad=*/true,/*UnfoldStore=*/false,
                                      NewMIs)) {
          DEBUG(dbgs() << "2addr: ABANDONING UNFOLD\n");
          return false;
        }
        assert(NewMIs.size() == 2 &&
               "Unfolded a load into multiple instructions!");
        // The load was previously folded, so this is the only use.
        NewMIs[1]->addRegisterKilled(Reg, TRI);

        // Tentatively insert the instructions into the block so that they
        // look "normal" to the transformation logic.
        mbbi->insert(mi, NewMIs[0]);
        mbbi->insert(mi, NewMIs[1]);

        DEBUG(dbgs() << "2addr:    NEW LOAD: " << *NewMIs[0]
                     << "2addr:    NEW INST: " << *NewMIs[1]);

        // Transform the instruction, now that it no longer has a load.
        unsigned NewDstIdx = NewMIs[1]->findRegisterDefOperandIdx(regA);
        unsigned NewSrcIdx = NewMIs[1]->findRegisterUseOperandIdx(regB);
        MachineBasicBlock::iterator NewMI = NewMIs[1];
        bool TransformSuccess =
          tryInstructionTransform(NewMI, mi, mbbi,
                                  NewSrcIdx, NewDstIdx, Dist, Processed);
        if (TransformSuccess ||
            NewMIs[1]->getOperand(NewSrcIdx).isKill()) {
          // Success, or at least we made an improvement. Keep the unfolded
          // instructions and discard the original.
          if (LV) {
            for (unsigned i = 0, e = MI.getNumOperands(); i != e; ++i) {
              MachineOperand &MO = MI.getOperand(i);
              if (MO.isReg() &&
                  TargetRegisterInfo::isVirtualRegister(MO.getReg())) {
                if (MO.isUse()) {
                  if (MO.isKill()) {
                    if (NewMIs[0]->killsRegister(MO.getReg()))
                      LV->replaceKillInstruction(MO.getReg(), &MI, NewMIs[0]);
                    else {
                      assert(NewMIs[1]->killsRegister(MO.getReg()) &&
                             "Kill missing after load unfold!");
                      LV->replaceKillInstruction(MO.getReg(), &MI, NewMIs[1]);
                    }
                  }
                } else if (LV->removeVirtualRegisterDead(MO.getReg(), &MI)) {
                  if (NewMIs[1]->registerDefIsDead(MO.getReg()))
                    LV->addVirtualRegisterDead(MO.getReg(), NewMIs[1]);
                  else {
                    assert(NewMIs[0]->registerDefIsDead(MO.getReg()) &&
                           "Dead flag missing after load unfold!");
                    LV->addVirtualRegisterDead(MO.getReg(), NewMIs[0]);
                  }
                }
              }
            }
            LV->addVirtualRegisterKilled(Reg, NewMIs[1]);
          }
          MI.eraseFromParent();
          mi = NewMIs[1];
          if (TransformSuccess)
            return true;
        } else {
          // Transforming didn't eliminate the tie and didn't lead to an
          // improvement. Clean up the unfolded instructions and keep the
          // original.
          DEBUG(dbgs() << "2addr: ABANDONING UNFOLD\n");
          NewMIs[0]->eraseFromParent();
          NewMIs[1]->eraseFromParent();
        }
      }
    }
  }

  return false;
}

// Collect tied operands of MI that need to be handled.
// Rewrite trivial cases immediately.
// Return true if any tied operands where found, including the trivial ones.
bool TwoAddressInstructionPass::
collectTiedOperands(MachineInstr *MI, TiedOperandMap &TiedOperands) {
  const MCInstrDesc &MCID = MI->getDesc();
  bool AnyOps = false;
  unsigned NumOps = MI->getNumOperands();

  for (unsigned SrcIdx = 0; SrcIdx < NumOps; ++SrcIdx) {
    unsigned DstIdx = 0;
    if (!MI->isRegTiedToDefOperand(SrcIdx, &DstIdx))
      continue;
    AnyOps = true;
    MachineOperand &SrcMO = MI->getOperand(SrcIdx);
    MachineOperand &DstMO = MI->getOperand(DstIdx);
    unsigned SrcReg = SrcMO.getReg();
    unsigned DstReg = DstMO.getReg();
    // Tied constraint already satisfied?
    if (SrcReg == DstReg)
      continue;

    assert(SrcReg && SrcMO.isUse() && "two address instruction invalid");

    // Deal with <undef> uses immediately - simply rewrite the src operand.
    if (SrcMO.isUndef()) {
      // Constrain the DstReg register class if required.
      if (TargetRegisterInfo::isVirtualRegister(DstReg))
        if (const TargetRegisterClass *RC = TII->getRegClass(MCID, SrcIdx,
                                                             TRI, *MF))
          MRI->constrainRegClass(DstReg, RC);
      SrcMO.setReg(DstReg);
      DEBUG(dbgs() << "\t\trewrite undef:\t" << *MI);
      continue;
    }
    TiedOperands[SrcReg].push_back(std::make_pair(SrcIdx, DstIdx));
  }
  return AnyOps;
}

// Process a list of tied MI operands that all use the same source register.
// The tied pairs are of the form (SrcIdx, DstIdx).
void
TwoAddressInstructionPass::processTiedPairs(MachineInstr *MI,
                                            TiedPairList &TiedPairs,
                                            unsigned &Dist) {
  bool IsEarlyClobber = false;
  bool RemovedKillFlag = false;
  bool AllUsesCopied = true;
  unsigned LastCopiedReg = 0;
  unsigned RegB = 0;
  for (unsigned tpi = 0, tpe = TiedPairs.size(); tpi != tpe; ++tpi) {
    unsigned SrcIdx = TiedPairs[tpi].first;
    unsigned DstIdx = TiedPairs[tpi].second;

    const MachineOperand &DstMO = MI->getOperand(DstIdx);
    unsigned RegA = DstMO.getReg();
    IsEarlyClobber |= DstMO.isEarlyClobber();

    // Grab RegB from the instruction because it may have changed if the
    // instruction was commuted.
    RegB = MI->getOperand(SrcIdx).getReg();

    if (RegA == RegB) {
      // The register is tied to multiple destinations (or else we would
      // not have continued this far), but this use of the register
      // already matches the tied destination.  Leave it.
      AllUsesCopied = false;
      continue;
    }
    LastCopiedReg = RegA;

    assert(TargetRegisterInfo::isVirtualRegister(RegB) &&
           "cannot make instruction into two-address form");

#ifndef NDEBUG
    // First, verify that we don't have a use of "a" in the instruction
    // (a = b + a for example) because our transformation will not
    // work. This should never occur because we are in SSA form.
    for (unsigned i = 0; i != MI->getNumOperands(); ++i)
      assert(i == DstIdx ||
             !MI->getOperand(i).isReg() ||
             MI->getOperand(i).getReg() != RegA);
#endif

    // Emit a copy.
    BuildMI(*MI->getParent(), MI, MI->getDebugLoc(),
            TII->get(TargetOpcode::COPY), RegA).addReg(RegB);

    // Update DistanceMap.
    MachineBasicBlock::iterator PrevMI = MI;
    --PrevMI;
    DistanceMap.insert(std::make_pair(PrevMI, Dist));
    DistanceMap[MI] = ++Dist;

    SlotIndex CopyIdx;
    if (Indexes)
      CopyIdx = Indexes->insertMachineInstrInMaps(PrevMI).getRegSlot();

    DEBUG(dbgs() << "\t\tprepend:\t" << *PrevMI);

    MachineOperand &MO = MI->getOperand(SrcIdx);
    assert(MO.isReg() && MO.getReg() == RegB && MO.isUse() &&
           "inconsistent operand info for 2-reg pass");
    if (MO.isKill()) {
      MO.setIsKill(false);
      RemovedKillFlag = true;
    }

    // Make sure regA is a legal regclass for the SrcIdx operand.
    if (TargetRegisterInfo::isVirtualRegister(RegA) &&
        TargetRegisterInfo::isVirtualRegister(RegB))
      MRI->constrainRegClass(RegA, MRI->getRegClass(RegB));

    MO.setReg(RegA);

    // Propagate SrcRegMap.
    SrcRegMap[RegA] = RegB;
  }


  if (AllUsesCopied) {
    if (!IsEarlyClobber) {
      // Replace other (un-tied) uses of regB with LastCopiedReg.
      for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
        MachineOperand &MO = MI->getOperand(i);
        if (MO.isReg() && MO.getReg() == RegB && MO.isUse()) {
          if (MO.isKill()) {
            MO.setIsKill(false);
            RemovedKillFlag = true;
          }
          MO.setReg(LastCopiedReg);
        }
      }
    }

    // Update live variables for regB.
    if (RemovedKillFlag && LV && LV->getVarInfo(RegB).removeKill(MI)) {
      MachineBasicBlock::iterator PrevMI = MI;
      --PrevMI;
      LV->addVirtualRegisterKilled(RegB, PrevMI);
    }

  } else if (RemovedKillFlag) {
    // Some tied uses of regB matched their destination registers, so
    // regB is still used in this instruction, but a kill flag was
    // removed from a different tied use of regB, so now we need to add
    // a kill flag to one of the remaining uses of regB.
    for (unsigned i = 0, e = MI->getNumOperands(); i != e; ++i) {
      MachineOperand &MO = MI->getOperand(i);
      if (MO.isReg() && MO.getReg() == RegB && MO.isUse()) {
        MO.setIsKill(true);
        break;
      }
    }
  }
}

/// runOnMachineFunction - Reduce two-address instructions to two operands.
///
bool TwoAddressInstructionPass::runOnMachineFunction(MachineFunction &Func) {
  MF = &Func;
  const TargetMachine &TM = MF->getTarget();
  MRI = &MF->getRegInfo();
  TII = TM.getInstrInfo();
  TRI = TM.getRegisterInfo();
  InstrItins = TM.getInstrItineraryData();
  Indexes = getAnalysisIfAvailable<SlotIndexes>();
  LV = getAnalysisIfAvailable<LiveVariables>();
  LIS = getAnalysisIfAvailable<LiveIntervals>();
  AA = &getAnalysis<AliasAnalysis>();
  OptLevel = TM.getOptLevel();

  bool MadeChange = false;

  DEBUG(dbgs() << "********** REWRITING TWO-ADDR INSTRS **********\n");
  DEBUG(dbgs() << "********** Function: "
        << MF->getName() << '\n');

  // This pass takes the function out of SSA form.
  MRI->leaveSSA();

  TiedOperandMap TiedOperands;

  SmallPtrSet<MachineInstr*, 8> Processed;
  for (MachineFunction::iterator mbbi = MF->begin(), mbbe = MF->end();
       mbbi != mbbe; ++mbbi) {
    unsigned Dist = 0;
    DistanceMap.clear();
    SrcRegMap.clear();
    DstRegMap.clear();
    Processed.clear();
    for (MachineBasicBlock::iterator mi = mbbi->begin(), me = mbbi->end();
         mi != me; ) {
      MachineBasicBlock::iterator nmi = llvm::next(mi);
      if (mi->isDebugValue()) {
        mi = nmi;
        continue;
      }

      // Remember REG_SEQUENCE instructions, we'll deal with them later.
      if (mi->isRegSequence())
        RegSequences.push_back(&*mi);

      DistanceMap.insert(std::make_pair(mi, ++Dist));

      processCopy(&*mi, &*mbbi, Processed);

      // First scan through all the tied register uses in this instruction
      // and record a list of pairs of tied operands for each register.
      if (!collectTiedOperands(mi, TiedOperands)) {
        mi = nmi;
        continue;
      }

      ++NumTwoAddressInstrs;
      MadeChange = true;
      DEBUG(dbgs() << '\t' << *mi);

      // If the instruction has a single pair of tied operands, try some
      // transformations that may either eliminate the tied operands or
      // improve the opportunities for coalescing away the register copy.
      if (TiedOperands.size() == 1) {
        SmallVector<std::pair<unsigned, unsigned>, 4> &TiedPairs
          = TiedOperands.begin()->second;
        if (TiedPairs.size() == 1) {
          unsigned SrcIdx = TiedPairs[0].first;
          unsigned DstIdx = TiedPairs[0].second;
          unsigned SrcReg = mi->getOperand(SrcIdx).getReg();
          unsigned DstReg = mi->getOperand(DstIdx).getReg();
          if (SrcReg != DstReg &&
              tryInstructionTransform(mi, nmi, mbbi, SrcIdx, DstIdx, Dist,
                                      Processed)) {
            // The tied operands have been eliminated or shifted further down the
            // block to ease elimination. Continue processing with 'nmi'.
            TiedOperands.clear();
            mi = nmi;
            continue;
          }
        }
      }

      // Now iterate over the information collected above.
      for (TiedOperandMap::iterator OI = TiedOperands.begin(),
             OE = TiedOperands.end(); OI != OE; ++OI) {
        processTiedPairs(mi, OI->second, Dist);
        DEBUG(dbgs() << "\t\trewrite to:\t" << *mi);
      }

      // Rewrite INSERT_SUBREG as COPY now that we no longer need SSA form.
      if (mi->isInsertSubreg()) {
        // From %reg = INSERT_SUBREG %reg, %subreg, subidx
        // To   %reg:subidx = COPY %subreg
        unsigned SubIdx = mi->getOperand(3).getImm();
        mi->RemoveOperand(3);
        assert(mi->getOperand(0).getSubReg() == 0 && "Unexpected subreg idx");
        mi->getOperand(0).setSubReg(SubIdx);
        mi->getOperand(0).setIsUndef(mi->getOperand(1).isUndef());
        mi->RemoveOperand(1);
        mi->setDesc(TII->get(TargetOpcode::COPY));
        DEBUG(dbgs() << "\t\tconvert to:\t" << *mi);
      }

      // Clear TiedOperands here instead of at the top of the loop
      // since most instructions do not have tied operands.
      TiedOperands.clear();
      mi = nmi;
    }
  }

  // Eliminate REG_SEQUENCE instructions. Their whole purpose was to preseve
  // SSA form. It's now safe to de-SSA.
  MadeChange |= eliminateRegSequences();

  return MadeChange;
}

static void UpdateRegSequenceSrcs(unsigned SrcReg,
                                  unsigned DstReg, unsigned SubIdx,
                                  MachineRegisterInfo *MRI,
                                  const TargetRegisterInfo &TRI) {
  for (MachineRegisterInfo::reg_iterator RI = MRI->reg_begin(SrcReg),
         RE = MRI->reg_end(); RI != RE; ) {
    MachineOperand &MO = RI.getOperand();
    ++RI;
    MO.substVirtReg(DstReg, SubIdx, TRI);
  }
}

// Find the first def of Reg, assuming they are all in the same basic block.
static MachineInstr *findFirstDef(unsigned Reg, MachineRegisterInfo *MRI) {
  SmallPtrSet<MachineInstr*, 8> Defs;
  MachineInstr *First = 0;
  for (MachineRegisterInfo::def_iterator RI = MRI->def_begin(Reg);
       MachineInstr *MI = RI.skipInstruction(); Defs.insert(MI))
    First = MI;
  if (!First)
    return 0;

  MachineBasicBlock *MBB = First->getParent();
  MachineBasicBlock::iterator A = First, B = First;
  bool Moving;
  do {
    Moving = false;
    if (A != MBB->begin()) {
      Moving = true;
      --A;
      if (Defs.erase(A)) First = A;
    }
    if (B != MBB->end()) {
      Defs.erase(B);
      ++B;
      Moving = true;
    }
  } while (Moving && !Defs.empty());
  assert(Defs.empty() && "Instructions outside basic block!");
  return First;
}

static bool HasOtherRegSequenceUses(unsigned Reg, MachineInstr *RegSeq,
                                    MachineRegisterInfo *MRI) {
  for (MachineRegisterInfo::use_iterator UI = MRI->use_begin(Reg),
         UE = MRI->use_end(); UI != UE; ++UI) {
    MachineInstr *UseMI = &*UI;
    if (UseMI != RegSeq && UseMI->isRegSequence())
      return true;
  }
  return false;
}

/// eliminateRegSequences - Eliminate REG_SEQUENCE instructions as part
/// of the de-ssa process. This replaces sources of REG_SEQUENCE as
/// sub-register references of the register defined by REG_SEQUENCE. e.g.
///
/// %reg1029<def>, %reg1030<def> = VLD1q16 %reg1024<kill>, ...
/// %reg1031<def> = REG_SEQUENCE %reg1029<kill>, 5, %reg1030<kill>, 6
/// =>
/// %reg1031:5<def>, %reg1031:6<def> = VLD1q16 %reg1024<kill>, ...
bool TwoAddressInstructionPass::eliminateRegSequences() {
  if (RegSequences.empty())
    return false;

  for (unsigned i = 0, e = RegSequences.size(); i != e; ++i) {
    MachineInstr *MI = RegSequences[i];
    unsigned DstReg = MI->getOperand(0).getReg();
    if (MI->getOperand(0).getSubReg() ||
        TargetRegisterInfo::isPhysicalRegister(DstReg) ||
        !(MI->getNumOperands() & 1)) {
      DEBUG(dbgs() << "Illegal REG_SEQUENCE instruction:" << *MI);
      llvm_unreachable(0);
    }

    bool IsImpDef = true;
    SmallVector<unsigned, 4> RealSrcs;
    SmallSet<unsigned, 4> Seen;
    for (unsigned i = 1, e = MI->getNumOperands(); i < e; i += 2) {
      // Nothing needs to be inserted for <undef> operands.
      if (MI->getOperand(i).isUndef()) {
        MI->getOperand(i).setReg(0);
        continue;
      }
      unsigned SrcReg = MI->getOperand(i).getReg();
      unsigned SrcSubIdx = MI->getOperand(i).getSubReg();
      unsigned SubIdx = MI->getOperand(i+1).getImm();
      // DefMI of NULL means the value does not have a vreg in this block
      // i.e., its a physical register or a subreg.
      // In either case we force a copy to be generated.
      MachineInstr *DefMI = NULL;
      if (!MI->getOperand(i).getSubReg() &&
          !TargetRegisterInfo::isPhysicalRegister(SrcReg)) {
        DefMI = MRI->getUniqueVRegDef(SrcReg);
      }

      if (DefMI && DefMI->isImplicitDef()) {
        DefMI->eraseFromParent();
        continue;
      }
      IsImpDef = false;

      // Remember COPY sources. These might be candidate for coalescing.
      if (DefMI && DefMI->isCopy() && DefMI->getOperand(1).getSubReg())
        RealSrcs.push_back(DefMI->getOperand(1).getReg());

      bool isKill = MI->getOperand(i).isKill();
      if (!DefMI || !Seen.insert(SrcReg) ||
          MI->getParent() != DefMI->getParent() ||
          !isKill || HasOtherRegSequenceUses(SrcReg, MI, MRI) ||
          !TRI->getMatchingSuperRegClass(MRI->getRegClass(DstReg),
                                         MRI->getRegClass(SrcReg), SubIdx)) {
        // REG_SEQUENCE cannot have duplicated operands, add a copy.
        // Also add an copy if the source is live-in the block. We don't want
        // to end up with a partial-redef of a livein, e.g.
        // BB0:
        // reg1051:10<def> =
        // ...
        // BB1:
        // ... = reg1051:10
        // BB2:
        // reg1051:9<def> =
        // LiveIntervalAnalysis won't like it.
        //
        // If the REG_SEQUENCE doesn't kill its source, keeping live variables
        // correctly up to date becomes very difficult. Insert a copy.

        // Defer any kill flag to the last operand using SrcReg. Otherwise, we
        // might insert a COPY that uses SrcReg after is was killed.
        if (isKill)
          for (unsigned j = i + 2; j < e; j += 2)
            if (MI->getOperand(j).getReg() == SrcReg) {
              MI->getOperand(j).setIsKill();
              isKill = false;
              break;
            }

        MachineBasicBlock::iterator InsertLoc = MI;
        MachineInstr *CopyMI = BuildMI(*MI->getParent(), InsertLoc,
                                MI->getDebugLoc(), TII->get(TargetOpcode::COPY))
            .addReg(DstReg, RegState::Define, SubIdx)
            .addReg(SrcReg, getKillRegState(isKill), SrcSubIdx);
        MI->getOperand(i).setReg(0);
        if (LV && isKill && !TargetRegisterInfo::isPhysicalRegister(SrcReg))
          LV->replaceKillInstruction(SrcReg, MI, CopyMI);
        DEBUG(dbgs() << "Inserted: " << *CopyMI);
      }
    }

    for (unsigned i = 1, e = MI->getNumOperands(); i < e; i += 2) {
      unsigned SrcReg = MI->getOperand(i).getReg();
      if (!SrcReg) continue;
      unsigned SubIdx = MI->getOperand(i+1).getImm();
      UpdateRegSequenceSrcs(SrcReg, DstReg, SubIdx, MRI, *TRI);
    }

    // Set <def,undef> flags on the first DstReg def in the basic block.
    // It marks the beginning of the live range. All the other defs are
    // read-modify-write.
    if (MachineInstr *Def = findFirstDef(DstReg, MRI)) {
      for (unsigned i = 0, e = Def->getNumOperands(); i != e; ++i) {
        MachineOperand &MO = Def->getOperand(i);
        if (MO.isReg() && MO.isDef() && MO.getReg() == DstReg)
          MO.setIsUndef();
      }
      DEBUG(dbgs() << "First def: " << *Def);
    }

    if (IsImpDef) {
      DEBUG(dbgs() << "Turned: " << *MI << " into an IMPLICIT_DEF");
      MI->setDesc(TII->get(TargetOpcode::IMPLICIT_DEF));
      for (int j = MI->getNumOperands() - 1, ee = 0; j > ee; --j)
        MI->RemoveOperand(j);
    } else {
      DEBUG(dbgs() << "Eliminated: " << *MI);
      MI->eraseFromParent();
    }
  }

  RegSequences.clear();
  return true;
}
