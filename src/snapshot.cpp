//! \file
/*
**  Copyright (C) - Triton
**
**  This program is under the terms of the BSD License.
*/

#include <exception>
#include <iostream>

#include "snapshot.hpp"
#include "globals.hpp"
#include "utils.hpp"
#include "context.hpp"

#include "dbg.hpp"

Snapshot::Snapshot() {
    this->locked = true;
    this->snapshotTaintEngine = nullptr;
    this->snapshotSymEngine = nullptr;
    this->astCtx = nullptr;
    this->cpu_x8664 = nullptr;
    this->cpu_x86 = nullptr;
    this->cpu_AArch64 = nullptr;
    this->cpu_Arm32 = nullptr;
    this->mustBeRestore = false;
    this->snapshotTaken = false;
    this->savedSp = BADADDR;
    this->address = BADADDR;
}


Snapshot::~Snapshot() {
    releaseSavedState();
}

void Snapshot::releaseSavedState() {
    delete this->snapshotSymEngine;
    this->snapshotSymEngine = nullptr;
    delete this->snapshotTaintEngine;
    this->snapshotTaintEngine = nullptr;
    delete this->astCtx;
    this->astCtx = nullptr;
    delete this->cpu_x8664;
    this->cpu_x8664 = nullptr;
    delete this->cpu_x86;
    this->cpu_x86 = nullptr;
    delete this->cpu_AArch64;
    this->cpu_AArch64 = nullptr;
    delete this->cpu_Arm32;
    this->cpu_Arm32 = nullptr;
}

/* Check if the snapshot has been taken */
bool Snapshot::exists(void) {
    return this->snapshotTaken;
}

/* Add the modification byte. */
void Snapshot::addModification(ea_t mem, char byte) {
    if (this->locked == false && this->memory.find(mem) == this->memory.end())
        this->memory[mem] = byte;
}


/* Enable the snapshot engine. */
void Snapshot::takeSnapshot() {
    if (snapshotTaken)
        return;
    ea_t sp;
    if (!get_sp_val(&sp)) {
        msg("[!] Cannot take snapshot: debugger stack pointer unavailable\n");
        return;
    }
    this->savedSp = sp;
    this->IDAContext.clear();
    try {
        /* Save Triton symbolic and taint state. */
        this->snapshotSymEngine = new triton::engines::symbolic::SymbolicEngine(*tritonCtx.getSymbolicEngine());
        this->snapshotTaintEngine = new triton::engines::taint::TaintEngine(*tritonCtx.getTaintEngine());
        this->astCtx = new triton::ast::AstContext(*tritonCtx.getAstContext());

        /* Construct CPU copies with their own Capstone handles. */
        switch (tritonCtx.getArchitecture()) {
        case triton::arch::ARCH_X86_64:
            this->cpu_x8664 = new triton::arch::x86::x8664Cpu(nullptr);
            *this->cpu_x8664 = *static_cast<triton::arch::x86::x8664Cpu*>(tritonCtx.getCpuInstance());
            break;
        case triton::arch::ARCH_X86:
            this->cpu_x86 = new triton::arch::x86::x86Cpu(nullptr);
            *this->cpu_x86 = *static_cast<triton::arch::x86::x86Cpu*>(tritonCtx.getCpuInstance());
            break;
        case triton::arch::ARCH_AARCH64:
            this->cpu_AArch64 = new triton::arch::arm::aarch64::AArch64Cpu(nullptr);
            *this->cpu_AArch64 = *static_cast<triton::arch::arm::aarch64::AArch64Cpu*>(tritonCtx.getCpuInstance());
            break;
        case triton::arch::ARCH_ARM32:
            this->cpu_Arm32 = new triton::arch::arm::arm32::Arm32Cpu(nullptr);
            *this->cpu_Arm32 = *static_cast<triton::arch::arm::arm32::Arm32Cpu*>(tritonCtx.getCpuInstance());
            break;
        default:
            throw triton::exceptions::Architecture("Architecture not supported.");
        }

        /* Save integer parent registers only; subregister writes overlap them. */
        const auto &regs = tritonCtx.getAllRegisters();
        for (const auto& [id, reg] : regs) {
            if (reg.getId() != reg.getParent() || reg.getSize() > sizeof(uint64))
                continue;
            uint64 ival;
            if (get_reg_val(reg.getName().c_str(), &ival))
                this->IDAContext[reg.getName()] = ival;
        }
        if (tritonCtx.getArchitecture() == triton::arch::ARCH_X86_64)
            this->IDAContext["rsp"] = sp;
        else if (tritonCtx.getArchitecture() == triton::arch::ARCH_X86)
            this->IDAContext["esp"] = sp;
        else
            this->IDAContext["sp"] = sp;
        msg("[+] Snapshot saved SP " MEM_FORMAT "\n", sp);
        const auto rdi = this->IDAContext.find("rdi");
        if (rdi != this->IDAContext.end())
            msg("[+] Snapshot saved RDI " MEM_FORMAT "\n", (ea_t)rdi->second);

        this->saved_ponce_runtime_status = ponce_runtime_status;
        this->snapshotTaken = true;
        this->locked = false;
    } catch (const std::exception& e) {
        releaseSavedState();
        this->IDAContext.clear();
        this->savedSp = BADADDR;
        msg("[!] Cannot take snapshot: %s\n", e.what());
    }
}

void Snapshot::setAddress(ea_t address) {
    this->address = address;
}


