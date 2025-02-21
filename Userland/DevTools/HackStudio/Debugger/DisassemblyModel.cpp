/*
 * Copyright (c) 2020, Luke Wilde <lukew@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "DisassemblyModel.h"
#include <AK/MappedFile.h>
#include <AK/StringBuilder.h>
#include <LibDebug/DebugSession.h>
#include <LibELF/Image.h>
#include <LibX86/Disassembler.h>
#include <LibX86/ELFSymbolProvider.h>
#include <ctype.h>
#include <stdio.h>

namespace HackStudio {

DisassemblyModel::DisassemblyModel(const Debug::DebugSession& debug_session, const PtraceRegisters& regs)
{
    auto lib = debug_session.library_at(regs.eip);
    if (!lib)
        return;
    auto containing_function = lib->debug_info->get_containing_function(regs.eip - lib->base_address);
    if (!containing_function.has_value()) {
        dbgln("Cannot disassemble as the containing function was not found.");
        return;
    }

    OwnPtr<ELF::Image> kernel_elf;
    const ELF::Image* elf = nullptr;

    // FIXME: Use /proc for this
#if ARCH(I386)
    FlatPtr kernel_base = 0xc0000000;
#else
    FlatPtr kernel_base = 0x2000000000;
#endif

    if (containing_function.value().address_low >= kernel_base) {
        auto file_or_error = MappedFile::map("/boot/Kernel.debug");
        if (file_or_error.is_error())
            return;
        kernel_elf = make<ELF::Image>(file_or_error.value()->bytes());
        elf = kernel_elf.ptr();
    } else {
        elf = &lib->debug_info->elf();
    }

    auto symbol = elf->find_symbol(containing_function.value().address_low);
    if (!symbol.has_value())
        return;
    VERIFY(symbol.has_value());

    auto view = symbol.value().raw_data();

    X86::ELFSymbolProvider symbol_provider(*elf);
    X86::SimpleInstructionStream stream((const u8*)view.characters_without_null_termination(), view.length());
    X86::Disassembler disassembler(stream);

    size_t offset_into_symbol = 0;
    for (;;) {
        auto insn = disassembler.next();
        if (!insn.has_value())
            break;
        FlatPtr address_in_profiled_program = symbol.value().value() + offset_into_symbol;
        auto disassembly = insn.value().to_string(address_in_profiled_program, &symbol_provider);
        StringView instruction_bytes = view.substring_view(offset_into_symbol, insn.value().length());
        m_instructions.append({ insn.value(), disassembly, instruction_bytes, address_in_profiled_program });

        offset_into_symbol += insn.value().length();
    }
}

DisassemblyModel::~DisassemblyModel()
{
}

int DisassemblyModel::row_count(const GUI::ModelIndex&) const
{
    return m_instructions.size();
}

String DisassemblyModel::column_name(int column) const
{
    switch (column) {
    case Column::Address:
        return "Address";
    case Column::InstructionBytes:
        return "Insn Bytes";
    case Column::Disassembly:
        return "Disassembly";
    default:
        VERIFY_NOT_REACHED();
        return {};
    }
}

GUI::Variant DisassemblyModel::data(const GUI::ModelIndex& index, GUI::ModelRole role) const
{
    auto& insn = m_instructions[index.row()];

    if (role == GUI::ModelRole::Display) {
        if (index.column() == Column::Address)
            return String::formatted("{:p}", insn.address);
        if (index.column() == Column::InstructionBytes) {
            StringBuilder builder;
            for (auto ch : insn.bytes)
                builder.appendff("{:02x} ", static_cast<unsigned char>(ch));
            return builder.to_string();
        }
        if (index.column() == Column::Disassembly)
            return insn.disassembly;
        return {};
    }
    return {};
}

void DisassemblyModel::update()
{
    did_update();
}

}
