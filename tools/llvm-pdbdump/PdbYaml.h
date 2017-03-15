//===- PdbYAML.h ---------------------------------------------- *- C++ --*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_PDBYAML_H
#define LLVM_TOOLS_LLVMPDBDUMP_PDBYAML_H

#include "OutputStyle.h"

#include "llvm/ADT/Optional.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/TypeRecord.h"
#include "llvm/DebugInfo/MSF/MSFCommon.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/PDBTypes.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/YAMLTraits.h"

#include <vector>

namespace llvm {
namespace pdb {

namespace yaml {
struct SerializationContext;

struct MSFHeaders {
  msf::SuperBlock SuperBlock;
  uint32_t NumDirectoryBlocks = 0;
  std::vector<uint32_t> DirectoryBlocks;
  uint32_t NumStreams = 0;
  uint32_t FileSize = 0;
};

struct StreamBlockList {
  std::vector<uint32_t> Blocks;
};

struct NamedStreamMapping {
  StringRef StreamName;
  uint32_t StreamNumber;
};

struct PdbInfoStream {
  PdbRaw_ImplVer Version = PdbImplVC70;
  uint32_t Signature = 0;
  uint32_t Age = 1;
  PDB_UniqueId Guid;
  std::vector<NamedStreamMapping> NamedStreams;
};

struct PdbSymbolRecord {
  codeview::CVSymbol Record;
};

struct PdbModiStream {
  uint32_t Signature;
  std::vector<PdbSymbolRecord> Symbols;
};

struct PdbDbiModuleInfo {
  StringRef Obj;
  StringRef Mod;
  std::vector<StringRef> SourceFiles;
  Optional<PdbModiStream> Modi;
};

struct PdbDbiStream {
  PdbRaw_DbiVer VerHeader = PdbDbiV70;
  uint32_t Age = 1;
  uint16_t BuildNumber = 0;
  uint32_t PdbDllVersion = 0;
  uint16_t PdbDllRbld = 0;
  uint16_t Flags = 1;
  PDB_Machine MachineType = PDB_Machine::x86;

  std::vector<PdbDbiModuleInfo> ModInfos;
};

struct PdbTpiRecord {
  codeview::CVType Record;
};

struct PdbTpiFieldListRecord {
  codeview::CVMemberRecord Record;
};

struct PdbTpiStream {
  PdbRaw_TpiVer Version = PdbTpiV80;
  std::vector<PdbTpiRecord> Records;
};

struct PdbObject {
  explicit PdbObject(BumpPtrAllocator &Allocator) : Allocator(Allocator) {}

  Optional<MSFHeaders> Headers;
  Optional<std::vector<uint32_t>> StreamSizes;
  Optional<std::vector<StreamBlockList>> StreamMap;
  Optional<PdbInfoStream> PdbStream;
  Optional<PdbDbiStream> DbiStream;
  Optional<PdbTpiStream> TpiStream;
  Optional<PdbTpiStream> IpiStream;

  Optional<std::vector<StringRef>> StringTable;

  BumpPtrAllocator &Allocator;
};
}
}
}

namespace llvm {
namespace yaml {

template <> struct MappingTraits<pdb::yaml::PdbObject> {
  static void mapping(IO &IO, pdb::yaml::PdbObject &Obj);
};

template <> struct MappingTraits<pdb::yaml::MSFHeaders> {
  static void mapping(IO &IO, pdb::yaml::MSFHeaders &Obj);
};

template <> struct MappingTraits<msf::SuperBlock> {
  static void mapping(IO &IO, msf::SuperBlock &SB);
};

template <> struct MappingTraits<pdb::yaml::StreamBlockList> {
  static void mapping(IO &IO, pdb::yaml::StreamBlockList &SB);
};

template <> struct MappingTraits<pdb::yaml::PdbInfoStream> {
  static void mapping(IO &IO, pdb::yaml::PdbInfoStream &Obj);
};

template <> struct MappingContextTraits<pdb::yaml::PdbDbiStream, pdb::yaml::SerializationContext> {
  static void mapping(IO &IO, pdb::yaml::PdbDbiStream &Obj, pdb::yaml::SerializationContext &Context);
};

template <>
struct MappingContextTraits<pdb::yaml::PdbTpiStream, pdb::yaml::SerializationContext> {
  static void mapping(IO &IO, pdb::yaml::PdbTpiStream &Obj,
    pdb::yaml::SerializationContext &Context);
};

template <> struct MappingTraits<pdb::yaml::NamedStreamMapping> {
  static void mapping(IO &IO, pdb::yaml::NamedStreamMapping &Obj);
};

template <> struct MappingContextTraits<pdb::yaml::PdbSymbolRecord, pdb::yaml::SerializationContext> {
  static void mapping(IO &IO, pdb::yaml::PdbSymbolRecord &Obj, pdb::yaml::SerializationContext &Context);
};

template <> struct MappingContextTraits<pdb::yaml::PdbModiStream, pdb::yaml::SerializationContext> {
  static void mapping(IO &IO, pdb::yaml::PdbModiStream &Obj, pdb::yaml::SerializationContext &Context);
};

template <> struct MappingContextTraits<pdb::yaml::PdbDbiModuleInfo, pdb::yaml::SerializationContext> {
  static void mapping(IO &IO, pdb::yaml::PdbDbiModuleInfo &Obj, pdb::yaml::SerializationContext &Context);
};

template <>
struct MappingContextTraits<pdb::yaml::PdbTpiRecord,
                            pdb::yaml::SerializationContext> {
  static void mapping(IO &IO, pdb::yaml::PdbTpiRecord &Obj,
                      pdb::yaml::SerializationContext &Context);
};
}
}

#endif // LLVM_TOOLS_LLVMPDBDUMP_PDBYAML_H