/* Restore the snapshot. */
bool Snapshot::restoreSnapshot() {

    /* 1 - Restore all memory modification. */
    for (auto i = this->memory.begin(); i != this->memory.end(); ++i) {
        if (write_dbg_memory(i->first, &i->second, 1) != 1)
            msg("[!] Could not restore memory at " MEM_FORMAT "\n", i->first);
    }
    this->memory.clear();

    /* 2 - Restore current symbolic engine state */
    *tritonCtx.getSymbolicEngine() = *this->snapshotSymEngine;

    /* 3 - Restore current taint engine state */
    *tritonCtx.getTaintEngine() = *this->snapshotTaintEngine;

    /* 4 - Restore current AST context */
    *tritonCtx.getAstContext() = *this->astCtx;

    /* 5 - Restore the Triton CPU state */
    switch (tritonCtx.getArchitecture()) {
    case triton::arch::ARCH_X86_64:
        *reinterpret_cast<triton::arch::x86::x8664Cpu*>(tritonCtx.getCpuInstance()) = *this->cpu_x8664;
        break;
    case triton::arch::ARCH_X86:
        *reinterpret_cast<triton::arch::x86::x86Cpu*>(tritonCtx.getCpuInstance()) = *this->cpu_x86;
        break;
    case triton::arch::ARCH_AARCH64:
        *reinterpret_cast<triton::arch::arm::aarch64::AArch64Cpu*>(tritonCtx.getCpuInstance()) = *this->cpu_AArch64;
        break;
    case triton::arch::ARCH_ARM32:
        *reinterpret_cast<triton::arch::arm::arm32::Arm32Cpu*>(tritonCtx.getCpuInstance()) = *this->cpu_Arm32;
        break;
    default:
        throw triton::exceptions::Architecture("Architecture not supported.");
        break;
    }

    this->mustBeRestore = false;

    /* 6 - Restore IDA registers context
    Suposedly XIP should be set at the same time and execution redirected*/
    const char *ip_register = tritonCtx.getArchitecture() == triton::arch::ARCH_X86_64 ? "rip"
                            : tritonCtx.getArchitecture() == triton::arch::ARCH_X86 ? "eip" : "pc";
    const char *sp_register = tritonCtx.getArchitecture() == triton::arch::ARCH_X86_64 ? "rsp"
                            : tritonCtx.getArchitecture() == triton::arch::ARCH_X86 ? "esp" : "sp";
    bool registersRestored = true;
    for (const auto& [name, value] : this->IDAContext) {
        if (name == ip_register || name == sp_register)
            continue;
        if (!IDA_setCurrentRegisterValue(name.c_str(), value)) {
            msg("[!] ERROR restoring register %s\n", name.c_str());
            registersRestored = false;
        }
    }
    if (!IDA_setCurrentRegisterValue(sp_register, savedSp)) {
        msg("[!] ERROR restoring stack pointer %s\n", sp_register);
        registersRestored = false;
    }
    msg("[+] Snapshot restoring SP " MEM_FORMAT "\n", savedSp);
    const auto rdi = this->IDAContext.find("rdi");
    if (rdi != this->IDAContext.end())
        msg("[+] Snapshot restoring RDI " MEM_FORMAT "\n", (ea_t)rdi->second);
    // Switch instruction pointer last, after stack and other registers are valid.
    if (!IDA_setCurrentRegisterValue(ip_register, address)) {
        msg("[!] ERROR restoring instruction pointer %s\n", ip_register);
        registersRestored = false;
    }
    ea_t restoredSp = BADADDR;
    if (!get_sp_val(&restoredSp) || restoredSp != savedSp) {
        msg("[!] Snapshot SP mismatch: saved " MEM_FORMAT ", debugger has " MEM_FORMAT "\n", savedSp, restoredSp);
        registersRestored = false;
    }
    if (rdi != this->IDAContext.end()) {
        uint64 restoredRdi = BADADDR;
        if (!get_reg_val("rdi", &restoredRdi) || restoredRdi != rdi->second) {
            msg("[!] Snapshot RDI mismatch: saved " MEM_FORMAT ", debugger has " MEM_FORMAT "\n", (ea_t)rdi->second, (ea_t)restoredRdi);
            registersRestored = false;
        }
    }
    if (!registersRestored) {
        register_info_t info;
        if (get_dbg_reg_info(sp_register, &info))
            msg("[!] Debugger %s register dtype: %d (dt_qword: %d)\n", sp_register, int(info.dtype), int(dt_qword));
        msg("[!] Snapshot restore incomplete; restart debuggee before continuing\n");
        return false;
    }

    /* 7 - Restore the Ponce status */
    ponce_runtime_status = this->saved_ponce_runtime_status;

    /* 8 - We need to set to NULL the last instruction. We are deleting the last instructions in the Tritonize callback.
    So after restore a snapshot if last_instruction is not NULL is double freeing the same instruction */
    ponce_runtime_status.last_triton_instruction = nullptr;
    return true;
}

/* Disable the snapshot engine. */
void Snapshot::disableSnapshot(void) {
    this->locked = true;
}


/* Reset the snapshot engine.
* Clear all backups for a new snapshot. */
void Snapshot::resetEngine(void) {
    this->memory.clear();
    releaseSavedState();

    const bool hadSnapshot = this->snapshotTaken;
    this->snapshotTaken = false;
    this->locked = true;
    this->mustBeRestore = false;
    this->savedSp = BADADDR;
    this->IDAContext.clear();

    //We delete the comment and color that we created
    if (hadSnapshot && this->address != BADADDR) {
        ponce_set_cmt(this->address, "", false, false, false);
        del_item_color(this->address);
    }
    this->address = BADADDR;
}


/* Check if the snapshot engine is locked. */
bool Snapshot::isLocked(void) {
    return this->locked;
}


/* Check if we must restore the snapshot */
bool Snapshot::mustBeRestored(void) {
    return this->mustBeRestore;
}


/* Check if we must restore the snapshot */
void Snapshot::setRestore(bool flag) {
    this->mustBeRestore = flag;
}
